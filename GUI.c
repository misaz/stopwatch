/* self */
#include "GUI.h"

/* project */
#include "BLE.h"
#include "Button.h"
#include "Display.h"
#include "FuelGauge.h"
#include "Time.h"
#include "Ws2812b.h"

/* sdtlib */
#include <stdio.h>
#include <string.h>

/* max32655 + mbed + cordio */
#include <app_api.h>
#include <lp.h>
#include <nvic_table.h>
#include <wsf_os.h>
#include <wsf_timer.h>
#include <wsf_trace.h>
#include <wut.h>

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

static const uint8_t batIcon[] = {
    0b11111110,
    0b10000011,
    0b10000011,
    0b11111110,
};

static const uint8_t batIconChargeFill[] = {
    0b10000011,
    0b11000011,
    0b11100011,
    0b11110011,
    0b11111011,
    0b11111111,
};

static const uint8_t closeIcon[] = {
    0b00100010,
    0b00010100,
    0b00001000,
    0b00010100,
    0b00100010,
};

#define GUI_MENU_POS 0
#define GUI_STATUS_POS (sizeof(menuIcon) + 2)
#define GUI_BAT_POS (DISPLAY_WIDTH - sizeof(batIcon))
#define GUI_BLE_POS (DISPLAY_WIDTH - sizeof(batIcon) - sizeof(bleIcon) - 4)

#define GUI_LED_BRIGHTNESS 3

#define LAPS_MAX 256

static void GUI_RenderScreen();
static void GUI_StartClick(uint32_t pressTime);
static void GUI_StopClick(uint32_t pressTime);
static void GUI_LapClick(uint32_t pressTime);
static void GUI_MenuClick(uint32_t pressTime);
static void GUI_MenuLeftClick(uint32_t pressTime);
static void GUI_MenuRightClick(uint32_t pressTime);
static void GUI_SetReadyModeButtons();
static void GUI_SetRunModeButtons();
static void GUI_Menu_TurnOffClick();
static void GUI_Menu_BluetoothClick();

static int isBleConnected = 0;
static int isBleAdvertisign = 0;
static char *mainPageStatusString = "ready";
static char *mainPageButtonText[BUTTON_COUNT];
static void (*mainPageButtonHandlers[BUTTON_COUNT])(uint32_t pressTime);
static char *menuButtonText[BUTTON_COUNT];
static void (*menuButtonHandlers[BUTTON_COUNT])(uint32_t pressTime);

static uint32_t stopwatchStartTime = 0;
static uint32_t stopwatchStopTime = 0;
static int isStopwatchRunning = 0;
static uint32_t totalTime = 0;
static uint32_t lapOffsets[LAPS_MAX];
static int lapCount = 0;
static char lapNomainPageStatusString[16];

static uint32_t animationCounter = 0;

static char batteryLevelMenuLabel[16] = {'\0'};

static int isMenuOpen = 0;
static int menuScroll = 0;
static int menuSelectedItem = 0;
static struct {
    char *itemName;
    char *itemValue;
    char *actionLabel;
    void (*clickHandler)();
} menuItems[] = {
    {
        .itemName = "Turn off",
        .itemValue = "",
        .actionLabel = "select",
        .clickHandler = GUI_Menu_TurnOffClick,
    },
    {
        .itemName = "BLE",
        .itemValue = "CONNECTED",
        .actionLabel = "change",
        .clickHandler = GUI_Menu_BluetoothClick,
    },
    {
        .itemName = "Battery",
        .itemValue = batteryLevelMenuLabel,
        .actionLabel = "",
        .clickHandler = NULL,
    },
    {
        .itemName = "FW ver",
        .itemValue = "1.0",
        .actionLabel = "",
        .clickHandler = NULL,
    },
};

static void GUI_TimerHandler(wsfEventMask_t event, wsfMsgHdr_t *pMsg) {
    if (pMsg == NULL || pMsg->event != GUI_TIMER_TICK_EVENT) {
        return;
    }

    animationCounter++;

    int isAnimationRenderNeeded = ((!isBleConnected && isBleAdvertisign) || FuelGauge_IsCharging()) && (animationCounter % 5 == 0);

    if (isStopwatchRunning || isAnimationRenderNeeded) {
        GUI_RenderScreen();
    }

    if (isStopwatchRunning) {
        BLE_SetCurrentTime(TIME_TIMER->cnt - stopwatchStartTime);
    }

    snprintf(batteryLevelMenuLabel, sizeof(batteryLevelMenuLabel), "%d %%", FuelGauge_GetBatteryStatus());

    if (isBleConnected) {
        menuItems[1].itemValue = "connected";
    } else if (isBleAdvertisign) {
        menuItems[1].itemValue = "visible";
    } else {
        menuItems[1].itemValue = "off";
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
    mainPageButtonText[BUTTON_BTNM_NO] = "";
    mainPageButtonHandlers[BUTTON_BTNM_NO] = GUI_MenuClick;
    menuButtonText[BUTTON_BTNM_NO] = "";
    menuButtonHandlers[BUTTON_BTNM_NO] = GUI_MenuClick;

    GUI_SetReadyModeButtons();

    menuButtonText[BUTTON_BTNL_NO] = "*";
    menuButtonHandlers[BUTTON_BTNL_NO] = GUI_MenuLeftClick;
    menuButtonText[BUTTON_BTNR_NO] = "";
    menuButtonHandlers[BUTTON_BTNR_NO] = GUI_MenuRightClick;

    guiTimerHandler = WsfOsSetNextHandler(GUI_TimerHandler);

    guiTimer.handlerId = guiTimerHandler;
    guiTimer.msg.event = GUI_TIMER_TICK_EVENT;
    guiTimer.msg.param = 0;
    guiTimer.msg.status = 0;
    WsfTimerStartMs(&guiTimer, 500);

    GUI_RenderScreen();
}

void GUI_HandleButtonPress(int buttonNumber, uint32_t pressTime) {
    if (isMenuOpen) {
        if (menuButtonHandlers[buttonNumber] != NULL) {
            menuButtonHandlers[buttonNumber](pressTime);
        }
    } else {
        if (mainPageButtonHandlers[buttonNumber] != NULL) {
            mainPageButtonHandlers[buttonNumber](pressTime);
        }
    }
}

static void GUI_RenderBatteryIcon() {
    for (int i = 0; i < sizeof(batIcon); i++) {
        Display_SetPixelBuffer(GUI_BAT_POS + i, 0, batIcon[i]);
    }

    uint8_t batteryFill;
    if (FuelGauge_IsCharging()) {
        batteryFill = batIconChargeFill[(animationCounter % 30) / 5];
    } else {
        int level = FuelGauge_GetBatteryStatus();
        if (level < 16) {
            batteryFill = batIconChargeFill[0];
        } else if (level < 32) {
            batteryFill = batIconChargeFill[1];
        } else if (level < 48) {
            batteryFill = batIconChargeFill[2];
        } else if (level < 64) {
            batteryFill = batIconChargeFill[3];
        } else if (level < 80) {
            batteryFill = batIconChargeFill[4];
        } else {
            batteryFill = batIconChargeFill[5];
        }
    }
    Display_OrPixelBuffer(GUI_BAT_POS + 1, 0, batteryFill);
    Display_OrPixelBuffer(GUI_BAT_POS + 2, 0, batteryFill);
}

static void GUI_RenderStatusBar() {
    if (isMenuOpen) {
        for (int i = 0; i < sizeof(closeIcon); i++) {
            Display_SetPixelBuffer(i + GUI_MENU_POS, 0, closeIcon[i]);
        }
    } else {
        for (int i = 0; i < sizeof(menuIcon); i++) {
            Display_SetPixelBuffer(i + GUI_MENU_POS, 0, menuIcon[i]);
        }
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

    GUI_RenderBatteryIcon();

    int mainPageStatusStringLen = Display_GetStringLength(mainPageStatusString);

    int offset = GUI_STATUS_POS + (GUI_BLE_POS - GUI_STATUS_POS) / 2 - mainPageStatusStringLen / 2;

    Display_PrintString(offset, 0, mainPageStatusString);

    for (int i = GUI_STATUS_POS; i < GUI_BLE_POS; i++) {
        Display_ShiftLeftPixelBuffer(i, 0, 1);
    }
}

static void GUI_RenderButtons() {
    int buttonSize = (DISPLAY_WIDTH - 2) / 2;

    int buttonOrderRemap[2] = {BUTTON_BTNL_NO, BUTTON_BTNR_NO};

    char **textSource;
    if (isMenuOpen) {
        textSource = menuButtonText;
    } else {
        textSource = mainPageButtonText;
    }

    for (int i = 0; i < 2; i++) {
        int textLen = Display_GetStringLength(textSource[buttonOrderRemap[i]]) - 1;

        int offset = i * (DISPLAY_WIDTH / 2) + buttonSize / 2 - textLen / 2;

        Display_PrintString(offset, DISPLAY_LINES - 1, textSource[buttonOrderRemap[i]]);
    }
    for (int i = 0; i < DISPLAY_WIDTH; i++) {
        Display_ShiftLeftPixelBuffer(i, DISPLAY_LINES - 1, 2);
        Display_OrPixelBuffer(i, DISPLAY_LINES - 1, 0b00000001);
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

static void GUI_RenderMenu() {
    for (int i = 0; i < sizeof(menuItems) / sizeof(*menuItems); i++) {
        int line = 1 + i - menuScroll;
        if (line < 1 || line > 4) {
            continue;
        }

        // display item name
        Display_PrintString(1, line, menuItems[i].itemName);

        // display item name
        int valLen = Display_GetStringLength(menuItems[i].itemValue);
        Display_PrintString(DISPLAY_WIDTH - valLen, line, menuItems[i].itemValue);

        for (int j = 0; j < DISPLAY_WIDTH; j++) {
            Display_ShiftLeftPixelBuffer(j, line, 2);
        }

        // highlight selected item
        if (menuSelectedItem == i) {
            for (int j = 0; j < DISPLAY_WIDTH; j++) {
                Display_OrPixelBuffer(j, line, 0b00000001);
                Display_InvertPixelBuffer(j, line);
            }
        }
    }
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

static void GUI_MenuClick(uint32_t pressTime) {
    if (isMenuOpen) {
        isMenuOpen = 0;
    } else {
        isMenuOpen = 1;
        menuSelectedItem = 0;
        menuButtonText[BUTTON_BTNR_NO] = menuItems[menuSelectedItem].actionLabel;
    }

    GUI_RenderScreen();
}

static void GUI_MenuLeftClick(uint32_t pressTime) {
    menuSelectedItem++;

    if (menuSelectedItem >= (sizeof(menuItems) / sizeof(*menuItems))) {
        menuSelectedItem = 0;
        menuScroll = 0;
    }

    if (menuSelectedItem - menuScroll > 4) {
        menuScroll++;
    }

    menuButtonText[BUTTON_BTNR_NO] = menuItems[menuSelectedItem].actionLabel;

    GUI_RenderScreen();
}

static void GUI_MenuRightClick(uint32_t pressTime) {
    if (menuItems[menuSelectedItem].clickHandler) {
        menuItems[menuSelectedItem].clickHandler();
    }

    GUI_RenderScreen();
}

static void GUI_SetReadyModeButtons() {
    mainPageStatusString = "ready";

    mainPageButtonText[BUTTON_BTNL_NO] = "start";
    mainPageButtonHandlers[BUTTON_BTNL_NO] = GUI_StartClick;

    mainPageButtonText[BUTTON_BTNR_NO] = "";
    mainPageButtonHandlers[BUTTON_BTNR_NO] = NULL;
}

static void GUI_SetRunModeButtons() {
    if (lapCount == 0 || lapCount >= LAPS_MAX) {
        mainPageStatusString = "run";
    } else if (lapCount < 99) {
        snprintf(lapNomainPageStatusString, sizeof(lapNomainPageStatusString), "lap %d", lapCount + 1);
        mainPageStatusString = lapNomainPageStatusString;
    } else {
        snprintf(lapNomainPageStatusString, sizeof(lapNomainPageStatusString), "lp %d", lapCount + 1);
        mainPageStatusString = lapNomainPageStatusString;
    }

    mainPageButtonText[BUTTON_BTNL_NO] = "stop";
    mainPageButtonHandlers[BUTTON_BTNL_NO] = GUI_StopClick;

    if (lapCount < LAPS_MAX) {
        mainPageButtonText[BUTTON_BTNR_NO] = "lap";
        mainPageButtonHandlers[BUTTON_BTNR_NO] = GUI_LapClick;
    } else {
        mainPageButtonText[BUTTON_BTNR_NO] = "";
        mainPageButtonHandlers[BUTTON_BTNR_NO] = NULL;
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
    if (isMenuOpen) {
        GUI_RenderMenu();
    } else {
        GUI_PrintTime();
        if (lapCount > 0) {
            GUI_PrintLaps();
        }
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

static void GUI_ShutdownTimerHandler() {
    for (int i = 0; i < MXC_IRQ_COUNT; i++) {
        NVIC_DisableIRQ(i);
    }
    MXC_WUT_Disable();
    MXC_GPIO_SetWakeEn(BUTTON_GPIO, BUTTON_BTNR_PIN | BUTTON_BTNL_PIN);
    MXC_LP_EnterBackupMode();
}

static void GUI_Menu_TurnOffClick() {
    int status;

    dmConnId_t connId = AppConnIsOpen();
    if (connId != DM_CONN_ID_NONE) {
        AppConnClose(connId);
    }

    WS2812B_SetColor(0, 0, 0, 0);
    WS2812B_Transmit();
    WS2812B_Disable();

    Display_Off();

    mxc_tmr_cfg_t shutdownTmr;
    shutdownTmr.bitMode = TMR_BIT_MODE_32;
    shutdownTmr.clock = MXC_TMR_32K_CLK;
    shutdownTmr.cmp_cnt = 64000;
    shutdownTmr.mode = TMR_MODE_ONESHOT;
    shutdownTmr.pol = 0;
    shutdownTmr.pres = TMR_PRES_1;

    status = MXC_TMR_Init(MXC_TMR2, &shutdownTmr, false);
    if (status) {
        APP_TRACE_ERR1("Error while enabling shutdown timer. MXC_TMR_Init failed with status code %d", status);
        return;
    }

    MXC_NVIC_SetVector(TMR2_IRQn, GUI_ShutdownTimerHandler);
    NVIC_SetPriority(TMR2_IRQn, 0);
    NVIC_ClearPendingIRQ(TMR2_IRQn);
    NVIC_EnableIRQ(TMR2_IRQn);

    MXC_TMR_EnableInt(MXC_TMR2);

    MXC_TMR_Start(MXC_TMR2);
}

static void GUI_Menu_BluetoothClick() {
    if (isBleConnected) {
        dmConnId_t connId = AppConnIsOpen();
        if (connId != DM_CONN_ID_NONE) {
            AppConnClose(connId);
        }
    } else if (isBleAdvertisign) {
        AppAdvStop();
    } else {
        AppAdvStart(APP_MODE_AUTO_INIT);
    }
}