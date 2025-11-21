#ifndef LEDS
#define LEDS

#define LED_STRIP_RMT_RES_HZ  (10 * 1000 * 1000)

#define LED_STRIP_USE_DMA  0
#define LED_STRIP_MEMORY_BLOCK_WORDS 0
#define LEDS_UPDATE_INTERVAL   100
#define BRIGHTNESS             28
static const uint8_t COLOR_BLUE[3]   = { 0, 0, BRIGHTNESS };
static const uint8_t COLOR_YELLOW[3] = { BRIGHTNESS, BRIGHTNESS, 0 };

static void leds_fill_alternating_task(void *arg);

#endif  // LEDS