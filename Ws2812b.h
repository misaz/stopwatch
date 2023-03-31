#ifndef WS2812B_H
#define WS2812B_H

#define WS2812B_LEDS_MAX 1

#include <stdint.h>

void WS2812B_init();
void WS2812B_SetColor(int index, uint8_t r, uint8_t g, uint8_t b);
void WS2812B_Transmit();

#endif