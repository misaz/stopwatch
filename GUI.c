/* project */
#include "GUI.h"

#include "Display.h"

/* sdtlib */
#include <string.h>

/* max32655 + mbed + cordio */
#include <wsf_os.h>
#include <wsf_timer.h>

#define GUI_TIMER_TICK_EVENT 0xfb

static wsfTimer_t guiTimer;
static wsfHandlerId_t guiTimerHandler;

static int i = 0;

static void GUI_TimerHandler(wsfEventMask_t event, wsfMsgHdr_t *pMsg) {
    if (pMsg == NULL || pMsg->event != GUI_TIMER_TICK_EVENT) {
        return;
    }

    Display_Clear();
    Display_PrintString(10, i, "ahoj");
    Display_Show();

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