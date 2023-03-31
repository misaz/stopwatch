/* self */
#include "Ws2812b.h"

/* stdlib */
#include <stdint.h>

/* max32625 + cordio */
#include <max32655.h>
#include <nvic_table.h>
#include <tmr.h>
#include <wsf_trace.h>

#define WS2812B_RESET_CYCLES (0)
#define WS2812B_BITS_PER_PIXEL (24 * 3)

#define WS2812B_LED_IN_GPIO MXC_GPIO1
#define WS2812B_LED_IN_PIN MXC_GPIO_PIN_6

#define WS2812B_LED_OUT_GPIO MXC_GPIO1
#define WS2812B_LED_OUT_PIN MXC_GPIO_PIN_7

#define WS2812B_TIMER MXC_TMR2
#define WS2812B_TIMER_IRQn TMR2_IRQn

static uint8_t gpioData[WS2812B_RESET_CYCLES + WS2812B_LEDS_MAX * WS2812B_BITS_PER_PIXEL + 1];
static uint8_t* gpioDataEnd = gpioData + sizeof(gpioData);
static uint8_t* gpioDataPos = gpioData;
static uint8_t* pixelData = gpioData + WS2812B_RESET_CYCLES;

void WS2812B_TimerInterruptHandler() {
    MXC_TMR_ClearFlags(WS2812B_TIMER);

    if (gpioDataPos < gpioDataEnd) {
        MXC_GPIO_OutPut(WS2812B_LED_IN_GPIO, WS2812B_LED_IN_PIN, WS2812B_LED_IN_PIN * *gpioDataPos++);
    } else {
        MXC_TMR_Stop(WS2812B_TIMER);
    }
}

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

    for (int i = 0; i < WS2812B_RESET_CYCLES; i++) {
        gpioData[i] = 0;
    }
    gpioData[sizeof(gpioData) - 1] = 1;

    for (int i = 0; i < WS2812B_LEDS_MAX; i++) {
        for (int j = 0; j < 24; j++) {
            gpioData[i * WS2812B_BITS_PER_PIXEL + j * 3 + 0] = 1;
            gpioData[i * WS2812B_BITS_PER_PIXEL + j * 3 + 1] = 0;
            gpioData[i * WS2812B_BITS_PER_PIXEL + j * 3 + 2] = 0;
        }
    }

    mxc_tmr_cfg_t tmr;
    tmr.mode = TMR_MODE_CONTINUOUS;
    tmr.clock = MXC_TMR_32M_CLK;
    tmr.bitMode = TMR_BIT_MODE_32;
    tmr.cmp_cnt = 10;
    tmr.pol = 0;
    tmr.pres = TMR_PRES_1;

    status = MXC_TMR_Init(WS2812B_TIMER, &tmr, FALSE);
    if (status) {
        APP_TRACE_ERR1("Unable to initialize WS2812V LED timer. MXC_TMR_Init failed with status code %d", status);
        return;
    }

    MXC_TMR_EnableInt(WS2812B_TIMER);

    MXC_NVIC_SetVector(WS2812B_TIMER_IRQn, WS2812B_TimerInterruptHandler);
    NVIC_SetPriority(WS2812B_TIMER_IRQn, 0);
    NVIC_ClearPendingIRQ(WS2812B_TIMER_IRQn);
    NVIC_EnableIRQ(WS2812B_TIMER_IRQn);
}

void WS2812B_SetColor(int index, uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < 8; i++) {
        pixelData[index * WS2812B_BITS_PER_PIXEL + i * 3 + 0 * 3 + 1] = !!(g & (1 << (7 - i)));
    }
    for (int i = 0; i < 8; i++) {
        pixelData[index * WS2812B_BITS_PER_PIXEL + i * 3 + 8 * 3 + 1] = !!(r & (1 << (7 - i)));
    }
    for (int i = 0; i < 8; i++) {
        pixelData[index * WS2812B_BITS_PER_PIXEL + i * 3 + 16 * 3 + 1] = !!(b & (1 << (7 - i)));
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

    // gpioDataPos = gpioData;
    // MXC_TMR_Start(WS2812B_TIMER);
}