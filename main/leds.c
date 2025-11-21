#include <stdio.h>
#include <esp_log.h>
#include <esp_err.h>
#include <driver/gpio.h>
#include "led_strip.h"
#include "leds.h"
#include "main.h"

static const char *TAG = "leds";

led_strip_handle_t configure_led(void)
{
    // LED strip general initialization, according to your led board design
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO_PIN, // The GPIO that connected to the LED strip's data line
        .max_leds = LED_STRIP_LED_COUNT,      // The number of LEDs in the strip,
        .led_model = LED_MODEL_WS2812,        // LED strip model
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB, // The color order of the strip: GRB
        .flags = {
            .invert_out = false, // don't invert the output signal
        }
    };

    // LED strip backend configuration: RMT
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,        // different clock source can lead to different power consumption
        .resolution_hz = LED_STRIP_RMT_RES_HZ, // RMT counter clock frequency
        .mem_block_symbols = LED_STRIP_MEMORY_BLOCK_WORDS, // the memory block size used by the RMT channel
        .flags = {
            .with_dma = LED_STRIP_USE_DMA,     // Using DMA can improve performance when driving more LEDs
        }
    };

    // LED Strip object handle
    led_strip_handle_t led_strip;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_LOGI(TAG, "Created LED strip object with RMT backend");
    return led_strip;
}

static void leds_fill_alternating_task(void *arg)
{
    led_strip_handle_t led_strip = configure_led();
    ESP_ERROR_CHECK(led_strip_clear(led_strip));
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));

    size_t idx = 0;
    bool blue_phase = true;  // true = blue, false = yellow

    while (1) {
        if (sending == true) {
            for (size_t i = 0; i < LED_STRIP_LED_COUNT; i++) {
                ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, BRIGHTNESS, 0, 0)); // Red color for sending
            }
            ESP_ERROR_CHECK(led_strip_refresh(led_strip));
        } else if (receiving == true) {
            for (size_t i = 0; i < LED_STRIP_LED_COUNT; i++) {
                ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, 0, BRIGHTNESS, 0)); // Green color for receiving
            }
            ESP_ERROR_CHECK(led_strip_refresh(led_strip));
        } else if (recording == true) {
            for (size_t i = 0; i < LED_STRIP_LED_COUNT; i++) {
                ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, BRIGHTNESS, 0, 0)); // Red color for sending
            }
            ESP_ERROR_CHECK(led_strip_refresh(led_strip));
        } else if (playing == true) {
            for (size_t i = 0; i < LED_STRIP_LED_COUNT; i++) {
                ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, 0, BRIGHTNESS, 0)); // Green color for sending
            }
            ESP_ERROR_CHECK(led_strip_refresh(led_strip));
        } else {
            const uint8_t *c = blue_phase ? COLOR_BLUE : COLOR_YELLOW;
            ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, idx, c[0], c[1], c[2]));
            ESP_ERROR_CHECK(led_strip_refresh(led_strip));
            idx++;
            if (idx >= LED_STRIP_LED_COUNT) {
                idx = 0;
                blue_phase = !blue_phase;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(LEDS_UPDATE_INTERVAL));
    }
}