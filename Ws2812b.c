/* self */
#include "Ws2812b.h"

/* stdlib */
#include <stdint.h>

/* max32625 + cordio */
#include <max32655.h>
#include <nvic_table.h>
#include <tmr.h>
#include <wsf_trace.h>

#define WS2812B_BITS_PER_PIXEL (24 * 3)

#define WS2812B_LED_IN_GPIO MXC_GPIO1
#define WS2812B_LED_IN_PIN MXC_GPIO_PIN_6

#define WS2812B_LED_OUT_GPIO MXC_GPIO1
#define WS2812B_LED_OUT_PIN MXC_GPIO_PIN_7

static uint8_t gpioData[WS2812B_LEDS_MAX * WS2812B_BITS_PER_PIXEL + 1];

void WS2812B_init() {
    int status;

    mxc_gpio_cfg_t ledIn;
    ledIn.port = WS2812B_LED_IN_GPIO;
    ledIn.mask = WS2812B_LED_IN_PIN;
    ledIn.func = MXC_GPIO_FUNC_OUT;
    ledIn.pad = MXC_GPIO_PAD_NONE;
    ledIn.vssel = MXC_GPIO_VSSEL_VDDIOH;

    status = MXC_GPIO_Config(&ledIn);
    if (status) {
        APP_TRACE_ERR1("Unable to initialize WS2812V LED IN GPIO. MXC_GPIO_Config failed with status code %d", status);
    }

    mxc_gpio_cfg_t ledOut;
    ledOut.port = WS2812B_LED_OUT_GPIO;
    ledOut.mask = WS2812B_LED_OUT_PIN;
    ledOut.func = MXC_GPIO_FUNC_IN;
    ledOut.pad = MXC_GPIO_PAD_NONE;
    ledOut.vssel = MXC_GPIO_VSSEL_VDDIOH;

    status = MXC_GPIO_Config(&ledOut);
    if (status) {
        APP_TRACE_ERR1("Unable to initialize WS2812V LED OUT GPIO. MXC_GPIO_Config failed with status code %d", status);
    }

    for (int i = 0; i < WS2812B_LEDS_MAX; i++) {
        for (int j = 0; j < 24; j++) {
            gpioData[i * WS2812B_BITS_PER_PIXEL + j * 3 + 0] = 1;
            gpioData[i * WS2812B_BITS_PER_PIXEL + j * 3 + 1] = 0;
            gpioData[i * WS2812B_BITS_PER_PIXEL + j * 3 + 2] = 0;
        }
    }
}

void WS2812B_SetColor(int index, uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < 8; i++) {
        gpioData[index * WS2812B_BITS_PER_PIXEL + i * 3 + 0 * 3 + 1] = !!(g & (1 << (7 - i)));
    }
    for (int i = 0; i < 8; i++) {
        gpioData[index * WS2812B_BITS_PER_PIXEL + i * 3 + 8 * 3 + 1] = !!(r & (1 << (7 - i)));
    }
    for (int i = 0; i < 8; i++) {
        gpioData[index * WS2812B_BITS_PER_PIXEL + i * 3 + 16 * 3 + 1] = !!(b & (1 << (7 - i)));
    }
}

void WS2812B_Transmit() {
    __disable_irq();

    MXC_GPIO_OutClr(WS2812B_LED_IN_GPIO, WS2812B_LED_IN_PIN);

    for (volatile int i = 0; i < 1000; i++) {
        __NOP();
    }

    for (int i = 0; i < sizeof(gpioData); i++) {
        MXC_GPIO_OutPut(WS2812B_LED_IN_GPIO, WS2812B_LED_IN_PIN, WS2812B_LED_IN_PIN * gpioData[i]);
        __NOP();
        __NOP();
        __NOP();
    }

    MXC_GPIO_OutSet(WS2812B_LED_IN_GPIO, WS2812B_LED_IN_PIN);

    __enable_irq();
}