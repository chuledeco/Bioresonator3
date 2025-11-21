#ifndef I2S_ES8311_H_
#define I2S_ES8311_H_

#include "driver/i2s_std.h"

#define NUM_BUFFERS 5

#define MAX_LENGTH_SECONDS      10          // Duración máxima de audio en segundos
#define EXAMPLE_RECV_BUF_SIZE   (2*240)     // (10s at 44.1kHz, mono)
#define I2S_SAMPLE_RATE         (44100)     // 44.1kHz
#define MCLK_MULTIPLE           (256)       // If not using 24-bit data width, 256 should be enough

#define VOLUME          50      //CONFIG_EXAMPLE_VOICE_VOLUME
#define MIC_GAIN        25      // en dB //CONFIG_EXAMPLE_MIC_GAIN

#define I2C_NUM         (0)     // I2C port number
#define I2S_NUM         (0)     // I2S port number

extern i2s_chan_handle_t tx_handle;
extern i2s_chan_handle_t rx_handle;

extern uint16_t *file_from_web;
extern uint16_t *file_from_mic;

extern bool record;
extern bool play;

extern uint8_t aquisition_length_seconds;
extern uint32_t readed_samples;

typedef struct {
    int16_t *data;
    size_t size;
    bool ready;
    uint8_t index;
} audio_buffer_t;

extern audio_buffer_t audio_buffers[NUM_BUFFERS];
extern uint8_t audio_index;

esp_err_t init_i2s_es8311(uint32_t sample_rate);

esp_err_t es8311_set_volume(int volume);

esp_err_t reserve_memory_for_audio_files();

#endif /* I2S_ES8311_H_ */