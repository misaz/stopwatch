/* self */
#include "FuelGauge.h"

/* max32655 + cordio */
#include <i2c.h>
#include <wsf_timer.h>
#include <wsf_trace.h>

#define FUEL_GAUGE_I2C MXC_I2C1
#define FUEL_GAUGE_I2C_I2C_IRQn I2C1_IRQn

#define FUEL_GAUGE_MAX17048_ADDR 0x36
#define FUEL_GAUGE_MAX20303_ADDR 0x28

#define FUEL_GAUGE_TIMER_TICK_EVENT 0xEA
#define FUEL_GAUGE_TIMER_DELAY 1000

static wsfTimer_t timer;
static wsfHandlerId_t timerHandler;
static int bateryStatus = 0;
static int isCharging = 0;

static uint8_t socRegAddr = 0x04;
static uint8_t socValue[2];

static uint8_t chargerStatusRegAddr = 0x06;
static uint8_t chargerStatusValue;

static mxc_i2c_req_t i2cReq;

static int operationCounter = 0;

static void FuelGauge_TimerHandler(wsfEventMask_t event, wsfMsgHdr_t *pMsg);

void FuelGauge_Init() {
    timerHandler = WsfOsSetNextHandler(FuelGauge_TimerHandler);

    timer.handlerId = timerHandler;
    timer.msg.event = FUEL_GAUGE_TIMER_TICK_EVENT;
    timer.msg.param = 0;
    timer.msg.status = 0;
    WsfTimerStartMs(&timer, 300);
}

void FueldGauge_BatteryLevelCompletionCallback(mxc_i2c_req_t *req, int result) {
    if (result == 0) {
        uint16_t first_byte = socValue[0];
        uint16_t second_byte = socValue[1];
        uint16_t val = (first_byte << 8) | second_byte;

        bateryStatus = val / 256;
        // APP_TRACE_INFO2("Fuel Gauge SOC reading completed. Value: %d. Percent: %d", val, bateryStatus);
    } else {
        APP_TRACE_ERR0("Fuel Gauge SOC reading failed");
        bateryStatus = 0;
    }
}

void FueldGauge_ChargerStatusCompletionCallback(mxc_i2c_req_t *req, int result) {
    if (result == 0) {
        uint8_t val = chargerStatusValue & 0x7;
        isCharging = (val >= 2) && (val <= 6);

        // APP_TRACE_INFO2("PMIC Charger Status reading completed. Register value: %d. Is charging: %d", chargerStatusValue, isCharging);
    } else {
        APP_TRACE_ERR0("PMIC Charger Status reading failed");
        isCharging = 0;
    }
}

static void FuelGauge_ReadBatteryLevel() {
    i2cReq.i2c = FUEL_GAUGE_I2C;
    i2cReq.addr = FUEL_GAUGE_MAX17048_ADDR;
    i2cReq.callback = FueldGauge_BatteryLevelCompletionCallback;
    i2cReq.restart = 0;
    i2cReq.tx_buf = &socRegAddr;
    i2cReq.tx_len = sizeof(socRegAddr);
    i2cReq.rx_buf = socValue;
    i2cReq.rx_len = sizeof(socValue);
}

static void FuelGauge_ReadChargerStatus() {
    i2cReq.i2c = FUEL_GAUGE_I2C;
    i2cReq.addr = FUEL_GAUGE_MAX20303_ADDR;
    i2cReq.callback = FueldGauge_ChargerStatusCompletionCallback;
    i2cReq.restart = 0;
    i2cReq.tx_buf = &chargerStatusRegAddr;
    i2cReq.tx_len = sizeof(socRegAddr);
    i2cReq.rx_buf = &chargerStatusValue;
    i2cReq.rx_len = sizeof(socValue);
}

static void FuelGauge_TimerHandler(wsfEventMask_t event, wsfMsgHdr_t *pMsg) {
    int status;

    if (pMsg == NULL || pMsg->event != FUEL_GAUGE_TIMER_TICK_EVENT) {
        return;
    }

    status = MXC_I2C_Init(FUEL_GAUGE_I2C, 1, 0);
    if (status) {
        APP_TRACE_ERR1("Fuel Gauge Initialization failed. MXC_I2C_Init failed with status code %d", status);
        WsfTimerStartMs(&timer, FUEL_GAUGE_TIMER_DELAY);
        return;
    }

    status = MXC_I2C_SetFrequency(FUEL_GAUGE_I2C, 100000);
    if (status < 0) {
        APP_TRACE_ERR1("Fuel Gauge Initialization failed. MXC_I2C_SetFrequency failed with status code %d", status);
        WsfTimerStartMs(&timer, FUEL_GAUGE_TIMER_DELAY);
        return;
    }

    NVIC_SetPriority(FUEL_GAUGE_I2C_I2C_IRQn, 3);
    NVIC_ClearPendingIRQ(FUEL_GAUGE_I2C_I2C_IRQn);
    NVIC_EnableIRQ(FUEL_GAUGE_I2C_I2C_IRQn);

    if (operationCounter++ == 3) {
        FuelGauge_ReadBatteryLevel();
        operationCounter = 0;
    } else {
        FuelGauge_ReadChargerStatus();
    }

    status = MXC_I2C_MasterTransactionAsync(&i2cReq);
    if (status) {
        WsfTimerStartMs(&timer, FUEL_GAUGE_TIMER_DELAY);
        APP_TRACE_ERR1("Fuel Gauge Initialization failed. MXC_I2C_MasterTransactionAsync failed with status code %d", status);
        return;
    }

    WsfTimerStartMs(&timer, FUEL_GAUGE_TIMER_DELAY);
}

int FuelGauge_GetBatteryStatus() {
    return bateryStatus;
}

int FuelGauge_IsCharging() {
    return isCharging;
}