/* self */
#include "Button.h"

/* project */
#include "GUI.h"
#include "Time.h"

/* max32625 + cordio */
#include <gpio.h>
#include <max32655.h>
#include <nvic_table.h>
#include <wsf_timer.h>
#include <wsf_trace.h>

static wsfTimer_t timer;
static wsfHandlerId_t timerHandler;

static volatile int isEvent = 0;
static volatile uint32_t lastEventTime;
static volatile uint32_t lastEventMask;

static int prevButtonState[BUTTON_COUNT];
static uint32_t buttonMask[BUTTON_COUNT] = {BUTTON_BTNR_PIN, BUTTON_BTNL_PIN, BUTTON_BTNM_PIN};
static uint32_t firstTransitionChange[BUTTON_COUNT];

#define BUTTON_DEBOUNCE_TIME_MS 30

// 30 ms @ 32 MHz
#define BUTTON_DEBOUNCE_TIME_TICK (BUTTON_DEBOUNCE_TIME_MS * TIME_TICK_PER_MSEC)

void Button_GpioInterruptHandler() {
    isEvent = 1;
    lastEventTime = TIME_TIMER->cnt;
    lastEventMask = MXC_GPIO_GetFlags(BUTTON_GPIO);
    MXC_GPIO_ClearFlags(BUTTON_GPIO, lastEventMask);
}

void Button_TimerHandler(wsfEventMask_t event, wsfMsgHdr_t *pMsg) {
    if (pMsg == NULL || pMsg->event != BUTTON_TIMER_TICK_EVENT) {
        return;
    }

    NVIC_DisableIRQ(BUTTON_IRQn);
    int isEventLocal = isEvent;
    uint32_t lastEventTimeLocal = lastEventTime;
    uint32_t lastEventMaskLocal = lastEventMask;
    isEvent = 0;
    NVIC_EnableIRQ(BUTTON_IRQn);

    for (size_t i = 0; i < BUTTON_COUNT; i++) {
        if (isEventLocal) {
            if (lastEventMaskLocal & buttonMask[i]) {
                uint32_t timeSinceLastEvent = lastEventTimeLocal - firstTransitionChange[i];

                if (timeSinceLastEvent > BUTTON_DEBOUNCE_TIME_TICK) {
                    firstTransitionChange[i] = lastEventTimeLocal;
                }
            }
        }

        int currentBtnState = !!MXC_GPIO_InGet(BUTTON_GPIO, buttonMask[i]);

        /* && (now - firstTransitionChange[i]) < BUTTON_DEBOUNCE_TIME_MS */
        if (prevButtonState[i] == 1 && currentBtnState == 0) {
            GUI_HandleButtonPress(i, firstTransitionChange[i]);
        }

        prevButtonState[i] = currentBtnState;
    }

    WsfTimerStartMs(&timer, BUTTON_DEBOUNCE_TIME_MS);
}

void Button_Init() {
    int status;

    mxc_gpio_cfg_t btn;
    btn.port = BUTTON_GPIO;
    btn.mask = BUTTON_BTNR_PIN | BUTTON_BTNL_PIN | BUTTON_BTNM_PIN;
    btn.func = MXC_GPIO_FUNC_IN;
    btn.pad = MXC_GPIO_PAD_NONE;
    btn.vssel = MXC_GPIO_VSSEL_VDDIOH;

    status = MXC_GPIO_Config(&btn);
    if (status) {
        APP_TRACE_ERR1("MXC_GPIO_Config failed with code %d", status);
        return;
    }

    NVIC_ClearPendingIRQ(BUTTON_IRQn);
    NVIC_SetPriority(BUTTON_IRQn, 0);
    MXC_NVIC_SetVector(BUTTON_IRQn, Button_GpioInterruptHandler);

    status = MXC_GPIO_IntConfig(&btn, MXC_GPIO_INT_FALLING);
    if (status) {
        APP_TRACE_ERR1("MXC_GPIO_IntConfig failed with code %d", status);
        return;
    }

    MXC_GPIO_EnableInt(BUTTON_GPIO, btn.mask);

    timerHandler = WsfOsSetNextHandler(Button_TimerHandler);

    timer.handlerId = timerHandler;
    timer.msg.event = BUTTON_TIMER_TICK_EVENT;
    timer.msg.param = 0;
    timer.msg.status = 0;
    WsfTimerStartMs(&timer, 1);
}