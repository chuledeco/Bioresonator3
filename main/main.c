#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "main.h"
#include "webServer.h"
#include "i2s_es8311.h"
#include "leds.h"

static const char *TAG = "main";

bool sending = false;
bool receiving = false;
bool recording = false;
bool playing = false;

void print_memory_status(void)
{
    #include "esp_heap_caps.h"
    #include "esp_system.h"
    ESP_LOGI(TAG, "-------------------- MEMORY STATUS --------------------");

    // --- Heap general ---
    ESP_LOGI(TAG, "Total heap:      %d bytes", (int)heap_caps_get_total_size(MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "Free heap:       %d bytes", (int)esp_get_free_heap_size());
    ESP_LOGI(TAG, "Min free heap:   %d bytes", (int)esp_get_minimum_free_heap_size());

    // --- RAM interna (32-bit) ---
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t internal_min  = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "Internal RAM free: %d bytes (min ever: %d)", internal_free, internal_min);

    // --- PSRAM ---
#if CONFIG_SPIRAM
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psram_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_min   = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "PSRAM total: %d bytes", psram_total);
    ESP_LOGI(TAG, "PSRAM free:  %d bytes (min ever: %d)", psram_free, psram_min);
#else
    ESP_LOGI(TAG, "PSRAM not enabled in this build");
#endif

    // --- Detalle por tipo de memoria ---
    ESP_LOGI(TAG, "8-bit accessible:  %d bytes free", (int)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "32-bit accessible: %d bytes free", (int)heap_caps_get_free_size(MALLOC_CAP_32BIT));

    ESP_LOGI(TAG, "--------------------------------------------------------\n");
}

void configure_gpio(){
    // AMP_CTRL: 1 -> Amplifier on
    // SW1:0 SW2:0 -> Antenna as input
    // SW1:1 SW2:1 -> Antenna as output
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SW1) | (1ULL << SW2) | (1ULL << AMP_CTRL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_ERROR_CHECK(gpio_set_level(AMP_CTRL, 0));
    ESP_ERROR_CHECK(gpio_set_level(SW1, 1));
    ESP_ERROR_CHECK(gpio_set_level(SW2, 1));
}

void ap_init(){
    ESP_LOGI(TAG, "Initializing WiFi in AP mode...");

    esp_netif_init();
    esp_event_loop_create_default();

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    assert(ap_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    char ssid[32] = SSID;
    char password[64] = PASSWORD;
    wifi_config_t ap_config = {
        .ap = {
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK
        },
    };

    strncpy((char *)ap_config.ap.ssid, ssid, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid[sizeof(ap_config.ap.ssid) - 1] = '\0'; // Ensure null termination
    strncpy((char *)ap_config.ap.password, password, sizeof(ap_config.ap.password));
    ap_config.ap.password[sizeof(ap_config.ap.password) - 1] = '\0'; // Ensure null termination

    if (strlen((char *)ap_config.ap.password) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        ap_config.ap.ssid_len = strlen((char*)ap_config.ap.ssid);    
    }

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "WiFi AP initialized with SSID: %s, Password: %s", 
        (char *)ap_config.ap.ssid, 
        (char *)ap_config.ap.password);
}

void app_main(void)
{
    ESP_LOGI("main", "Bioresonator BC initializing...");

    ESP_LOGI(TAG, "Initializing NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Initializing WIFI...");
    ap_init();
    ESP_LOGI(TAG, "Initializing Web Server...");
    ws_init();

    ESP_LOGI(TAG, "Configure GPIOs...");
    configure_gpio();

    ESP_LOGI(TAG, "Reserve memory for audio files...");
    ESP_ERROR_CHECK(reserve_memory_for_audio_files());

    ESP_LOGI(TAG, "Initialize I2S, ES8311 codec and audio task");
    ESP_ERROR_CHECK(init_i2s_es8311(I2S_SAMPLE_RATE));

    ESP_LOGI(TAG, "Initialize LEDs...");
    xTaskCreate(leds_fill_alternating_task, "leds_task", 2*2048, NULL, 5, NULL);

    while(1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}