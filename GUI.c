/* self */
#include "GUI.h"

/* project */
#include "Button.h"
#include "Display.h"
#include "Time.h"

/* sdtlib */
#include <stdio.h>
#include <string.h>

/* max32655 + mbed + cordio */
#include <wsf_os.h>
#include <wsf_timer.h>

#define GUI_TIMER_TICK_EVENT 0xfb

static wsfTimer_t guiTimer;
static wsfHandlerId_t guiTimerHandler;

static const uint8_t menuIcon[] = {
    0b01010100,
    0b01010100,
    0b01010100,
    0b01010100,
};

static const uint8_t bleIcon[] = {
    0b01000010,
    0b00100100,
    0b00011000,
    0b11111111,
    0b10011001,
    0b01011010,
    0b00100100,
};

static const uint8_t eyeIcon[] = {
    0b00011000,
    0b00100100,
    0b01000010,
    0b01011010,
    0b01011010,
    0b01000010,
    0b00100100,
    0b00011000,
};

static const uint8_t batIcon[] = {
    0b11111110,
    0b10000011,
    0b10000011,
    0b11111110,
};

static const uint8_t closeIcon[] = {
    0,
    0,
    0b00100010,
    0b00010100,
    0b00001000,
    0b00010100,
    0b00100010,
    0,
};

#define GUI_MENU_POS 2
#define GUI_BAT_POS 64 - 4
#define GUI_BLE_POS 64 - 14

static int isBleConnected = 0;
static int isBleAdvertisign = 0;
static char* statusString = "ready";

static void GUI_RenderScreen();

static void GUI_TimerHandler(wsfEventMask_t event, wsfMsgHdr_t* pMsg) {
    if (pMsg == NULL || pMsg->event != GUI_TIMER_TICK_EVENT) {
        return;
    }

    GUI_RenderScreen();

    WsfTimerStartMs(&guiTimer, 100);
}

void GUI_Init() {
    guiTimerHandler = WsfOsSetNextHandler(GUI_TimerHandler);

    guiTimer.handlerId = guiTimerHandler;
    guiTimer.msg.event = GUI_TIMER_TICK_EVENT;
    guiTimer.msg.param = 0;
    guiTimer.msg.status = 0;
    WsfTimerStartMs(&guiTimer, 500);

    GUI_RenderScreen();
}

void GUI_HandleButtonPress(int buttonNumber, uint32_t pressTime) {
    if (buttonNumber == 0) {
        isBleConnected = 1;
        isBleAdvertisign = 0;
    } else if (buttonNumber == 1) {
        isBleConnected = 0;
        isBleAdvertisign = 1;
    } else {
        isBleConnected = 0;
        isBleAdvertisign = 0;
    }

    GUI_RenderScreen();
}

static void GUI_RenderStatusBar() {
    for (int i = 0; i < sizeof(menuIcon); i++) {
        Display_SetPixelBuffer(i + GUI_MENU_POS, 0, menuIcon[i]);
    }

    if (isBleConnected) {
        for (int i = 0; i < sizeof(bleIcon); i++) {
            Display_SetPixelBuffer(i + GUI_BLE_POS, 0, bleIcon[i]);
        }
    } else if (isBleAdvertisign) {
        if ((TIME_TIMER->cnt / 16000000) % 2 == 0) {
            for (int i = 0; i < sizeof(bleIcon); i++) {
                Display_SetPixelBuffer(i + GUI_BLE_POS, 0, bleIcon[i]);
            }
        }
    }

    int leftWidth = sizeof(menuIcon) + GUI_MENU_POS;
    int rightWidth = sizeof(batIcon) + sizeof(bleIcon) + 2;

    for (int i = 0; i < sizeof(batIcon); i++) {
        Display_SetPixelBuffer(i + GUI_BAT_POS, 0, batIcon[i]);
    }

    int offset = 10 + (DISPLAY_WIDTH - leftWidth - rightWidth) / 2 - Display_GetStringLength(statusString) / 2;

    Display_PrintString(offset, 0, statusString);
}

static void GUI_RenderScreen() {
    Display_Clear();
    GUI_RenderStatusBar();
    Display_Show();
}