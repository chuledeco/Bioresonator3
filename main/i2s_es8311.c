#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_vol.h"
#include "esp_check.h"
#include "esp_log.h"
#include "math.h"
#include "main.h"
#include "esp_heap_caps.h"
#include "driver/i2s_std.h"
#include "i2s_es8311.h"


uint8_t audio_index = 0;
uint8_t aquisition_length_seconds = 10; // seconds

static const char *TAG = "i2s_es8311";
i2s_chan_handle_t tx_handle = NULL;
i2s_chan_handle_t rx_handle = NULL;

uint16_t *file_from_web = NULL;
uint16_t *file_from_mic = NULL;

uint32_t readed_samples = 0;

bool record = false;

esp_err_t reserve_memory_for_audio_files()
{
    file_from_web = heap_caps_malloc(MAX_LENGTH_SECONDS * I2S_SAMPLE_RATE * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (file_from_web == NULL) {
        ESP_LOGE(TAG, "No memory for file_from_web buffer");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "file_from_web addr: %p", file_from_web);

    // file_from_mic = heap_caps_malloc(MAX_LENGTH_SECONDS * I2S_SAMPLE_RATE * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    size_t buffer_size = MAX_LENGTH_SECONDS * I2S_SAMPLE_RATE * sizeof(uint16_t);
    buffer_size = ((buffer_size + 31) / 32 + 1) * 32;  // Redondear a múltiplo de 32

    
    file_from_mic = heap_caps_aligned_alloc(
        32,                    // Alineación a 32 bytes (CACHE_LINE_SIZE)
        buffer_size,
        MALLOC_CAP_SPIRAM      // Solo SPIRAM, SIN 8BIT
    );    
    
    ESP_LOGI(TAG, "file_from_mic allocated at %p, aligned: %s, size: %d bytes", 
        file_from_mic,
        ((uintptr_t)file_from_mic % 32 == 0) ? "YES" : "NO",
        buffer_size
    );
    
    heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);

    if (file_from_mic == NULL) {
        ESP_LOGE(TAG, "No memory for file_from_mic buffer");
        return ESP_FAIL;
    }
    
    if (!file_from_web || !file_from_mic) {
        ESP_LOGE("MEM", "Error allocating I2S buffers");
        return ESP_FAIL;
    }

    ESP_LOGI("MEM", "file_from_web at %p", file_from_web);
    ESP_LOGI("MEM", "file_from_mic at %p", file_from_mic);

    // opcional: verificar si están en PSRAM
    if (esp_ptr_external_ram(file_from_web))
        ESP_LOGI("MEM", "RX buffer is in PSRAM");
    else
        ESP_LOGW("MEM", "RX buffer is in internal RAM");

    if (esp_ptr_external_ram(file_from_mic))
        ESP_LOGI("MEM", "TX buffer is in PSRAM");
    else
        ESP_LOGW("MEM", "TX buffer is in internal RAM");
        
    ESP_LOGI(TAG, "malloc returned: %p", file_from_mic);
    print_memory_status();  // tu función de debug
    ESP_LOGI(TAG, "malloc after print: %p", file_from_mic);
    ESP_LOGI(TAG, "file_from_mic addr: %p", file_from_mic);
    return ESP_OK;
}

static esp_err_t es8311_codec_init(void)
{
    // TODO: Llevar esto al main
    /* Initialize I2C peripheral */
    i2c_master_bus_handle_t i2c_bus_handle = NULL;
    i2c_master_bus_config_t i2c_mst_cfg = {
        .i2c_port = I2C_NUM,
        .sda_io_num = I2C_SDA_IO,
        .scl_io_num = I2C_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_cfg, &i2c_bus_handle));

    /* Create control interface with I2C bus handle */
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus_handle,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(ctrl_if);

    /* Create data interface with I2S bus handle */
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM,
        .rx_handle = rx_handle,
        .tx_handle = tx_handle,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    assert(data_if);

    /* Create ES8311 interface handle */
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    assert(gpio_if);
    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .master_mode = false,
        .use_mclk = I2S_MCK_IO >= 0,
        .pa_pin = PA_CTRL_IO,
        .pa_reverted = false,
        .hw_gain = {
            .pa_voltage = 5.0,
            .codec_dac_voltage = 3.3,
        },
        .mclk_div = MCLK_MULTIPLE,
    };
    const audio_codec_if_t *es8311_if = es8311_codec_new(&es8311_cfg);
    assert(es8311_if);

    /* Create the top codec handle with ES8311 interface handle and data interface */
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = es8311_if,
        .data_if = data_if,
    };
    esp_codec_dev_handle_t codec_handle = esp_codec_dev_new(&dev_cfg);
    assert(codec_handle);

    /* Specify the sample configurations and open the device */
    esp_codec_dev_sample_info_t sample_cfg = {
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .channel = 1,
        .channel_mask = 0x01,
        .sample_rate = I2S_SAMPLE_RATE,
    };
    if (esp_codec_dev_open(codec_handle, &sample_cfg) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Open codec device failed");
        return ESP_FAIL;
    }

    /* Set the initial volume and gain */
    if (esp_codec_dev_set_out_vol(codec_handle, VOLUME) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "set output volume failed");
        return ESP_FAIL;
    }

    if (esp_codec_dev_set_in_gain(codec_handle, MIC_GAIN) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "set input gain failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t i2s_driver_init(uint32_t sample_rate)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; // Auto clear the legacy data in the DMA buffer
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle));
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_MCK_IO,
            .bclk = I2S_BCK_IO,
            .ws = I2S_WS_IO,
            .dout = I2S_DO_IO,
            .din = I2S_DI_IO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = MCLK_MULTIPLE;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
    return ESP_OK;
}

// static void i2s_tone(void *args)
// {
//     int sample_rate = 16000;
//     int tone_1_hz = 330;
//     int tone_2_hz = 220;
//     int tono_actual = tone_1_hz;
//     int contador = 0;
//     int ciclo_cambio = 80; // ms
//     esp_err_t ret = ESP_OK;
//     size_t bytes_write = 0;
//     const int samples_per_cycle_1 = sample_rate / tone_1_hz;
//     const int samples_per_cycle_2 = sample_rate / tone_2_hz;
//     size_t buffer_size_1 = samples_per_cycle_1  * sizeof(int16_t); // stereo
//     size_t buffer_size_2 = samples_per_cycle_2  * sizeof(int16_t); // stereo
//     int16_t *audio_buffer_1 = malloc(buffer_size_1);
//     int16_t *audio_buffer_2 = malloc(buffer_size_2);
//     const int samples_per_cycle = sample_rate / tone_1_hz;
//     size_t buffer_size = samples_per_cycle * sizeof(int16_t); // MONO: solo 1 canal
//     int16_t *audio_buffer = malloc(buffer_size);
//     if (!audio_buffer) {
//         ESP_LOGE(TAG, "No hay memoria para buffer de audio");
//         return;
//     }
//     // Pre–generar una onda senoidal (un ciclo completo)
//     for (int i = 0; i < samples_per_cycle; i++) {
//         float angle = 2.0f * 3.14 * i / samples_per_cycle;
//         int16_t sample = (int16_t)(sinf(angle) * 28000); // ~-3 dBFS
//         audio_buffer[i] = sample; // SOLO un canal
//     }
//     uint8_t *data_ptr = (uint8_t *)audio_buffer_1;
//     while (1) {        
//         ret = i2s_channel_write(tx_handle, data_ptr, buffer_size, &bytes_write, portMAX_DELAY);
//         if (ret != ESP_OK) {
//             ESP_LOGE(TAG, "[music] i2s write failed, %s", err_reason[ret == ESP_ERR_TIMEOUT]);
//             abort();
//         }
//         if (bytes_write > 0) {
//             //ESP_LOGI(TAG, "[music] i2s music played, %d bytes are written.", bytes_write);
//         } else {
//             ESP_LOGE(TAG, "[music] i2s music play failed.");
//             abort();
//         }
//         vTaskDelay(pdMS_TO_TICKS(1));
//         contador++;
//         if (contador >= ciclo_cambio) {
//             if (tono_actual == tone_1_hz) {
//                 tono_actual = tone_2_hz;
//                 data_ptr = (uint8_t *)audio_buffer_2;
//                 buffer_size = buffer_size_2;
//             } else {
//                 tono_actual = tone_1_hz;
//                 data_ptr = (uint8_t *)audio_buffer_1;
//                 buffer_size = buffer_size_1;
//             }
//             contador = 0;
//         }
//     }
//     vTaskDelete(NULL);
// }

// static void i2s_echo(void *args)
// {
//     ESP_LOGI(TAG, "[echo] Start i2s echo task");
//     int16_t *mic_data = malloc(EXAMPLE_RECV_BUF_SIZE);
//     if (!mic_data) {
//         ESP_LOGE(TAG, "[echo] No memory for read data buffer");
//         abort();
//     }
//     esp_err_t ret = ESP_OK;
//     size_t bytes_read = 0;
//     size_t bytes_write = 0;
//     ESP_LOGI(TAG, "[echo] Echo start");
//     gpio_set_level(AMP_CTRL, 0);
//     ESP_ERROR_CHECK(gpio_set_level(SW1, 1));
//     ESP_ERROR_CHECK(gpio_set_level(SW2, 1));
//     while (1) {
//         ret = i2s_channel_read(rx_handle, mic_data, EXAMPLE_RECV_BUF_SIZE, &bytes_read, 1000);
//         if (ret != ESP_OK) {
//             ESP_LOGE(TAG, "[echo] i2s read failed, %d", (int)ret);
//             abort();
//         }
//         ret = i2s_channel_write(tx_handle, mic_data, EXAMPLE_RECV_BUF_SIZE, &bytes_write, 1000);
//         if (ret != ESP_OK) {
//             ESP_LOGE(TAG, "[echo] i2s write failed, %d", (int)ret);
//             abort();
//         }
//         if (bytes_read != bytes_write) {
//             ESP_LOGW(TAG, "[echo] %d bytes read but only %d bytes are written", bytes_read, bytes_write);
//         }        
//     }
//     vTaskDelete(NULL);
// }

static esp_err_t i2s_save_data(uint16_t length) {
    ESP_LOGI(TAG, "i2s_save_data");
    // length = 1;
    if (length > MAX_LENGTH_SECONDS) {
        ESP_LOGE(TAG, "Length exceeds maximum allowed duration");
        return ESP_ERR_INVALID_ARG;
    }

    readed_samples = 0;
    size_t chunk_size = 120;
    uint32_t nsamples = length * I2S_SAMPLE_RATE;
    size_t bytes_readed;

    // ESP_LOGI(TAG, "%d seconds to read, Total %d samples at %d Hz",
    //      length, nsamples, I2S_SAMPLE_RATE);

    // ESP_LOGI(TAG, "Alloc file_from_mic: %p (%d bytes)", file_from_mic,
    //      MAX_LENGTH_SECONDS * I2S_SAMPLE_RATE * sizeof(uint16_t));

    // if (!file_from_mic) {
    //     ESP_LOGE(TAG, "PSRAM allocation failed!");
    //     return ESP_ERR_NO_MEM;
    // }

    // ESP_LOGI(TAG, "Heap internal free: %d | PSRAM free: %d",
    //         heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
    //         heap_caps_get_free_size(MALLOC_CAP_SPIRAM));


    uint32_t total_readed_samples = 0;
    size_t to_read;

    // char rx_buffer[chunk_size * sizeof(uint16_t)];
    // Buffer temporal en memoria interna DMA (CRÍTICO)

    // uint16_t *rx_buffer = (uint16_t *)heap_caps_malloc(
    //     chunk_size * sizeof(uint16_t), 
    //     MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL
    // );

    uint16_t *rx_buffer = (uint16_t *)heap_caps_aligned_alloc(
        32,
        chunk_size * sizeof(uint16_t), 
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL
    );

    if (!rx_buffer) {
        ESP_LOGE(TAG, "Failed to allocate DMA buffer");
        return ESP_ERR_NO_MEM;
    }
    memset(rx_buffer, 0, sizeof(rx_buffer)*sizeof(uint16_t));
    memset(file_from_mic, 0, sizeof(file_from_mic) * sizeof(uint16_t));

    if(length == MAX_LENGTH_SECONDS) {
        ESP_LOGI(TAG, "infinite read");
    }

    uint8_t idx = 0;
    while ((total_readed_samples < nsamples) || (length == MAX_LENGTH_SECONDS)){
        to_read = (chunk_size < (nsamples - total_readed_samples)) ? chunk_size : nsamples - total_readed_samples;

        if (to_read == 0){
            i2s_channel_read(rx_handle, rx_buffer, chunk_size * sizeof(uint16_t), &bytes_readed, portMAX_DELAY);

            if(bytes_readed != chunk_size * sizeof(uint16_t)){
                ESP_LOGE(TAG, "Error reading from I2S: %d bytes readed", bytes_readed);
                free(rx_buffer);
                readed_samples = 0;
                return ESP_FAIL;
            }
            
            // memcpy(&file_from_mic[idx++ * bytes_readed], rx_buffer, bytes_readed);
            ESP_LOGI(TAG, "loop infinite %d", bytes_readed);

        } else {
            i2s_channel_read(rx_handle, rx_buffer, to_read * sizeof(uint16_t), &bytes_readed, portMAX_DELAY);
            
            if(bytes_readed != to_read * sizeof(uint16_t)){
                ESP_LOGE(TAG, "Error reading from I2S: %d bytes readed", bytes_readed);
                free(rx_buffer);
                readed_samples = 0;
                return ESP_FAIL;
            }

            memcpy(&file_from_mic[total_readed_samples], rx_buffer, bytes_readed);

            total_readed_samples += bytes_readed / sizeof(uint16_t);
        }
    }

    free(rx_buffer);
    readed_samples = total_readed_samples;
    ESP_LOGI(TAG, "%d samples readed", (int)readed_samples);
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

static void task(void *args){
    while(1){
        vTaskDelay(pdMS_TO_TICKS(3000));
        // ESP_LOGI(TAG, "recording loop: %d", record);
        if (record == true){
            record = false;
            recording = true;
            #ifdef AUDIO_DEBUG
                // Signal from microphone
                // Antenna as output
                gpio_set_level(SW1, 1);
                gpio_set_level(SW2, 1);
            #else
                // Signal from antenna
                // Antenna as input
                gpio_set_level(SW1, 0);
                gpio_set_level(SW2, 0);
            #endif

            gpio_set_level(AMP_CTRL, 0); // Amplifier off
            vTaskDelay(pdMS_TO_TICKS(10));

            ESP_LOGI(TAG, "Recording started");

            if (i2s_save_data(aquisition_length_seconds) != ESP_OK) {
                ESP_LOGE(TAG, "Error saving data from I2S");
            } else {
                ESP_LOGI(TAG, "Recording finished");
            }
            recording = false;
        }
    }
}

esp_err_t init_i2s_es8311(uint32_t sample_rate)
{
    /* Initialize i2s peripheral */
    if (i2s_driver_init(sample_rate) != ESP_OK) {
        ESP_LOGE(TAG, "i2s driver init failed");
        return ESP_FAIL;
    } else {
        // ESP_LOGI(TAG, "i2s driver init success");
    }

    /* Initialize i2c peripheral and config es8311 codec by i2c */
    if (es8311_codec_init() != ESP_OK) {
        ESP_LOGE(TAG, "es8311 codec init failed");
        return ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "es8311 codec init success");
    }

    // xTaskCreate(i2s_tone, "i2s_tone", 4*1024, NULL, 5, NULL);
    // xTaskCreate(i2s_echo, "i2s_echo", 4*1024, NULL, 5, NULL);
    xTaskCreate(task, "task", 8*1024, NULL, 5, NULL);
    
    // i2s_save_data(10);
    return ESP_OK;
}
