#ifndef _ESP_FIREBASE_H_
#define _ESP_FIREBASE_H_

#include "esp_http_client.h"
#include <string>

#define HTTP_RECV_BUFFER_SIZE 4096

namespace ESPFirebase 
{
    struct user_account_t
    {
        const char* user_email;
        const char* user_password;
    };
    
    struct http_ret_t
    {
        esp_err_t err;
        int status_code;
    }; 

    class FirebaseApp 
    {
    private:
        const char* https_certificate;
        std::string api_key = "";
        std::string register_url = "https://identitytoolkit.googleapis.com/v1/accounts:signUp?key=";
        std::string login_url = "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=";
        std::string auth_url = "https://securetoken.googleapis.com/v1/token?key=";
        std::string refresh_token = "";
        
        uint32_t token_expiry_time = 0;  // ⬅️ New: Store token expiration time (in seconds)
        
        esp_http_client_handle_t client;
        bool client_initialized = false;

        void firebaseClientInit(void);
        
        esp_err_t getRefreshToken(bool register_account);
        esp_err_t getAuthToken();

        esp_err_t nvsSaveTokens();  // ⬅️ Will now store auth_token, refresh_token, and token_expiry_time
        esp_err_t nvsReadTokens();  // ⬅️ Will now retrieve auth_token, refresh_token, and token_expiry_time

    public:
        user_account_t user_account = {"", ""};

        char* local_response_buffer;
        std::string auth_token = "";

        http_ret_t performRequest(const char* url, esp_http_client_method_t method, std::string post_field = "");
        esp_err_t setHeader(const char* header, const char* value);
        void clearHTTPBuffer(void);
        
        FirebaseApp(const char* api_key);
        ~FirebaseApp();

        esp_err_t registerUserAccount(const user_account_t& account);
        esp_err_t loginUserAccount(const user_account_t& account);
        
        void ensureValidAuthToken();  // ⬅️ New: Checks and refreshes token before expiry
    };
}

#endif
