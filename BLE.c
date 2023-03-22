/* project */
#include "BLE.h"

/* stdlib */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* max32655 + mbed + cordio */
#include <app_api.h>
#include <app_main.h>
#include <app_terminal.h>
#include <att_handler.h>
#include <bb_api.h>
#include <cfg_mac_ble.h>
#include <dm_handler.h>
#include <gatt/gatt_api.h>
#include <hci_handler.h>
#include <l2c_api.h>
#include <l2c_handler.h>
#include <ll_api.h>
#include <ll_init_api.h>
#include <max32655.h>
#include <pal_bb.h>
#include <pal_cfg.h>
#include <rtc.h>
#include <smp_api.h>
#include <smp_handler.h>
#include <svc_core.h>
#include <trimsir_regs.h>
#include <util/bstream.h>
#include <wsf_buf.h>
#include <wsf_bufio.h>
#include <wsf_heap.h>
#include <wsf_msg.h>
#include <wsf_timer.h>
#include <wsf_trace.h>
#include <wsf_types.h>
#include <wut.h>

void wutTrimCb(int err);
static void fitDmCback(dmEvt_t *pDmEvt);
static void fitAttCback(attEvt_t *pEvt);
static void fitCccCback(attsCccEvt_t *pEvt);
static void BLE_Handler(wsfEventMask_t event, wsfMsgHdr_t *pMsg);
static void BLE_Start();
static void fitClose(wsfMsgHdr_t *msg);
static void fitProcMsg(wsfMsgHdr_t *pMsg);
static void fitProcCccState(wsfMsgHdr_t *msg);
static void BLE_HandlerInit(wsfHandlerId_t handlerId);
static uint8_t BLE_StopwatchWriteCallback(dmConnId_t connId, uint16_t handle, uint8_t operation, uint16_t offset, uint16_t len, uint8_t *pValue, attsAttr_t *pAttr);

static wsfBufPoolDesc_t poolDescriptions[] = {
    {.len = 16, .num = 8},
    {.len = 32, .num = 4},
    {.len = 192, .num = 8},
    {.len = 256, .num = 8},
    {.len = 512, .num = 4},
};

static const smpCfg_t fitSmpCfg = {
    .attemptTimeout = 500,
    .ioCap = SMP_IO_NO_IN_NO_OUT,
    .minKeyLen = 7,
    .maxKeyLen = 16,
    .maxAttempts = 1,
    .auth = 0,
    .maxAttemptTimeout = 64000,
    .attemptDecTimeout = 64000,
    .attemptExp = 2,
};

static const appAdvCfg_t fitAdvCfg = {
    .advDuration = {60000, 0, 0},
    .advInterval = {800, 0, 0},
};

static const appSlaveCfg_t fitSlaveCfg = {
    .connMax = 1,
};

static const appSecCfg_t fitSecCfg = {
    .auth = DM_AUTH_BOND_FLAG | DM_AUTH_SC_FLAG,
    .iKeyDist = 0,
    .rKeyDist = DM_KEY_DIST_LTK,
    .oob = FALSE,
    .initiateSec = TRUE,
};

static const appUpdateCfg_t fitUpdateCfg = {
    .idlePeriod = 6000,
    .connIntervalMin = 640,
    .connIntervalMax = 800,
    .connLatency = 0,
    .supTimeout = 900,
    .maxAttempts = 5,
};

static const uint8_t fitAdvDataDisc[] = {
    /*! flags */
    2,                                                  /*! length */
    DM_ADV_TYPE_FLAGS,                                  /*! AD type */
    DM_FLAG_LE_GENERAL_DISC | DM_FLAG_LE_BREDR_NOT_SUP, /*! flags */

    /*! tx power */
    2,                    /*! length */
    DM_ADV_TYPE_TX_POWER, /*! AD type */
    0,                    /*! tx power */
};

static const uint8_t fitScanDataDisc[] = {
    /*! device name */
    11,                     /*! length */
    DM_ADV_TYPE_LOCAL_NAME, /*! AD type */
    'M',
    'i',
    's',
    'a',
    'z',
    ' ',
    't',
    'e',
    's',
    't',
};

#define STOPWATCH_SERVICE_GUID 0x00, 0xca, 0x1f, 0x05, 0x95, 0x95, 0xf5, 0xd6, 0x21, 0x7c, 0xcc, 0x85, 0x88, 0x1e, 0x61, 0x2c
#define STOPWATCH_STATUS_CHARACTERISTICS_GUID 0x01, 0xca, 0x1f, 0x05, 0x95, 0x95, 0xf5, 0xd6, 0x21, 0x7c, 0xcc, 0x85, 0x88, 0x1e, 0x61, 0x2c
#define STOPWATCH_ELAPSED_CHARACTERISTICS_GUID 0x10, 0xca, 0x1f, 0x05, 0x95, 0x95, 0xf5, 0xd6, 0x21, 0x7c, 0xcc, 0x85, 0x88, 0x1e, 0x61, 0x2c
#define STOPWATCH_LAPS_COUNT_CHARACTERISTICS_GUID 0x20, 0xca, 0x1f, 0x05, 0x95, 0x95, 0xf5, 0xd6, 0x21, 0x7c, 0xcc, 0x85, 0x88, 0x1e, 0x61, 0x2c
#define STOPWATCH_LAP_SELECT_CHARACTERISTICS_GUID 0x21, 0xca, 0x1f, 0x05, 0x95, 0x95, 0xf5, 0xd6, 0x21, 0x7c, 0xcc, 0x85, 0x88, 0x1e, 0x61, 0x2c
#define STOPWATCH_LAP_TIME_CHARACTERISTICS_GUID 0x22, 0xca, 0x1f, 0x05, 0x95, 0x95, 0xf5, 0xd6, 0x21, 0x7c, 0xcc, 0x85, 0x88, 0x1e, 0x61, 0x2c

#define STOPWATCH_HANDLE_OFFSET 1000

enum {
    STOPWATCH_SERVICE_HANDLE = STOPWATCH_HANDLE_OFFSET,

    STOPWATCH_STATUS_CHARACTERISTICS_HANDLE,
    STOPWATCH_STATUS_VALUE_HANDLE,
    STOPWATCH_STATUS_CCC_HANDLE,

    STOPWATCH_ELAPSED_CHARACTERISTICS_HANDLE,
    STOPWATCH_ELAPSED_VALUE_HANDLE,

    STOPWATCH_LAPS_COUNT_CHARACTERISTICS_HANDLE,
    STOPWATCH_LAPS_COUNT_VALUE_HANDLE,
    STOPWATCH_LAPS_COUNT_CCC_HANDLE,

    STOPWATCH_LAP_SELECT_CHARACTERISTICS_HANDLE,
    STOPWATCH_LAP_SELECT_VALUE_HANDLE,

    STOPWATCH_LAP_TIME_CHARACTERISTICS_HANDLE,
    STOPWATCH_LAP_TIME_VALUE_HANDLE,

    STOPWATCH_LAST_HANDLE
};

static uint8_t stopwatchServiceGuid[ATT_128_UUID_LEN] = {STOPWATCH_SERVICE_GUID};
static uint8_t stopwatchStatusCharacteristicsGuid[ATT_128_UUID_LEN] = {STOPWATCH_STATUS_CHARACTERISTICS_GUID};
static uint8_t stopwatchElapsedCharacteristicsGuid[ATT_128_UUID_LEN] = {STOPWATCH_ELAPSED_CHARACTERISTICS_GUID};
static uint8_t stopwatchLapsCountCharacteristicsGuid[ATT_128_UUID_LEN] = {STOPWATCH_LAPS_COUNT_CHARACTERISTICS_GUID};
static uint8_t stopwatchLapSelectCharacteristicsGuid[ATT_128_UUID_LEN] = {STOPWATCH_LAP_SELECT_CHARACTERISTICS_GUID};
static uint8_t stopwatchLapTimeCharacteristicsGuid[ATT_128_UUID_LEN] = {STOPWATCH_LAP_TIME_CHARACTERISTICS_GUID};

static uint16_t stopwatchServiceGuidLength = sizeof(stopwatchServiceGuid);

static uint8_t stopwatchStatusCharacteristicsValue[] = {
    ATT_PROP_READ | ATT_PROP_NOTIFY,
    UINT16_TO_BYTES(STOPWATCH_STATUS_VALUE_HANDLE),
    STOPWATCH_STATUS_CHARACTERISTICS_GUID,
};
static uint16_t stopwatchStatusCharacteristicsValueLength = sizeof(stopwatchStatusCharacteristicsValue);
static uint8_t stopwatchStatus = 0;
static uint16_t stopwatchStatusLength = sizeof(stopwatchStatus);
static uint8_t stopwatchStatusCcc[] = {UINT16_TO_BYTES(0x0000)};
static uint16_t stopwatchStatusCccLength = sizeof(stopwatchStatusCcc);

static uint8_t stopwatchElapsedCharacteristicsValue[] = {
    ATT_PROP_READ,
    UINT16_TO_BYTES(STOPWATCH_ELAPSED_VALUE_HANDLE),
    STOPWATCH_ELAPSED_CHARACTERISTICS_GUID,
};
static uint16_t stopwatchElapsedCharacteristicsValueLength = sizeof(stopwatchElapsedCharacteristicsValue);
static uint32_t stopwatchElapsed = 0;
static uint16_t stopwatchElapsedLength = sizeof(stopwatchElapsed);

static uint8_t stopwatchLapsCountCharacteristicsValue[] = {
    ATT_PROP_READ | ATT_PROP_NOTIFY,
    UINT16_TO_BYTES(STOPWATCH_LAPS_COUNT_VALUE_HANDLE),
    STOPWATCH_LAPS_COUNT_CHARACTERISTICS_GUID,
};
static uint16_t stopwatchLapsCountCharacteristicsValueLength = sizeof(stopwatchLapsCountCharacteristicsValue);
static uint8_t stopwatchLapsCount = 0;
static uint16_t stopwatchLapsCountLength = sizeof(stopwatchLapsCount);
static uint8_t stopwatchLapsCountCcc[] = {UINT16_TO_BYTES(0x0000)};
static uint16_t stopwatchLapsCountCccLength = sizeof(stopwatchLapsCountCcc);

static uint8_t stopwatchLapSelectCharacteristicsValue[] = {
    ATT_PROP_READ | ATT_PROP_WRITE,
    UINT16_TO_BYTES(STOPWATCH_LAP_SELECT_VALUE_HANDLE),
    STOPWATCH_LAP_SELECT_CHARACTERISTICS_GUID,
};
static uint16_t stopwatchLapSelectCharacteristicsValueLength = sizeof(stopwatchLapSelectCharacteristicsValue);
static uint8_t stopwatchLapSelect = 0;
static uint16_t stopwatchLapSelectLength = sizeof(stopwatchLapSelect);

static uint8_t stopwatchLapTimeCharacteristicsValue[] = {
    ATT_PROP_READ,
    UINT16_TO_BYTES(STOPWATCH_LAP_TIME_VALUE_HANDLE),
    STOPWATCH_LAP_TIME_CHARACTERISTICS_GUID,
};
static uint16_t stopwatchLapTimeCharacteristicsValueLength = sizeof(stopwatchLapTimeCharacteristicsValue);
static uint32_t stopwatchLapTime = 0;
static uint16_t stopwatchLapTimeLength = sizeof(stopwatchLapTime);

static attsAttr_t stopwatchAttributes[] = {
    /* Service */
    {
        .pUuid = attPrimSvcUuid,
        .pValue = stopwatchServiceGuid,
        .pLen = &stopwatchServiceGuidLength,
        .maxLen = sizeof(stopwatchServiceGuid),
        .settings = 0,
        .permissions = ATTS_PERMIT_READ,
    },

    /* Status characteristics */
    {
        .pUuid = attChUuid,
        .pValue = stopwatchStatusCharacteristicsValue,
        .pLen = &stopwatchStatusCharacteristicsValueLength,
        .maxLen = sizeof(stopwatchStatusCharacteristicsValue),
        .settings = 0,
        .permissions = ATTS_PERMIT_READ,
    },
    {
        .pUuid = stopwatchStatusCharacteristicsGuid,
        .pValue = &stopwatchStatus,
        .pLen = &stopwatchStatusLength,
        .maxLen = sizeof(stopwatchStatus),
        .settings = 0,
        .permissions = ATTS_PERMIT_READ,
    },
    {
        .pUuid = attCliChCfgUuid,
        .pValue = stopwatchStatusCcc,
        .pLen = &stopwatchStatusCccLength,
        .maxLen = sizeof(stopwatchStatusCcc),
        .settings = ATTS_SET_CCC,
        .permissions = ATTS_PERMIT_READ | ATTS_PERMIT_WRITE,
    },

    /* Elapsed characteristics */
    {
        .pUuid = attChUuid,
        .pValue = stopwatchElapsedCharacteristicsValue,
        .pLen = &stopwatchElapsedCharacteristicsValueLength,
        .maxLen = sizeof(stopwatchElapsedCharacteristicsValue),
        .settings = 0,
        .permissions = ATTS_PERMIT_READ,
    },
    {
        .pUuid = stopwatchElapsedCharacteristicsGuid,
        .pValue = (uint8_t *)&stopwatchElapsed,
        .pLen = &stopwatchElapsedLength,
        .maxLen = sizeof(stopwatchElapsed),
        .settings = 0,
        .permissions = ATTS_PERMIT_READ,
    },

    /* Laps Count characteristics */
    {
        .pUuid = attChUuid,
        .pValue = stopwatchLapsCountCharacteristicsValue,
        .pLen = &stopwatchLapsCountCharacteristicsValueLength,
        .maxLen = sizeof(stopwatchLapsCountCharacteristicsValue),
        .settings = 0,
        .permissions = ATTS_PERMIT_READ,
    },
    {
        .pUuid = stopwatchLapsCountCharacteristicsGuid,
        .pValue = &stopwatchLapsCount,
        .pLen = &stopwatchLapsCountLength,
        .maxLen = sizeof(stopwatchLapsCount),
        .settings = 0,
        .permissions = ATTS_PERMIT_READ,
    },
    {
        .pUuid = attCliChCfgUuid,
        .pValue = stopwatchLapsCountCcc,
        .pLen = &stopwatchLapsCountCccLength,
        .maxLen = sizeof(stopwatchLapsCountCcc),
        .settings = ATTS_SET_CCC,
        .permissions = ATTS_PERMIT_READ | ATTS_PERMIT_WRITE,
    },

    /* Laps select characteristics */
    {
        .pUuid = attChUuid,
        .pValue = stopwatchLapSelectCharacteristicsValue,
        .pLen = &stopwatchLapSelectCharacteristicsValueLength,
        .maxLen = sizeof(stopwatchLapSelectCharacteristicsValue),
        .settings = ATTS_SET_WRITE_CBACK,
        .permissions = ATTS_PERMIT_READ | ATTS_PERMIT_WRITE,
    },
    {
        .pUuid = stopwatchLapSelectCharacteristicsGuid,
        .pValue = &stopwatchLapSelect,
        .pLen = &stopwatchLapSelectLength,
        .maxLen = sizeof(stopwatchLapSelect),
        .settings = ATTS_SET_WRITE_CBACK,
        .permissions = ATTS_PERMIT_READ | ATTS_PERMIT_WRITE,
    },

    /* Laps time characteristics */
    {
        .pUuid = attChUuid,
        .pValue = stopwatchLapTimeCharacteristicsValue,
        .pLen = &stopwatchLapTimeCharacteristicsValueLength,
        .maxLen = sizeof(stopwatchLapTimeCharacteristicsValue),
        .settings = 0,
        .permissions = ATTS_PERMIT_READ,
    },
    {
        .pUuid = stopwatchLapTimeCharacteristicsGuid,
        .pValue = (uint8_t *)&stopwatchLapTime,
        .pLen = &stopwatchLapTimeLength,
        .maxLen = sizeof(stopwatchLapTime),
        .settings = 0,
        .permissions = ATTS_PERMIT_READ,
    },
};

static attsGroup_t stopwatchGroup = {
    .pNext = NULL,
    .pAttr = stopwatchAttributes,
    .readCback = NULL,
    .writeCback = BLE_StopwatchWriteCallback,
    .startHandle = STOPWATCH_HANDLE_OFFSET,
    .endHandle = STOPWATCH_LAST_HANDLE - 1,
};

enum {
    GATT_SC_CCC_IDX,
    STOPWATCH_STATUS_IDX,
    STOPWATCH_LAPS_COUNT_IDX,
    NUM_CCC_IDX
};

static const attsCccSet_t fitCccSet[NUM_CCC_IDX] = {
    {
        .handle = GATT_SC_CH_CCC_HDL,
        .valueRange = ATT_CLIENT_CFG_INDICATE,
        .secLevel = DM_SEC_LEVEL_NONE,
    },
    {
        .handle = STOPWATCH_STATUS_CCC_HANDLE,
        .valueRange = ATT_CLIENT_CFG_NOTIFY,
        .secLevel = DM_SEC_LEVEL_NONE,
    },
    {
        .handle = STOPWATCH_LAPS_COUNT_CCC_HANDLE,
        .valueRange = ATT_CLIENT_CFG_NOTIFY,
        .secLevel = DM_SEC_LEVEL_NONE,
    },
};

static LlRtCfg_t mainLlRtCfg;
static volatile int wutTrimComplete;

wsfHandlerId_t bleHandlerId;

static void BLE_InitWsf(void) {
    /* +12 for message headroom, + 2 event header, +255 maximum parameter length. */
    const uint16_t maxRptBufSize = 12 + 2 + 255;

    /* +12 for message headroom, +4 for header. */
    const uint16_t aclBufSize = 12 + mainLlRtCfg.maxAclLen + 4 + BB_DATA_PDU_TAILROOM;

    /* Adjust buffer allocation based on platform configuration. */
    poolDescriptions[2].len = maxRptBufSize;
    poolDescriptions[2].num = mainLlRtCfg.maxAdvReports;
    poolDescriptions[3].len = aclBufSize;
    poolDescriptions[3].num = mainLlRtCfg.numTxBufs + mainLlRtCfg.numRxBufs;

    const uint8_t numPools = sizeof(poolDescriptions) / sizeof(poolDescriptions[0]);

    uint16_t memUsed;
    memUsed = WsfBufInit(numPools, poolDescriptions);
    WsfHeapAlloc(memUsed);
    WsfOsInit();
    WsfTimerInit();
#if (WSF_TOKEN_ENABLED == TRUE) || (WSF_TRACE_ENABLED == TRUE)
    WsfTraceRegisterHandler(WsfBufIoWrite);
    WsfTraceEnable(TRUE);
#endif
}

void BLE_InitStack() {
    wsfHandlerId_t handlerId;

    SecInit();
    SecAesInit();
    SecCmacInit();
    SecEccInit();

    handlerId = WsfOsSetNextHandler(HciHandler);
    HciHandlerInit(handlerId);

    handlerId = WsfOsSetNextHandler(DmHandler);
    DmDevVsInit(0);
    DmConnInit();
    DmAdvInit();
    DmConnSlaveInit();
    DmSecInit();
    DmSecLescInit();
    DmPrivInit();
    DmHandlerInit(handlerId);

    handlerId = WsfOsSetNextHandler(L2cSlaveHandler);
    L2cSlaveHandlerInit(handlerId);
    L2cInit();
    L2cSlaveInit();

    handlerId = WsfOsSetNextHandler(AttHandler);
    AttHandlerInit(handlerId);
    AttsInit();
    AttsIndInit();

    handlerId = WsfOsSetNextHandler(SmpHandler);
    SmpHandlerInit(handlerId);
    SmprInit();
    SmprScInit();
    HciSetMaxRxAclLen(256);

    handlerId = WsfOsSetNextHandler(AppHandler);
    AppHandlerInit(handlerId);

    handlerId = WsfOsSetNextHandler(BLE_Handler);
    BLE_HandlerInit(handlerId);
}

void BLE_Init() {
    /* Configurations must be persistent. */
    static BbRtCfg_t mainBbRtCfg;

    PalBbLoadCfg((PalBbCfg_t *)&mainBbRtCfg);
    LlGetDefaultRunTimeCfg(&mainLlRtCfg);
#if (BT_VER >= LL_VER_BT_CORE_SPEC_5_0)
    /* Set 5.0 requirements. */
    mainLlRtCfg.btVer = LL_VER_BT_CORE_SPEC_5_0;
#endif
    PalCfgLoadData(PAL_CFG_ID_LL_PARAM, &mainLlRtCfg.maxAdvSets, sizeof(LlRtCfg_t) - 9);
#if (BT_VER >= LL_VER_BT_CORE_SPEC_5_0)
    PalCfgLoadData(PAL_CFG_ID_BLE_PHY, &mainLlRtCfg.phy2mSup, 4);
#endif

    /* Set the 32k sleep clock accuracy into one of the following bins, default is 20
      HCI_CLOCK_500PPM
      HCI_CLOCK_250PPM
      HCI_CLOCK_150PPM
      HCI_CLOCK_100PPM
      HCI_CLOCK_75PPM
      HCI_CLOCK_50PPM
      HCI_CLOCK_30PPM
      HCI_CLOCK_20PPM
    */
    mainBbRtCfg.clkPpm = 20;

    /* Set the default connection power level */
    mainLlRtCfg.defTxPwrLvl = 0;

    uint32_t memUsed;
    memUsed = WsfBufIoUartInit(WsfHeapGetFreeStartAddress(), 2048);
    WsfHeapAlloc(memUsed);

    BLE_InitWsf();
    AppTerminalInit();

    LlInitRtCfg_t llCfg = {.pBbRtCfg = &mainBbRtCfg,
                           .wlSizeCfg = 4,
                           .rlSizeCfg = 4,
                           .plSizeCfg = 4,
                           .pLlRtCfg = &mainLlRtCfg,
                           .pFreeMem = WsfHeapGetFreeStartAddress(),
                           .freeMemAvail = WsfHeapCountAvailable()};

    memUsed = LlInit(&llCfg);
    WsfHeapAlloc(memUsed);

    bdAddr_t bdAddr;
    PalCfgLoadData(PAL_CFG_ID_BD_ADDR, bdAddr, sizeof(bdAddr_t));
    LlSetBdAddr((uint8_t *)&bdAddr);

    /* Start the 32 MHz crystal and the BLE DBB counter to trim the 32 kHz crystal */
    PalBbEnable();

    /* Output buffered square wave of 32 kHz clock to GPIO */
    MXC_RTC_SquareWaveStart(MXC_RTC_F_32KHZ);

    /* Execute the trim procedure */
    wutTrimComplete = 0;
    MXC_WUT_TrimCrystalAsync(wutTrimCb);
    while (!wutTrimComplete) {
    }

    /* Shutdown the 32 MHz crystal and the BLE DBB */
    PalBbDisable();

    BLE_InitStack();
    BLE_Start();
}

static void BLE_HandlerInit(wsfHandlerId_t handlerId) {
    APP_TRACE_INFO0("FitHandlerInit");

    /* store handler ID */
    bleHandlerId = handlerId;

    /* Set configuration pointers */
    pAppAdvCfg = (appAdvCfg_t *)&fitAdvCfg;
    pAppSlaveCfg = (appSlaveCfg_t *)&fitSlaveCfg;
    pAppSecCfg = (appSecCfg_t *)&fitSecCfg;
    pAppUpdateCfg = (appUpdateCfg_t *)&fitUpdateCfg;

    /* Initialize application framework */
    AppSlaveInit();
    AppServerInit();

    /* Set stack configuration pointers */
    pSmpCfg = (smpCfg_t *)&fitSmpCfg;
}

static void BLE_Handler(wsfEventMask_t event, wsfMsgHdr_t *pMsg) {
    if (pMsg != NULL) {
        APP_TRACE_INFO1("Fit got evt %d", pMsg->event);

        /* process ATT messages */
        if (pMsg->event >= ATT_CBACK_START && pMsg->event <= ATT_CBACK_END) {
            /* process server-related ATT messages */
            AppServerProcAttMsg(pMsg);
        } else if (pMsg->event >= DM_CBACK_START && pMsg->event <= DM_CBACK_END) {
            /* process DM messages */
            /* process advertising and connection-related messages */
            AppSlaveProcDmMsg((dmEvt_t *)pMsg);

            /* process security-related messages */
            AppSlaveSecProcDmMsg((dmEvt_t *)pMsg);
        }

        /* perform profile and user interface-related operations */
        fitProcMsg(pMsg);
    }
}

static void BLE_Start() {
    /* Register for stack callbacks */
    DmRegister(fitDmCback);
    DmConnRegister(DM_CLIENT_ID_APP, fitDmCback);
    AttRegister(fitAttCback);
    AttConnRegister(AppServerConnCback);
    AttsCccRegister(NUM_CCC_IDX, (attsCccSet_t *)fitCccSet, fitCccCback);

    /* Initialize attribute server database */
    SvcCoreGattCbackRegister(GattReadCback, GattWriteCback);
    SvcCoreAddGroup();

    AttsAddGroup(&stopwatchGroup);

    /* Set Service Changed CCCD index. */
    GattSetSvcChangedIdx(GATT_SC_CCC_IDX);

    /* Reset the device */
    DmDevReset();
}

void WUT_IRQHandler(void) {
    MXC_WUT_Handler();
}

void wutTrimCb(int err) {
    if (err != E_NO_ERROR) {
        APP_TRACE_INFO1("32 kHz trim error %d\n", err);
    } else {
        APP_TRACE_INFO1("32kHz trimmed to 0x%x", (MXC_TRIMSIR->rtc & MXC_F_TRIMSIR_RTC_X1TRIM) >>
                                                     MXC_F_TRIMSIR_RTC_X1TRIM_POS);
    }
    wutTrimComplete = 1;
}

static void fitDmCback(dmEvt_t *pDmEvt) {
    dmEvt_t *pMsg;
    uint16_t len = DmSizeOfEvt(pDmEvt);

    if ((pMsg = WsfMsgAlloc(len)) != NULL) {
        memcpy(pMsg, pDmEvt, len);
        WsfMsgSend(bleHandlerId, pMsg);
    }
}

static void fitAttCback(attEvt_t *pEvt) {
    attEvt_t *pMsg;

    if ((pMsg = WsfMsgAlloc(sizeof(attEvt_t) + pEvt->valueLen)) != NULL) {
        memcpy(pMsg, pEvt, sizeof(attEvt_t));
        pMsg->pValue = (uint8_t *)(pMsg + 1);
        memcpy(pMsg->pValue, pEvt->pValue, pEvt->valueLen);
        WsfMsgSend(bleHandlerId, pMsg);
    }
}

static void fitCccCback(attsCccEvt_t *pEvt) {
    attsCccEvt_t *pMsg;
    appDbHdl_t dbHdl;

    /* If CCC not set from initialization and there's a device record and currently bonded */
    if ((pEvt->handle != ATT_HANDLE_NONE) &&
        ((dbHdl = AppDbGetHdl((dmConnId_t)pEvt->hdr.param)) != APP_DB_HDL_NONE) &&
        AppCheckBonded((dmConnId_t)pEvt->hdr.param)) {
        /* Store value in device database. */
        AppDbSetCccTblValue(dbHdl, pEvt->idx, pEvt->value);
    }

    if ((pMsg = WsfMsgAlloc(sizeof(attsCccEvt_t))) != NULL) {
        memcpy(pMsg, pEvt, sizeof(attsCccEvt_t));
        WsfMsgSend(bleHandlerId, pMsg);
    }
}

static void fitSetup() {
    AppAdvSetData(APP_ADV_DATA_DISCOVERABLE, sizeof(fitAdvDataDisc), (uint8_t *)fitAdvDataDisc);
    AppAdvSetData(APP_SCAN_DATA_DISCOVERABLE, sizeof(fitScanDataDisc), (uint8_t *)fitScanDataDisc);

    AppAdvSetData(APP_ADV_DATA_CONNECTABLE, 0, NULL);
    AppAdvSetData(APP_SCAN_DATA_CONNECTABLE, 0, NULL);

    AppAdvStart(APP_MODE_AUTO_INIT);
}

static void fitProcMsg(wsfMsgHdr_t *pMsg) {
    switch (pMsg->event) {
        case ATTS_CCC_STATE_IND:
            fitProcCccState(pMsg);
            break;

        case DM_RESET_CMPL_IND:
            AttsCalculateDbHash();
            DmSecGenerateEccKeyReq();
            fitSetup();
            break;

        case DM_CONN_CLOSE_IND:
            fitClose(pMsg);
            break;

        case DM_SEC_PAIR_CMPL_IND:
            DmSecGenerateEccKeyReq();
            break;

        case DM_SEC_PAIR_FAIL_IND:
            DmSecGenerateEccKeyReq();
            break;

        case DM_SEC_AUTH_REQ_IND: {
            dmEvt_t *dme = (dmEvt_t *)pMsg;
            AppHandlePasskey(&dme->authReq);
            break;
        }
        case DM_SEC_ECC_KEY_IND: {
            dmEvt_t *dme = (dmEvt_t *)pMsg;
            DmSecSetEccKey(&dme->eccMsg.data.key);
            break;
        }
        case DM_SEC_COMPARE_IND: {
            dmEvt_t *dme = (dmEvt_t *)pMsg;
            AppHandleNumericComparison(&dme->cnfInd);
            break;
        }
        case DM_PRIV_CLEAR_RES_LIST_IND:
            APP_TRACE_INFO1("Clear resolving list status 0x%02x", pMsg->status);
            break;

        default:
            break;
    }
}

static void fitClose(wsfMsgHdr_t *msg) {
}

static void fitProcCccState(wsfMsgHdr_t *msg) {
    attsCccEvt_t *ccce = (attsCccEvt_t *)msg;
    APP_TRACE_INFO3("ccc state ind value:%d handle:%d idx:%d", ccce->value, ccce->handle, ccce->idx);
}

static uint8_t BLE_StopwatchWriteCallback(dmConnId_t connId, uint16_t handle, uint8_t operation, uint16_t offset, uint16_t len, uint8_t *pValue, attsAttr_t *pAttr) {
    APP_TRACE_INFO0("BLE_StopwatchWriteCallback");

    AttsSetAttr(handle, 1, pValue);
    return ATT_SUCCESS;
}