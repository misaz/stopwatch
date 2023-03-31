/* self */
#include "GUI.h"

/* project */
#include "BLE.h"
#include "Button.h"
#include "Display.h"
#include "Time.h"
#include "Ws2812b.h"

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

#define GUI_MENU_POS 0
#define GUI_STATUS_POS (sizeof(menuIcon) + 2)
#define GUI_BAT_POS (DISPLAY_WIDTH - sizeof(batIcon))
#define GUI_BLE_POS (DISPLAY_WIDTH - sizeof(batIcon) - sizeof(bleIcon) - 4)

#define GUI_LED_BRIGHTNESS 5

#define LAPS_MAX 256

static void GUI_RenderScreen();
static void GUI_StartClick(uint32_t pressTime);
static void GUI_StopClick(uint32_t pressTime);
static void GUI_LapClick(uint32_t pressTime);
static void GUI_StandbyClick(uint32_t pressTime);
static void GUI_MenuClick(uint32_t pressTime);
static void GUI_SetReadyModeButtons();
static void GUI_SetRunModeButtons();

static int isBleConnected = 0;
static int isBleAdvertisign = 0;
static char *statusString = "ready";
static char *buttonText[BUTTON_COUNT];
static void (*buttonHandlers[BUTTON_COUNT])(uint32_t pressTime);

static uint32_t stopwatchStartTime = 0;
static uint32_t stopwatchStopTime = 0;
static int isStopwatchRunning = 0;
static uint32_t totalTime = 0;
static uint32_t lapOffsets[LAPS_MAX];
static int lapCount = 0;
static char lapNoStatusString[16];

static uint32_t animationCounter = 0;
static int ledAnimation = 0;

static void GUI_TimerHandler(wsfEventMask_t event, wsfMsgHdr_t *pMsg) {
    if (pMsg == NULL || pMsg->event != GUI_TIMER_TICK_EVENT) {
        return;
    }

    animationCounter++;

    int isAnimationRenderNeeded = !isBleConnected && isBleAdvertisign && (animationCounter % 5 == 0);

    if (isStopwatchRunning || isAnimationRenderNeeded) {
        GUI_RenderScreen();
    }

    if (isStopwatchRunning) {
        BLE_SetCurrentTime(TIME_TIMER->cnt - stopwatchStartTime);
    }

    if (isStopwatchRunning) {
        WS2812B_SetColor(0, 0, GUI_LED_BRIGHTNESS, 0);
    } else if (isBleAdvertisign && !isBleConnected) {
        if (animationCounter % 10 < 5) {
            WS2812B_SetColor(0, 0, 0, GUI_LED_BRIGHTNESS);
        } else {
            WS2812B_SetColor(0, GUI_LED_BRIGHTNESS, GUI_LED_BRIGHTNESS, 0);
        }
    } else {
        WS2812B_SetColor(0, 0, 0, 0);
    }

    WS2812B_Transmit();

    WsfTimerStartMs(&guiTimer, 50);
}

void GUI_Init() {
    buttonText[BUTTON_BTNM_NO] = "";
    buttonHandlers[BUTTON_BTNM_NO] = GUI_MenuClick;

    GUI_SetReadyModeButtons();

    guiTimerHandler = WsfOsSetNextHandler(GUI_TimerHandler);

    guiTimer.handlerId = guiTimerHandler;
    guiTimer.msg.event = GUI_TIMER_TICK_EVENT;
    guiTimer.msg.param = 0;
    guiTimer.msg.status = 0;
    WsfTimerStartMs(&guiTimer, 500);

    GUI_RenderScreen();
}

void GUI_HandleButtonPress(int buttonNumber, uint32_t pressTime) {
    if (buttonHandlers[buttonNumber] != NULL) {
        buttonHandlers[buttonNumber](pressTime);
    }
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
        if (animationCounter % 10 < 5) {
            for (int i = 0; i < sizeof(bleIcon); i++) {
                Display_SetPixelBuffer(i + GUI_BLE_POS, 0, bleIcon[i]);
            }
        }
    }

    for (int i = 0; i < sizeof(batIcon); i++) {
        Display_SetPixelBuffer(i + GUI_BAT_POS, 0, batIcon[i]);
    }

    int statusStringLen = Display_GetStringLength(statusString);

    int offset = GUI_STATUS_POS + (GUI_BLE_POS - GUI_STATUS_POS) / 2 - statusStringLen / 2;

    Display_PrintString(offset, 0, statusString);

    for (int i = GUI_STATUS_POS; i < GUI_BLE_POS; i++) {
        Display_ShiftLeftPixelBuffer(i, 0, 1);
    }
}

static void GUI_RenderButtons() {
    int buttonSize = (DISPLAY_WIDTH - 2) / 2;

    int buttonOrderRemap[2] = {BUTTON_BTNL_NO, BUTTON_BTNR_NO};

    for (int i = 0; i < 2; i++) {
        int textLen = Display_GetStringLength(buttonText[buttonOrderRemap[i]]) - 1;

        int offset = i * (DISPLAY_WIDTH / 2) + buttonSize / 2 - textLen / 2;

        Display_PrintString(offset, DISPLAY_LINES - 1, buttonText[buttonOrderRemap[i]]);
    }
    for (int i = 0; i < DISPLAY_WIDTH; i++) {
        Display_ShiftLeftPixelBuffer(i, DISPLAY_LINES - 1, 1);
    }

    Display_SetPixelBuffer(buttonSize, DISPLAY_LINES - 1, 0xFF);
    Display_SetPixelBuffer(buttonSize + 1, DISPLAY_LINES - 1, 0xFF);

    for (int i = 0; i < DISPLAY_WIDTH; i++) {
        Display_InvertPixelBuffer(i, DISPLAY_LINES - 1);
    }
}

static void GUI_PrintTime() {
    char *str;

    uint32_t timeToRender;

    if (!isStopwatchRunning) {
        timeToRender = totalTime;
    } else {
        uint32_t now = TIME_TIMER->cnt;
        timeToRender = now - stopwatchStartTime;
    }

    int sec_total = timeToRender / TIME_TICK_PER_SEC;

    int hours = sec_total / 3600;
    sec_total %= 3600;

    int minutes = sec_total / 60;
    sec_total %= 60;

    int sec = sec_total;

    float ticksPerMsec = (float)TIME_TICK_PER_SEC / 1000.0;
    int msec = (int)((float)(timeToRender % TIME_TICK_PER_SEC) / ticksPerMsec);

    char buff[32];
    snprintf(buff, sizeof(buff), "%02d:%02d:%02d.%03d", hours, minutes, sec, msec);

    str = buff;

    int len = Display_GetStringLength(str) - 1;
    int offset = DISPLAY_WIDTH / 2 - len / 2;

    Display_PrintString(offset, 2, str);
}

static void GUI_PrintLapTime(uint32_t time, char *timeBuffer, size_t timeBufferSize) {
    int sec_total = time / TIME_TICK_PER_SEC;

    int hours = sec_total / 3600;
    sec_total %= 3600;

    int minutes = sec_total / 60;
    sec_total %= 60;

    int sec = sec_total;

    float ticksPerMsec = (float)TIME_TICK_PER_SEC / 1000.0;
    int msec = (int)((float)(time % TIME_TICK_PER_SEC) / ticksPerMsec);

    if (hours == 0 && minutes == 0) {
        snprintf(timeBuffer, timeBufferSize, "%02d.%03d", sec, msec);
    } else if (hours == 0) {
        snprintf(timeBuffer, timeBufferSize, "%02d:%02d.%03d", minutes, sec, msec);
    } else {
        snprintf(timeBuffer, timeBufferSize, "%02d:%02d:%02d", hours, minutes, sec);
    }
}

static void GUI_PrintLaps() {
    char time[32];
    char line[32];

    uint32_t lapTime;

    if (lapCount == 1) {
        lapTime = lapOffsets[0] - stopwatchStartTime;
    } else {
        lapTime = lapOffsets[lapCount - 1] - lapOffsets[lapCount - 2];
    }

    GUI_PrintLapTime(lapTime, time, sizeof(time));
    snprintf(line, sizeof(line), "L%d: %s", lapCount, time);

    Display_PrintString(0, 3, line);

    if (isStopwatchRunning) {
        lapTime = TIME_TIMER->cnt - lapOffsets[lapCount - 1];
    } else {
        lapTime = stopwatchStopTime - lapOffsets[lapCount - 1];
    }

    GUI_PrintLapTime(lapTime, time, sizeof(time));
    snprintf(line, sizeof(line), "L%d: %s", lapCount + 1, time);

    Display_PrintString(0, 4, line);
}

static void GUI_StartClick(uint32_t pressTime) {
    stopwatchStartTime = pressTime;
    isStopwatchRunning = 1;
    lapCount = 0;

    BLE_LapCountChanged(lapCount);
    BLE_SetStatus(0x01);

    GUI_SetRunModeButtons();
    GUI_RenderScreen();
}

static void GUI_StopClick(uint32_t pressTime) {
    totalTime = pressTime - stopwatchStartTime;
    stopwatchStopTime = pressTime;
    isStopwatchRunning = 0;

    BLE_SetCurrentTime(totalTime);
    BLE_SetStatus(0x00);

    GUI_SetReadyModeButtons();
    GUI_RenderScreen();
}

static void GUI_LapClick(uint32_t pressTime) {
    if (lapCount < LAPS_MAX) {
        lapOffsets[lapCount++] = pressTime;
    }

    BLE_LapCountChanged(lapCount);

    GUI_SetRunModeButtons();
    GUI_RenderScreen();
}

static void GUI_StandbyClick(uint32_t pressTime) {
}

static void GUI_MenuClick(uint32_t pressTime) {
}

static void GUI_SetReadyModeButtons() {
    statusString = "ready";

    buttonText[BUTTON_BTNL_NO] = "start";
    buttonHandlers[BUTTON_BTNL_NO] = GUI_StartClick;

    buttonText[BUTTON_BTNR_NO] = "";
    buttonHandlers[BUTTON_BTNR_NO] = NULL;
}

static void GUI_SetRunModeButtons() {
    if (lapCount == 0 || lapCount >= LAPS_MAX) {
        statusString = "run";
    } else if (lapCount < 99) {
        snprintf(lapNoStatusString, sizeof(lapNoStatusString), "lap %d", lapCount + 1);
        statusString = lapNoStatusString;
    } else {
        snprintf(lapNoStatusString, sizeof(lapNoStatusString), "lp %d", lapCount + 1);
        statusString = lapNoStatusString;
    }

    buttonText[BUTTON_BTNL_NO] = "stop";
    buttonHandlers[BUTTON_BTNL_NO] = GUI_StopClick;

    if (lapCount < LAPS_MAX) {
        buttonText[BUTTON_BTNR_NO] = "lap";
        buttonHandlers[BUTTON_BTNR_NO] = GUI_LapClick;
    } else {
        buttonText[BUTTON_BTNR_NO] = "";
        buttonHandlers[BUTTON_BTNR_NO] = NULL;
    }
}

void GUI_SetBleAdvertisignStatus(int isAdvertisign) {
    isBleAdvertisign = isAdvertisign;
    GUI_RenderScreen();
}

void GUI_SetBleConnectionStatus(int isConnected) {
    isBleConnected = isConnected;
    GUI_RenderScreen();
}

static void GUI_RenderScreen() {
    Display_Clear();
    GUI_RenderStatusBar();
    GUI_PrintTime();
    if (lapCount > 0) {
        GUI_PrintLaps();
    }
    GUI_RenderButtons();
    Display_Show();
}

uint32_t GUI_GetLapTime(uint8_t lapNumber) {
    if (lapNumber >= lapCount) {
        return 0;
    }

    if (lapNumber == 0) {
        return lapOffsets[0] - stopwatchStartTime;
    } else {
        return lapOffsets[lapNumber] - lapOffsets[lapNumber - 1];
    }
}