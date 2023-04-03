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
    0x12,
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

static int isTransmitRequested = 0;
static int isIdle = 1;

static mxc_i2c_req_t i2cRequest;

static wsfHandlerId_t displayOpTimerHandler;
static wsfTimer_t displayOpTimer;

static enum {
    DISPLAY_STATE_UNINITIALIZED,
    DISPLAY_STATE_INIT_COMMANDS,
    DISPLAY_STATE_SEND_BUFFER_COMMANDS,
    DISPLAY_STATE_SEND_BUFFER,
    DISPLAY_STATE_IDLE,
    DISPLAY_STATE_OFF_REQUEST,
    DISPLAY_STATE_OFF
} currentState = DISPLAY_STATE_UNINITIALIZED;

static int isI2CActive = 0;
static int lastTransactionStatus = 0;

// The same I2C2_IRQHandler is defined in BLE stack (pal_twi.c)
// void I2C2_IRQHandler() {
//     MXC_I2C_AsyncHandler(DISPLAY_I2C);
// }

static void Display_InitI2C();
static void Display_TransmitNextConfigCommand();
static void Display_TransmitNextSendBufferCommand();
static void Display_TransmitScreenData();

static void Display_SwapBuffers(uint8_t **b1, uint8_t **b2) {
    uint8_t *temp = *b1;
    *b1 = *b2;
    *b2 = temp;
}

static void Display_CompletionCallback(mxc_i2c_req_t *req, int result) {
    lastTransactionStatus = result;
    isI2CActive = 0;
}

static void Display_TransmitNextConfigCommand() {
    int status;

    commandBuffer[0] = 0x00;
    commandBuffer[1] = *configCommandsToExecute;

    i2cRequest.tx_buf = commandBuffer;
    i2cRequest.tx_len = sizeof(commandBuffer);

    isI2CActive = 1;

    status = MXC_I2C_MasterTransactionAsync(&i2cRequest);
    if (status) {
        APP_TRACE_ERR1("Display_TransmitNextConfigCommand: MXC_I2C_MasterTransactionAsync failed=%d", status);
    }
}

static void Display_TransmitOffCommand() {
    int status;

    commandBuffer[0] = 0x00;
    commandBuffer[1] = 0xAE;

    i2cRequest.tx_buf = commandBuffer;
    i2cRequest.tx_len = sizeof(commandBuffer);

    isI2CActive = 1;

    status = MXC_I2C_MasterTransactionAsync(&i2cRequest);
    if (status) {
        APP_TRACE_ERR1("Display_TransmitOffCommand: MXC_I2C_MasterTransactionAsync failed=%d", status);
    }
}

static void Display_TransmitNextSendBufferCommand() {
    int status;

    commandBuffer[0] = 0x00;
    commandBuffer[1] = *sendBufferCommandToExecute;

    i2cRequest.tx_buf = commandBuffer;
    i2cRequest.tx_len = sizeof(commandBuffer);

    isI2CActive = 1;

    status = MXC_I2C_MasterTransactionAsync(&i2cRequest);
    if (status) {
        APP_TRACE_ERR1("Display_TransmitNextSendBufferCommand: MXC_I2C_MasterTransactionAsync failed=%d", status);
    }
}

static void Display_TransmitScreenData() {
    int status;

    i2cRequest.tx_buf = transmitBuffer - 1;
    i2cRequest.tx_len = sizeof(buffer1);

    isI2CActive = 1;

    status = MXC_I2C_MasterTransactionAsync(&i2cRequest);
    if (status) {
        APP_TRACE_ERR1("Display_TransmitScreenData: MXC_I2C_MasterTransactionAsync failed=%d", status);
    }
}

static void Display_TimerHandler(wsfEventMask_t event, wsfMsgHdr_t *pMsg) {
    if (pMsg == NULL || pMsg->event != DISPLAY_TIMER_TICK_EVENT) {
        return;
    }

    if (currentState == DISPLAY_STATE_UNINITIALIZED) {
        Display_InitI2C();
    } else if (currentState == DISPLAY_STATE_INIT_COMMANDS && !isI2CActive) {
        if (lastTransactionStatus == 0) {
            if (configCommandsToExecute < configCommandsEnd) {
                Display_TransmitNextConfigCommand();
                configCommandsToExecute++;
            } else {
                if (isTransmitRequested) {
                    isTransmitRequested = 0;
                    Display_SwapBuffers(&transmitBuffer, &readyBuffer);
                    sendBufferCommandToExecute = sendBufferCommands;
                    currentState = DISPLAY_STATE_SEND_BUFFER_COMMANDS;
                } else {
                    currentState = DISPLAY_STATE_IDLE;
                }
            }
        } else {
            configCommandsToExecute = configCommands;
            currentState = DISPLAY_STATE_INIT_COMMANDS;
        }
    } else if (currentState == DISPLAY_STATE_SEND_BUFFER_COMMANDS && !isI2CActive) {
        if (lastTransactionStatus == 0) {
            if (sendBufferCommandToExecute < sendBufferCommandsEnd) {
                Display_TransmitNextSendBufferCommand();
                sendBufferCommandToExecute++;
            } else {
                currentState = DISPLAY_STATE_SEND_BUFFER;
                Display_TransmitScreenData();
            }
        } else {
            configCommandsToExecute = configCommands;
            currentState = DISPLAY_STATE_INIT_COMMANDS;
        }
    } else if (currentState == DISPLAY_STATE_SEND_BUFFER && !isI2CActive) {
        if (lastTransactionStatus == 0) {
            if (isTransmitRequested) {
                isTransmitRequested = 0;
                Display_SwapBuffers(&transmitBuffer, &readyBuffer);
                sendBufferCommandToExecute = sendBufferCommands;
                currentState = DISPLAY_STATE_SEND_BUFFER_COMMANDS;
            } else {
                currentState = DISPLAY_STATE_IDLE;
            }
        } else {
            configCommandsToExecute = configCommands;
            currentState = DISPLAY_STATE_INIT_COMMANDS;
        }
    } else if (currentState == DISPLAY_STATE_IDLE) {
        if (isTransmitRequested) {
            isTransmitRequested = 0;
            Display_SwapBuffers(&transmitBuffer, &readyBuffer);
            sendBufferCommandToExecute = sendBufferCommands;
            currentState = DISPLAY_STATE_SEND_BUFFER_COMMANDS;
        }
    } else if (currentState == DISPLAY_STATE_OFF_REQUEST && !isI2CActive) {
        Display_TransmitOffCommand();
        currentState = DISPLAY_STATE_OFF;
    } else if (currentState == DISPLAY_STATE_OFF && !isI2CActive) {
        currentState = DISPLAY_STATE_OFF_REQUEST;
    }

    WsfTimerStartMs(&displayOpTimer, 1);
}

void Display_Init() {
    displayOpTimerHandler = WsfOsSetNextHandler(Display_TimerHandler);
    displayOpTimer.handlerId = displayOpTimerHandler;
    displayOpTimer.msg.event = DISPLAY_TIMER_TICK_EVENT;
    displayOpTimer.msg.param = 0;
    displayOpTimer.msg.status = 0;
    WsfTimerStartMs(&displayOpTimer, 250);
}

static void Display_InitI2C() {
    int status;

    i2cRequest.addr = DISPLAY_I2C_ADDRESS;
    i2cRequest.i2c = DISPLAY_I2C;
    i2cRequest.restart = 0;
    i2cRequest.rx_buf = NULL;
    i2cRequest.rx_len = 0;
    i2cRequest.callback = Display_CompletionCallback;

    isIdle = 0;

    status = MXC_I2C_Init(DISPLAY_I2C, 1, 0);
    if (status) {
        APP_TRACE_ERR1("Display_InitI2C: MXC_I2C_Init failed=%d", status);
        return;
    }

    MXC_GPIO_SetVSSEL(DISPLAY_I2C_SDA_GPIO, MXC_GPIO_VSSEL_VDDIOH, DISPLAY_I2C_SDA_GPIO_PIN);
    MXC_GPIO_SetVSSEL(DISPLAY_I2C_SCL_GPIO, MXC_GPIO_VSSEL_VDDIOH, DISPLAY_I2C_SCL_GPIO_PIN);

    status = MXC_I2C_SetFrequency(DISPLAY_I2C, 100000);
    if (status < 0) {
        APP_TRACE_ERR1("Display_InitI2C: MXC_I2C_SetFrequency failed=%d", status);
        return;
    }

    isI2CActive = 0;
    configCommandsToExecute = configCommands;
    currentState = DISPLAY_STATE_INIT_COMMANDS;

    NVIC_SetPriority(DISPLAY_I2C_IRQn, 3);
    NVIC_ClearPendingIRQ(DISPLAY_I2C_IRQn);
    NVIC_EnableIRQ(DISPLAY_I2C_IRQn);
}

void Display_Off() {
    currentState = DISPLAY_STATE_OFF_REQUEST;
}

void Display_Show() {
    Display_SwapBuffers(&workingBuffer, &readyBuffer);
    isTransmitRequested = 1;
}

void Display_Clear() {
    for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_LINES; i++) {
        workingBuffer[i] = 0x00;
    }
}

void Display_SetPixelBuffer(int x, int row, uint8_t value) {
    if (x >= DISPLAY_WIDTH || row >= DISPLAY_LINES) {
        return;
    }
    workingBuffer[row * DISPLAY_WIDTH + x] = value;
}

void Display_OrPixelBuffer(int x, int row, uint8_t value) {
    workingBuffer[row * DISPLAY_WIDTH + x] |= value;
}

void Display_InvertPixelBuffer(int x, int row) {
    workingBuffer[row * DISPLAY_WIDTH + x] ^= 0xFF;
}

void Display_ShiftLeftPixelBuffer(int x, int row, int shift) {
    workingBuffer[row * DISPLAY_WIDTH + x] <<= shift;
}

void Display_ShiftRightPixelBuffer(int x, int row, int shift) {
    workingBuffer[row * DISPLAY_WIDTH + x] >>= shift;
}

int Display_PrintChar(int x, int row, char ch) {
    if (ch == ':') {
        Display_SetPixelBuffer(x, row, 0x0A);
        Display_SetPixelBuffer(x + 1, row, 0);
        return x + 2;
    } else if (ch == '.') {
        Display_SetPixelBuffer(x, row, 0x10);
        Display_SetPixelBuffer(x + 1, row, 0);
        return x + 2;
    } else if (ch == '*') {
        Display_SetPixelBuffer(x + 0, row, 0b00000010);
        Display_SetPixelBuffer(x + 1, row, 0b00000100);
        Display_SetPixelBuffer(x + 2, row, 0b00001000);
        Display_SetPixelBuffer(x + 3, row, 0b00000100);
        Display_SetPixelBuffer(x + 4, row, 0b00000010);
        Display_SetPixelBuffer(x + 5, row, 0);
        return x + 6;
    } else if (ch == ' ') {
        Display_SetPixelBuffer(x, row, 0);
        Display_SetPixelBuffer(x + 1, row, 0);
        return x + 2;
    } else if (ch == '%') {
        Display_SetPixelBuffer(x + 0, row, 0b00010011);
        Display_SetPixelBuffer(x + 1, row, 0b00001011);
        Display_SetPixelBuffer(x + 2, row, 0b00000100);
        Display_SetPixelBuffer(x + 3, row, 0b00011010);
        Display_SetPixelBuffer(x + 4, row, 0b00011001);
        Display_SetPixelBuffer(x + 5, row, 0);
        return x + 6;
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

int Display_GetCharLength(char ch) {
    if (ch == ':') {
        return 2;
    } else if (ch == '.') {
        return 2;
    } else if (ch == '*') {
        return 6;
    } else if (ch == ' ') {
        return 2;
    } else if (ch == '%') {
        return 6;
    }

    if (ch >= 'a' && ch <= 'z') {
        ch -= 32;
    }

    if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z')) {
        ch -= 48;

        size_t startIndex = fondIndexTable[(unsigned char)ch];
        size_t endIndex = fondIndexTable[ch + 1];

        return endIndex - startIndex + 1;
    }

    return 0;
}

int Display_PrintString(int x, int row, char *str) {
    while (*str) {
        x = Display_PrintChar(x, row, *str);
        str++;
    }
    return x;
}

int Display_GetStringLength(char *str) {
    int len = 0;
    while (*str) {
        len += Display_GetCharLength(*str);
        str++;
    }
    return len;
}