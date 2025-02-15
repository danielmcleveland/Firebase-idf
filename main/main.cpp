#include <iostream>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "driver/gpio.h"

#include "esp_firebase/app.h"
#include "esp_firebase/rtdb.h"

#include "wifi_utils.h"
#include "firebase_config.h"
using namespace ESPFirebase;

extern "C" void app_main(void)
{
    // Initialize NVS for persistent storage
    ESP_ERROR_CHECK(nvs_flash_init());
    
    // Connect to WiFi (blocking until connected)
    wifiInit(SSID, PASSWORD);
    
    // Firebase Authentication
    FirebaseApp app = FirebaseApp(API_KEY);
    
    // Attempt to login with user credentials
    user_account_t account = {USER_EMAIL, USER_PASSWORD};
    if (app.loginUserAccount(account) != ESP_OK)
    {
        ESP_LOGE("MAIN", "Failed to log in to Firebase");
        return;
    }
    
    // Initialize Realtime Database
    RTDB db = RTDB(&app, DATABASE_URL);
    
    // Configure GPIO for garage door status detection
    gpio_reset_pin(GPIO_NUM_32);
    gpio_pad_select_gpio(GPIO_NUM_32);
    gpio_set_direction(GPIO_NUM_32, GPIO_MODE_INPUT);
    
    bool is_garage_door_open = false;
    bool is_garage_door_open_old = false;
    
    while (true)
    {
        is_garage_door_open = gpio_get_level(GPIO_NUM_32);
        
        if (is_garage_door_open_old != is_garage_door_open)
        {
            ESP_LOGI("MAIN", "Garage door state changed: %s", is_garage_door_open ? "OPEN" : "CLOSED");
            
            if (db.putData("/garage_open", is_garage_door_open) != ESP_OK)
            {
                ESP_LOGE("MAIN", "Failed to update garage door status in database");
            }
            is_garage_door_open_old = is_garage_door_open;
        }
        else
        {
            ESP_LOGI("MAIN", "Garage door state unchanged: %s", is_garage_door_open ? "OPEN" : "CLOSED");
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1-second delay
    }
}
