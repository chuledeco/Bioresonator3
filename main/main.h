#ifndef MAIN_H_
#define MAIN_H_

#include <stdbool.h>

#define AUDIO_DEBUG  // Use MIC and speaker instead of antenna

// GPIO definitions

#define SW1         2
#define SW2         1
#define AMP_CTRL    21

#define LED_STRIP_GPIO_PIN  48

#define PA_CTRL_IO      GPIO_NUM_2 //CONFIG_PA_CTRL_IO

#define I2C_SCL_IO      GPIO_NUM_9 //CONFIG_EXAMPLE_I2C_SCL_IO
#define I2C_SDA_IO      GPIO_NUM_8 //CONFIG_EXAMPLE_I2C_SDA_IO

#define I2S_MCK_IO      GPIO_NUM_0 //CONFIG_EXAMPLE_I2S_MCLK_IO
#define I2S_BCK_IO      GPIO_NUM_18 //CONFIG_EXAMPLE_I2S_BCLK_IO
#define I2S_WS_IO       GPIO_NUM_16 //CONFIG_EXAMPLE_I2S_WS_IO
#define I2S_DO_IO       GPIO_NUM_15 //CONFIG_EXAMPLE_I2S_DOUT_IO
#define I2S_DI_IO       GPIO_NUM_17 //GPIO_NUM_NC //CONFIG_EXAMPLE_I2S_DIN_IO
 
#define LED_STRIP_LED_COUNT 25

#define SSID            "BioresonatorBC"
#define PASSWORD        "BioresonatorBC"

extern bool recording;
extern bool playing;
extern bool sending;
extern bool receiving;

void print_memory_status(void);

#endif /* MAIN_H_ */