/* project */
#include "Display.h"

/* stdlib */
#include <stdint.h>
#include <stdlib.h>

/* max32655 + cordio */
#include <i2c.h>
#include <wsf_timer.h>
#include <wsf_trace.h>

#define DISPLAY_I2C_ADDRESS 0x3C
#define DISPLAY_I2C MXC_I2C2
#define DISPLAY_I2C_IRQn I2C2_IRQn
#define DISPLAY_I2C_SDA_GPIO MXC_GPIO0
#define DISPLAY_I2C_SDA_GPIO_PIN MXC_GPIO_PIN_31
#define DISPLAY_I2C_SCL_GPIO MXC_GPIO0
#define DISPLAY_I2C_SCL_GPIO_PIN MXC_GPIO_PIN_30

#define DISPLAY_WIDTH 64
#define DISPLAY_LINES 6

#define DISPLAY_TIMER_TICK_EVENT 0xFA

static uint8_t fontDefinition[] = {
    0x0e, 0x11, 0x11, 0x0e, 0x12, 0x1f, 0x10, 0x12, 0x19, 0x15, 0x12, 0x11, 0x15, 0x15, 0x0a, 0x0c,
    0x0a, 0x09, 0x1f, 0x17, 0x15, 0x15, 0x0d, 0x0e, 0x15, 0x15, 0x08, 0x01, 0x01, 0x1d, 0x03, 0x0a,
    0x15, 0x15, 0x0a, 0x02, 0x15, 0x15, 0x0e, 0x1e, 0x05, 0x05, 0x1e, 0x1f, 0x15, 0x15, 0x0a, 0x0e,
    0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x0e, 0x1f, 0x15, 0x15, 0x1f, 0x05, 0x05, 0x0e, 0x11, 0x15,
    0x1d, 0x1f, 0x04, 0x04, 0x1f, 0x1f, 0x08, 0x10, 0x10, 0x0f, 0x1f, 0x04, 0x0a, 0x11, 0x1f, 0x10,
    0x10, 0x1f, 0x02, 0x04, 0x02, 0x1f, 0x1f, 0x02, 0x04, 0x1f, 0x0e, 0x11, 0x11, 0x0e, 0x1f, 0x05,
    0x05, 0x02, 0x0e, 0x11, 0x09, 0x16, 0x1f, 0x05, 0x0d, 0x12, 0x12, 0x15, 0x15, 0x09, 0x01, 0x01,
    0x1f, 0x01, 0x01, 0x0f, 0x10, 0x10, 0x0f, 0x03, 0x0c, 0x10, 0x0c, 0x03, 0x07, 0x18, 0x07, 0x18,
    0x07, 0x1b, 0x04, 0x04, 0x1b, 0x17, 0x14, 0x14, 0x0f, 0x19, 0x15, 0x15, 0x13};

static uint8_t fondIndexTable[] = {
    0x00, 0x04, 0x07, 0x0b, 0x0f, 0x13, 0x17, 0x1b, 0x1f, 0x23, 0x27, 0x27, 0x27, 0x27, 0x27, 0x27,
    0x27, 0x27, 0x2b, 0x2f, 0x33, 0x37, 0x3a, 0x3d, 0x41, 0x45, 0x46, 0x4a, 0x4e, 0x51, 0x56, 0x5a,
    0x5e, 0x62, 0x66, 0x6a, 0x6e, 0x73, 0x77, 0x7c, 0x81, 0x85, 0x89, sizeof(fontDefinition)};

static uint8_t configCommands[] = {
    // from display datasheet:
    0xAE,  // Display Off
    0xD5,  // SET DISPLAY CLOCK
    0x80,  // 105HZ
    0xA8,  // Select Multiplex Ratio
    47,
    0xD3,  // Setting Display Offset
    0x00,  // 00H Reset, set common start
    0x40,  // Set Display Start Line
    0x8D,  // Set Charge Pump
    0x14,  // Endable Charge Pump
    0x20,
    0x00,
    // Set Segment Re-Map Default
    // 0xA0 (0x00) => column Address 0 mapped to 127
    // 0xA1 (0x01) => Column Address 127 mapped to 0
    0xA1,
    0xC8,  // Set COM Output Scan Direction
    0xDA,  // Set COM Hardware Configuration
    0x81,  // Set Contrast Control
    0xff,
    0xD9,  // Set Pre-Charge period
    0x22,
    0xDB,  // Set Deselect Vcomh level
    0x40,
    0xA4,  // Entire Display ON
    0xA6,  // Set Normal Display
    0xAF,  // Display ON
};
static uint8_t *configCommandsEnd = configCommands + sizeof(configCommands);
static uint8_t *configCommandsToExecute = configCommands;

static uint8_t sendBufferCommands[] = {
    0x22,
    0,
    6,
    0x21,
    32,
    95,
};
static uint8_t *sendBufferCommandsEnd = sendBufferCommands + sizeof(sendBufferCommands);
static uint8_t *sendBufferCommandToExecute = sendBufferCommands;

static uint8_t commandBuffer[2] = {};

static uint8_t buffer1[1 + DISPLAY_WIDTH * DISPLAY_LINES] = {0x40};
static uint8_t buffer2[1 + DISPLAY_WIDTH * DISPLAY_LINES] = {0x40};
static uint8_t buffer3[1 + DISPLAY_WIDTH * DISPLAY_LINES] = {0x40};

static uint8_t *workingBuffer = buffer1 + 1;
static uint8_t *readyBuffer = buffer2 + 1;
static uint8_t *transmitBuffer = buffer3 + 1;

static int isInitialized = 0;
static int isTransmitRequested = 0;
static int isIdle = 1;

static mxc_i2c_req_t i2cRequest;

static wsfHandlerId_t displayOpTimerHandler;
static wsfTimer_t displayOpTimer;

static enum {
    DISPLAY_STATE_UNINITIALIZED,
    DISPLAY_STATE_INIT_COMMANDS,
    DISPLAY_STATE_SEND_PREAMBLE,
    DISPLAY_STATE_SEND_BUFFER,
    DISPLAY_STATE_IDLE
} currentState = DISPLAY_STATE_UNINITIALIZED;

// The same I2C2_IRQHandler is defined in BLE stack (pal_twi.c)
// void I2C2_IRQHandler() {
//     MXC_I2C_AsyncHandler(DISPLAY_I2C);
// }

static void Display_TransmitNextConfigCommand();
static void Display_TransmitNextSendBufferCommand();
static void Display_TransmitScreenData();

static void Display_SwapBuffers(uint8_t **b1, uint8_t **b2) {
    uint8_t *temp = *b1;
    *b1 = *b2;
    *b2 = temp;
}

static void Display_CompletionCallback(mxc_i2c_req_t *req, int result) {
}

static void Display_ConfigCommandCompletionCallback(mxc_i2c_req_t *req, int result) {
    APP_TRACE_INFO1("Display_ConfigCommandCompletionCallback result=%d", result);

    if (result == 0) {
        configCommandsToExecute++;
        if (configCommandsToExecute < configCommandsEnd) {
            Display_TransmitNextConfigCommand();
        } else {
            if (isTransmitRequested) {
                isTransmitRequested = 0;
                Display_SwapBuffers(&transmitBuffer, &readyBuffer);
                sendBufferCommandToExecute = sendBufferCommands;
                Display_TransmitNextSendBufferCommand();
            } else {
                APP_TRACE_INFO0("Display I2C is now idle");
                isIdle = 1;
            }
        }
    } else {
        // error happended
        configCommandsToExecute = configCommands;
        Display_TransmitNextConfigCommand();
    }
}

static void Display_NextSendBufferCompletionCallback(mxc_i2c_req_t *req, int result) {
    APP_TRACE_INFO1("Display_NextSendBufferCompletionCallback result=%d", result);

    if (result == 0) {
        sendBufferCommandToExecute++;
        if (sendBufferCommandToExecute < sendBufferCommandsEnd) {
            Display_TransmitNextSendBufferCommand();
        } else if (sendBufferCommandToExecute == sendBufferCommandsEnd) {
            Display_TransmitScreenData();
        } else {
            if (isTransmitRequested) {
                isTransmitRequested = 0;
                Display_SwapBuffers(&transmitBuffer, &readyBuffer);
                sendBufferCommandToExecute = sendBufferCommands;
                Display_TransmitNextSendBufferCommand();
            } else {
                APP_TRACE_INFO0("Display I2C is now idle");
                isIdle = 1;
            }
        }
    } else {
        // error happended
        configCommandsToExecute = configCommands;
        Display_TransmitNextConfigCommand();
    }
}

static void Display_BufferCompletionCallback(mxc_i2c_req_t *req, int result) {
    APP_TRACE_INFO1("Display_BufferCompletionCallback result=%d", result);

    if (result == 0) {
        if (isTransmitRequested) {
            isTransmitRequested = 0;
            Display_SwapBuffers(&transmitBuffer, &readyBuffer);
            sendBufferCommandToExecute = sendBufferCommands;
            Display_TransmitNextSendBufferCommand();
        } else {
            APP_TRACE_INFO0("Display I2C is now idle");
            isIdle = 1;
        }
    } else {
        // error happended
        configCommandsToExecute = configCommands;
        Display_TransmitNextConfigCommand();
    }
}

static void Display_TransmitNextConfigCommand() {
    int status;
    APP_TRACE_INFO0("Display_TransmitNextConfigCommand");

    commandBuffer[0] = 0x00;
    commandBuffer[1] = *configCommandsToExecute;

    i2cRequest.tx_buf = commandBuffer;
    i2cRequest.tx_len = sizeof(commandBuffer);
    i2cRequest.callback = Display_ConfigCommandCompletionCallback;

    status = MXC_I2C_MasterTransactionAsync(&i2cRequest);
    if (status) {
        APP_TRACE_ERR1("Display_TransmitNextConfigCommand: MXC_I2C_MasterTransactionAsync failed=%d", status);
    }
}

static void Display_TransmitNextSendBufferCommand() {
    int status;
    APP_TRACE_INFO0("Display_TransmitNextSendBufferCommand");

    commandBuffer[0] = 0x00;
    commandBuffer[1] = *sendBufferCommandToExecute;

    i2cRequest.tx_buf = commandBuffer;
    i2cRequest.tx_len = sizeof(commandBuffer);
    i2cRequest.callback = Display_NextSendBufferCompletionCallback;

    status = MXC_I2C_MasterTransactionAsync(&i2cRequest);
    if (status) {
        APP_TRACE_ERR1("Display_TransmitNextSendBufferCommand: MXC_I2C_MasterTransactionAsync failed=%d", status);
    }
}

static void Display_TransmitScreenData() {
    int status;
    APP_TRACE_INFO0("Display_TransmitScreenData");

    i2cRequest.tx_buf = transmitBuffer - 1;
    i2cRequest.tx_len = sizeof(buffer1);
    i2cRequest.callback = Display_BufferCompletionCallback;

    status = MXC_I2C_MasterTransactionAsync(&i2cRequest);
    if (status) {
        APP_TRACE_ERR1("Display_TransmitScreenData: MXC_I2C_MasterTransactionAsync failed=%d", status);
    }
}

static void Display_TimerHandler(wsfEventMask_t event, wsfMsgHdr_t *pMsg) {
    if (pMsg && pMsg->event == DISPLAY_TIMER_TICK_EVENT) {
        APP_TRACE_INFO0("tick");
        WsfTimerStartMs(&displayOpTimer, 250);
    }
}

void Display_Init() {
    APP_TRACE_INFO0("Display_Init");
    int status;

    i2cRequest.addr = DISPLAY_I2C_ADDRESS;
    i2cRequest.i2c = DISPLAY_I2C;
    i2cRequest.restart = 0;
    i2cRequest.rx_buf = NULL;
    i2cRequest.rx_len = 0;

    isIdle = 0;

    status = MXC_I2C_Init(DISPLAY_I2C, 1, 0);
    if (status) {
        APP_TRACE_ERR1("Display_Init: MXC_I2C_Init failed=%d", status);
        return;
    }

    MXC_GPIO_SetVSSEL(DISPLAY_I2C_SDA_GPIO, MXC_GPIO_VSSEL_VDDIOH, DISPLAY_I2C_SDA_GPIO_PIN);
    MXC_GPIO_SetVSSEL(DISPLAY_I2C_SCL_GPIO, MXC_GPIO_VSSEL_VDDIOH, DISPLAY_I2C_SCL_GPIO_PIN);

    status = MXC_I2C_SetFrequency(DISPLAY_I2C, 100000);
    if (status < 0) {
        APP_TRACE_ERR1("Display_Init: MXC_I2C_SetFrequency failed=%d", status);
        return;
    }

    isInitialized = 1;

    displayOpTimerHandler = WsfOsSetNextHandler(Display_TimerHandler);
    displayOpTimer.handlerId = displayOpTimerHandler;
    displayOpTimer.msg.event = DISPLAY_TIMER_TICK_EVENT;
    displayOpTimer.msg.param = 0;
    displayOpTimer.msg.status = 0;
    WsfTimerStartMs(&displayOpTimer, 250);

    APP_TRACE_INFO0("Display_Init: Initialization successfull");

    Display_TransmitNextConfigCommand();
}

void Display_Show() {
    APP_TRACE_INFO0("Display_Show");
    if (!isInitialized) {
        APP_TRACE_INFO0("Display_Show: Initializing");
        Display_Init();
        if (!isInitialized) {
            return;
        }
    }

    NVIC_DisableIRQ(DISPLAY_I2C_IRQn);

    Display_SwapBuffers(&workingBuffer, &readyBuffer);

    isTransmitRequested = 1;
    if (isIdle) {
        Display_SwapBuffers(&transmitBuffer, &readyBuffer);

        sendBufferCommandToExecute = sendBufferCommands;
        Display_TransmitNextSendBufferCommand();
    }
    NVIC_EnableIRQ(DISPLAY_I2C_IRQn);
}

void Display_Clear() {
    for (int i = 0; i < 64 * 6; i++) {
        workingBuffer[i] = 0x00;
    }
}

void Display_SetPixelBuffer(int x, int row, uint8_t value) {
    workingBuffer[row * 64 + x] = value;
}

int Display_PrintChar(int x, int row, char ch) {
    if (ch == ':') {
        Display_SetPixelBuffer(x, row, 0x0A);
        Display_SetPixelBuffer(x + 1, row, 0);
        return x + 2;
    }

    if (ch >= 'a' && ch <= 'z') {
        ch -= 32;
    }

    if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z')) {
        ch -= 48;

        size_t startIndex = fondIndexTable[(unsigned char)ch];
        size_t endIndex = fondIndexTable[ch + 1];

        for (size_t i = startIndex; i < endIndex; i++) {
            Display_SetPixelBuffer(x++, row, fontDefinition[i]);
        }
        Display_SetPixelBuffer(x++, row, 0x00);
    }

    return x;
}

int Display_PrintString(int x, int row, char *str) {
    while (*str) {
        x = Display_PrintChar(x, row, *str);
        str++;
    }
    return x;
}
