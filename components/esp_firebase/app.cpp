#include <iostream>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "app.h"

#include "jsoncpp/value.h"
#include "jsoncpp/json.h"

#define NVS_TAG "NVS"
#define HTTP_TAG "HTTP_CLIENT"
#define FIREBASE_APP_TAG "FirebaseApp"

extern const char cert_start[] asm("_binary_gtsr1_pem_start");
extern const char cert_end[] asm("_binary_gtsr1_pem_end");

static int output_len = 0; 

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(HTTP_TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(HTTP_TAG, "HTTP_EVENT_ON_CONNECTED");
            memset(evt->user_data, 0, HTTP_RECV_BUFFER_SIZE);
            output_len = 0;
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(HTTP_TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            memcpy(evt->user_data + output_len, evt->data, evt->data_len);
            output_len += evt->data_len;
            break;
        default:
            break;
    }
    return ESP_OK;
}

namespace ESPFirebase {

void FirebaseApp::firebaseClientInit(void)
{   
    esp_http_client_config_t config = {0};
    config.url = "https://google.com";    
    config.event_handler = http_event_handler;
    config.cert_pem = FirebaseApp::https_certificate;
    config.user_data = FirebaseApp::local_response_buffer;
    config.buffer_size_tx = 4096;
    config.buffer_size = HTTP_RECV_BUFFER_SIZE;
    FirebaseApp::client = esp_http_client_init(&config);
    ESP_LOGD(FIREBASE_APP_TAG, "HTTP Client Initialized");
}

http_ret_t FirebaseApp::performRequest(const char* url, esp_http_client_method_t method, std::string post_field)
{
    ensureValidAuthToken();  // ✅ Ensure valid token before making a request
    
    ESP_ERROR_CHECK(esp_http_client_set_url(FirebaseApp::client, url));
    ESP_ERROR_CHECK(esp_http_client_set_method(FirebaseApp::client, method));
    ESP_ERROR_CHECK(esp_http_client_set_post_field(FirebaseApp::client, post_field.c_str(), post_field.length()));

    esp_err_t err = esp_http_client_perform(FirebaseApp::client);
    int status_code = esp_http_client_get_status_code(FirebaseApp::client);

    if (err != ESP_OK || status_code != 200)
    {
        ESP_LOGE(FIREBASE_APP_TAG, "Error while performing request esp_err_t code=0x%x | status_code=%d", (int)err, status_code);
        ESP_LOGE(FIREBASE_APP_TAG, "response=\n%s", local_response_buffer);
    }
    return {err, status_code};
}

void FirebaseApp::ensureValidAuthToken()
{
    uint32_t now = xTaskGetTickCount() / portTICK_PERIOD_MS;
    
    if (now >= FirebaseApp::token_expiry_time - (5 * 60 * 1000))  // Refresh 5 minutes before expiry
    {
        ESP_LOGI(FIREBASE_APP_TAG, "Auth token is about to expire. Refreshing...");
        if (FirebaseApp::getAuthToken() != ESP_OK)
        {
            ESP_LOGE(FIREBASE_APP_TAG, "Failed to refresh token.");
        }
    }
}

esp_err_t FirebaseApp::getAuthToken()
{
    http_ret_t http_ret;

    std::string token_post_data = R"({"grant_type": "refresh_token", "refresh_token":")";
    token_post_data += FirebaseApp::refresh_token + "\"}";

    FirebaseApp::setHeader("content-type", "application/json");
    http_ret = FirebaseApp::performRequest(FirebaseApp::auth_url.c_str(), HTTP_METHOD_POST, token_post_data);
    
    if (http_ret.err == ESP_OK && http_ret.status_code == 200)
    {
        Json::Reader reader;
        Json::Value data;
        reader.parse(FirebaseApp::local_response_buffer, data, false);
        
        FirebaseApp::auth_token = data["access_token"].asString();
        int expires_in = data["expires_in"].asInt(); 
        FirebaseApp::token_expiry_time = xTaskGetTickCount() / portTICK_PERIOD_MS + (expires_in * 1000);

        ESP_LOGI(FIREBASE_APP_TAG, "New Auth Token retrieved, expires in %d seconds", expires_in);
        nvsSaveTokens();
        
        return ESP_OK;
    }
    
    ESP_LOGE(FIREBASE_APP_TAG, "Failed to refresh auth token.");
    return ESP_FAIL;
}

esp_err_t FirebaseApp::nvsSaveTokens()
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    err |= nvs_set_str(my_handle, "refresh", FirebaseApp::refresh_token.c_str());
    err |= nvs_set_u32(my_handle, "expiry_time", FirebaseApp::token_expiry_time);
    err |= nvs_commit(my_handle);
    nvs_close(my_handle);

    ESP_LOGI(NVS_TAG, "Tokens saved to NVS.");
    return err;
}

esp_err_t FirebaseApp::nvsReadTokens()
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    size_t refresh_len = 500;
    char refresh[500] = {0};

    err |= nvs_get_str(my_handle, "refresh", refresh, &refresh_len);
    err |= nvs_get_u32(my_handle, "expiry_time", &FirebaseApp::token_expiry_time);
    nvs_close(my_handle);

    if (err == ESP_OK)
    {
        FirebaseApp::refresh_token = std::string(refresh);
        ESP_LOGI(NVS_TAG, "Refresh token and expiry time loaded from NVS.");
    }

    return err;
}

FirebaseApp::FirebaseApp(const char* api_key)
    : https_certificate(cert_start), api_key(api_key)
{
    FirebaseApp::local_response_buffer = new char[HTTP_RECV_BUFFER_SIZE];
    FirebaseApp::register_url += FirebaseApp::api_key; 
    FirebaseApp::login_url += FirebaseApp::api_key;
    FirebaseApp::auth_url += FirebaseApp::api_key;

    nvsReadTokens(); // ✅ Load refresh token on boot

    if (!FirebaseApp::refresh_token.empty()) 
    {
        ESP_LOGI(FIREBASE_APP_TAG, "Existing refresh token found, attempting authentication...");
        getAuthToken();
    }
    
    firebaseClientInit();
}

FirebaseApp::~FirebaseApp()
{
    delete[] FirebaseApp::local_response_buffer;
    esp_http_client_cleanup(FirebaseApp::client);
}

} // namespace ESPFirebase
