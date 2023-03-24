/* self */
#include "GUI.h"

/* project */
#include "Button.h"
#include "Display.h"

/* sdtlib */
#include <stdio.h>
#include <string.h>

/* max32655 + mbed + cordio */
#include <wsf_os.h>
#include <wsf_timer.h>

#define GUI_TIMER_TICK_EVENT 0xfb

static wsfTimer_t guiTimer;
static wsfHandlerId_t guiTimerHandler;

static int i = 0;

static int buttonPresses[BUTTON_COUNT];

static void GUI_TimerHandler(wsfEventMask_t event, wsfMsgHdr_t *pMsg) {
    if (pMsg == NULL || pMsg->event != GUI_TIMER_TICK_EVENT) {
        return;
    }

    i = (i + 1) % 6;

    WsfTimerStartMs(&guiTimer, 330);
}

void GUI_Init() {
    guiTimerHandler = WsfOsSetNextHandler(GUI_TimerHandler);

    guiTimer.handlerId = guiTimerHandler;
    guiTimer.msg.event = GUI_TIMER_TICK_EVENT;
    guiTimer.msg.param = 0;
    guiTimer.msg.status = 0;
    WsfTimerStartMs(&guiTimer, 330);
}

void GUI_HandleButtonPress(int buttonNumber, uint32_t pressTime) {
    char buffer[32];

    buttonPresses[buttonNumber]++;

    Display_Clear();

    snprintf(buffer, sizeof(buffer), "RIGHT: %d", buttonPresses[0]);
    Display_PrintString(0, 1, buffer);

    snprintf(buffer, sizeof(buffer), "LEFT: %d", buttonPresses[1]);
    Display_PrintString(0, 2, buffer);

    snprintf(buffer, sizeof(buffer), "MENU: %d", buttonPresses[2]);
    Display_PrintString(0, 3, buffer);

    Display_Show();
}