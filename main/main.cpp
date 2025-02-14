
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
    wifiInit(SSID, PASSWORD); // blocking until it connects

    // Config and Authentication
    user_account_t account = {USER_EMAIL, USER_PASSWORD};

    FirebaseApp app = FirebaseApp(API_KEY);

    app.loginUserAccount(account);

    RTDB db = RTDB(&app, DATABASE_URL);

    gpio_reset_pin(GPIO_NUM_32);
    gpio_pad_select_gpio(GPIO_NUM_32);
    gpio_set_direction(GPIO_NUM_32, GPIO_MODE_INPUT);

    bool is_garage_door_open = 0;
    bool is_garage_door_open_old = 0;
    while (true)
    {
        is_garage_door_open = gpio_get_level(GPIO_NUM_32);

        if (is_garage_door_open_old == is_garage_door_open)
        {
            printf("is_garage_door_open is unchanged and: %s\n", is_garage_door_open ? "true" : "false");
        }
        else
        {
            printf("is_garage_door_open has changed and is now: %s\n", is_garage_door_open ? "true" : "false");
            db.putData("/garage_open", is_garage_door_open);
            is_garage_door_open_old = is_garage_door_open;
        }

        vTaskDelay(1000);
    }
}
