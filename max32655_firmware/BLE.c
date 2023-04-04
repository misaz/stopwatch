/* self */
#include "BLE.h"

/* project */
#include "GUI.h"

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

void BLE_WakeupTimerTrimCallback(int err);
static void BLE_DeviceManagementCallback(dmEvt_t *pDmEvt);
static void BLE_AttCallback(attEvt_t *pEvt);
static void BLE_CccCallback(attsCccEvt_t *pEvt);
static void BLE_Handler(wsfEventMask_t event, wsfMsgHdr_t *pMsg);
static void BLE_Start();
static void BLE_ProcessMessage(wsfMsgHdr_t *pMsg);
static void BLE_HandlerInit(wsfHandlerId_t handlerId);
static uint8_t BLE_StopwatchWriteCallback(dmConnId_t connId, uint16_t handle, uint8_t operation, uint16_t offset, uint16_t len, uint8_t *pValue, attsAttr_t *pAttr);

static wsfBufPoolDesc_t memoryPoolDescriptors[] = {
    {.len = 16, .num = 8},
    {.len = 32, .num = 4},
    {.len = 192, .num = 4},
    {.len = 256, .num = 4},
    {.len = 512, .num = 4},
    {.len = 0, .num = 0},  // item values loadded dynamicaly in init function
    {.len = 0, .num = 0},  // item values loadded dynamicaly in init function
};

static const smpCfg_t secureManagerConfig = {
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

static const appAdvCfg_t advertisignConfig = {
    .advDuration = {60000, 0, 0},
    .advInterval = {800, 0, 0},
};

static const appSlaveCfg_t slaveConfig = {
    .connMax = 1,
};

static const appSecCfg_t securityConfig = {
    .auth = DM_AUTH_BOND_FLAG | DM_AUTH_SC_FLAG,
    .iKeyDist = 0,
    .rKeyDist = DM_KEY_DIST_LTK,
    .oob = FALSE,
    .initiateSec = TRUE,
};

static const appUpdateCfg_t updateConfig = {
    .idlePeriod = 6000,
    .connIntervalMin = 640,
    .connIntervalMax = 800,
    .connLatency = 0,
    .supTimeout = 900,
    .maxAttempts = 5,
};

static const uint8_t avertisignData[] = {
    /*! flags */
    2,                                                  /*! length */
    DM_ADV_TYPE_FLAGS,                                  /*! AD type */
    DM_FLAG_LE_GENERAL_DISC | DM_FLAG_LE_BREDR_NOT_SUP, /*! flags */

    /*! tx power */
    2,                    /*! length */
    DM_ADV_TYPE_TX_POWER, /*! AD type */
    0,                    /*! tx power */
};

static const uint8_t scanData[] = {
    /*! device name */
    16,                     /*! length */
    DM_ADV_TYPE_LOCAL_NAME, /*! AD type */
    'M',
    'i',
    's',
    'a',
    'z',
    ' ',
    'S',
    't',
    'o',
    'p',
    'w',
    'a',
    't',
    'c',
    'h',
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
    STOPWATCH_STATUS_CHARACTERISTICS_NAME_HANDLE,

    STOPWATCH_ELAPSED_CHARACTERISTICS_HANDLE,
    STOPWATCH_ELAPSED_VALUE_HANDLE,
    STOPWATCH_ELAPSED_CHARACTERISTICS_NAME_HANDLE,

    STOPWATCH_LAPS_COUNT_CHARACTERISTICS_HANDLE,
    STOPWATCH_LAPS_COUNT_VALUE_HANDLE,
    STOPWATCH_LAPS_COUNT_CCC_HANDLE,
    STOPWATCH_LAPS_COUNT_CHARACTERISTICS_NAME_HANDLE,

    STOPWATCH_LAP_SELECT_CHARACTERISTICS_HANDLE,
    STOPWATCH_LAP_SELECT_VALUE_HANDLE,
    STOPWATCH_LAP_SELECT_CHARACTERISTICS_NAME_HANDLE,

    STOPWATCH_LAP_TIME_CHARACTERISTICS_HANDLE,
    STOPWATCH_LAP_TIME_VALUE_HANDLE,
    STOPWATCH_LAP_TIME_CHARACTERISTICS_NAME_HANDLE,

    STOPWATCH_LAST_HANDLE
};

static uint8_t stopwatchServiceGuid[ATT_128_UUID_LEN] = {STOPWATCH_SERVICE_GUID};
static uint8_t stopwatchStatusCharacteristicsGuid[ATT_128_UUID_LEN] = {STOPWATCH_STATUS_CHARACTERISTICS_GUID};
static uint8_t stopwatchElapsedCharacteristicsGuid[ATT_128_UUID_LEN] = {STOPWATCH_ELAPSED_CHARACTERISTICS_GUID};
static uint8_t stopwatchLapsCountCharacteristicsGuid[ATT_128_UUID_LEN] = {STOPWATCH_LAPS_COUNT_CHARACTERISTICS_GUID};
static uint8_t stopwatchLapSelectCharacteristicsGuid[ATT_128_UUID_LEN] = {STOPWATCH_LAP_SELECT_CHARACTERISTICS_GUID};
static uint8_t stopwatchLapTimeCharacteristicsGuid[ATT_128_UUID_LEN] = {STOPWATCH_LAP_TIME_CHARACTERISTICS_GUID};

static uint16_t stopwatchServiceGuidLength = sizeof(stopwatchServiceGuid);

static uint8_t stopwatchStatusName[] = {'S', 't', 'a', 't', 'u', 's'};
static uint16_t stopwatchStatusNameLength = sizeof(stopwatchStatusName);

static uint8_t stopwatchElapsedName[] = {'E', 'l', 'a', 'p', 's', 'e', 'd', ' ', 'T', 'i', 'm', 'e'};
static uint16_t stopwatchElapsedNameLength = sizeof(stopwatchElapsedName);

static uint8_t stopwatchLapsCountName[] = {'L', 'a', 'p', 's', ' ', 'C', 'o', 'u', 'n', 't'};
static uint16_t stopwatchLapsCountNameLength = sizeof(stopwatchLapsCountName);

static uint8_t stopwatchLapSelectName[] = {'L', 'a', 'p', ' ', 'S', 'e', 'l', 'e', 'c', 't'};
static uint16_t stopwatchLapSelectNameLength = sizeof(stopwatchLapSelectName);

static uint8_t stopwatchLapTimeName[] = {'L', 'a', 'p', ' ', 'T', 'i', 'm', 'e'};
static uint16_t stopwatchLapTimeNameLength = sizeof(stopwatchLapTimeName);

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
    {
        .pUuid = attChUserDescUuid,
        .pValue = stopwatchStatusName,
        .pLen = &stopwatchStatusNameLength,
        .maxLen = sizeof(stopwatchStatusName),
        .settings = 0,
        .permissions = ATTS_PERMIT_READ,
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
    {
        .pUuid = attChUserDescUuid,
        .pValue = stopwatchElapsedName,
        .pLen = &stopwatchElapsedNameLength,
        .maxLen = sizeof(stopwatchElapsedName),
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
    {
        .pUuid = attChUserDescUuid,
        .pValue = stopwatchLapsCountName,
        .pLen = &stopwatchLapsCountNameLength,
        .maxLen = sizeof(stopwatchLapsCountName),
        .settings = 0,
        .permissions = ATTS_PERMIT_READ,
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
    {
        .pUuid = attChUserDescUuid,
        .pValue = stopwatchLapSelectName,
        .pLen = &stopwatchLapSelectNameLength,
        .maxLen = sizeof(stopwatchLapSelectName),
        .settings = 0,
        .permissions = ATTS_PERMIT_READ,
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
    {
        .pUuid = attChUserDescUuid,
        .pValue = stopwatchLapTimeName,
        .pLen = &stopwatchLapTimeNameLength,
        .maxLen = sizeof(stopwatchLapTimeName),
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

static const attsCccSet_t cccSet[NUM_CCC_IDX] = {
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
    // initialization logic inspired in Maxim sample

    // 269 come from Maxim example
    memoryPoolDescriptors[5].len = 269;
    memoryPoolDescriptors[5].num = mainLlRtCfg.maxAdvReports;

    // expression come from Maxim example
    memoryPoolDescriptors[6].len = 16 + mainLlRtCfg.maxAclLen + BB_DATA_PDU_TAILROOM;
    memoryPoolDescriptors[6].num = mainLlRtCfg.numTxBufs + mainLlRtCfg.numRxBufs;

    const uint8_t memoryPoolsCount = sizeof(memoryPoolDescriptors) / sizeof(*memoryPoolDescriptors);

    uint16_t memUsed;
    memUsed = WsfBufInit(memoryPoolsCount, memoryPoolDescriptors);
    WsfHeapAlloc(memUsed);

    WsfOsInit();
    WsfTimerInit();
#if (WSF_TOKEN_ENABLED == TRUE) || (WSF_TRACE_ENABLED == TRUE)
    WsfTraceRegisterHandler(WsfBufIoWrite);
    WsfTraceEnable(TRUE);
#endif
}

void BLE_InitStack() {
    // initialization logic inspired in Maxim sample

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
    // initialization logic inspired in Maxim sample

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

    mainBbRtCfg.clkPpm = 20;

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

    PalBbEnable();

    MXC_RTC_SquareWaveStart(MXC_RTC_F_32KHZ);

    wutTrimComplete = 0;
    MXC_WUT_TrimCrystalAsync(BLE_WakeupTimerTrimCallback);
    while (!wutTrimComplete) {
    }

    PalBbDisable();

    BLE_InitStack();
    BLE_Start();
}

static void BLE_HandlerInit(wsfHandlerId_t handlerId) {
    APP_TRACE_INFO0("BLE_HandlerInit");

    bleHandlerId = handlerId;

    pAppAdvCfg = (appAdvCfg_t *)&advertisignConfig;
    pAppSlaveCfg = (appSlaveCfg_t *)&slaveConfig;
    pAppSecCfg = (appSecCfg_t *)&securityConfig;
    pAppUpdateCfg = (appUpdateCfg_t *)&updateConfig;

    AppSlaveInit();
    AppServerInit();

    pSmpCfg = (smpCfg_t *)&secureManagerConfig;
}

static void BLE_Handler(wsfEventMask_t event, wsfMsgHdr_t *pMsg) {
    if (pMsg != NULL) {
        APP_TRACE_INFO1("Processing event %d", pMsg->event);

        if (pMsg->event >= ATT_CBACK_START && pMsg->event <= ATT_CBACK_END) {
            AppServerProcAttMsg(pMsg);
        } else if (pMsg->event >= DM_CBACK_START && pMsg->event <= DM_CBACK_END) {
            AppSlaveProcDmMsg((dmEvt_t *)pMsg);
            AppSlaveSecProcDmMsg((dmEvt_t *)pMsg);
        }

        BLE_ProcessMessage(pMsg);
    }
}

static void BLE_Start() {
    // start logic inspired in Maxim sample

    DmRegister(BLE_DeviceManagementCallback);
    DmConnRegister(DM_CLIENT_ID_APP, BLE_DeviceManagementCallback);
    AttRegister(BLE_AttCallback);
    AttConnRegister(AppServerConnCback);
    AttsCccRegister(NUM_CCC_IDX, (attsCccSet_t *)cccSet, BLE_CccCallback);

    SvcCoreGattCbackRegister(GattReadCback, GattWriteCback);
    SvcCoreAddGroup();

    AttsAddGroup(&stopwatchGroup);

    GattSetSvcChangedIdx(GATT_SC_CCC_IDX);

    DmDevReset();
}

void WUT_IRQHandler(void) {
    MXC_WUT_Handler();
}

void BLE_WakeupTimerTrimCallback(int err) {
    if (err != E_NO_ERROR) {
        APP_TRACE_INFO1("WUT Timer trimming failed with status code %d\n", err);
        return;
    }

    APP_TRACE_INFO1("Wakeup timer was sucessfully trimmed. Trim value 0x%x", (MXC_TRIMSIR->rtc & MXC_F_TRIMSIR_RTC_X1TRIM) >> MXC_F_TRIMSIR_RTC_X1TRIM_POS);
    wutTrimComplete = 1;
}

static void BLE_DeviceManagementCallback(dmEvt_t *pDmEvt) {
    dmEvt_t *pMsg;
    uint16_t len = DmSizeOfEvt(pDmEvt);

    if ((pMsg = WsfMsgAlloc(len)) != NULL) {
        memcpy(pMsg, pDmEvt, len);
        WsfMsgSend(bleHandlerId, pMsg);
    }
}

static void BLE_AttCallback(attEvt_t *pEvt) {
    attEvt_t *pMsg;

    if ((pMsg = WsfMsgAlloc(sizeof(attEvt_t) + pEvt->valueLen)) != NULL) {
        memcpy(pMsg, pEvt, sizeof(attEvt_t));
        pMsg->pValue = (uint8_t *)(pMsg + 1);
        memcpy(pMsg->pValue, pEvt->pValue, pEvt->valueLen);
        WsfMsgSend(bleHandlerId, pMsg);
    }
}

static void BLE_CccCallback(attsCccEvt_t *pEvt) {
    attsCccEvt_t *pMsg;
    appDbHdl_t dbHdl;

    if ((pEvt->handle != ATT_HANDLE_NONE) &&
        ((dbHdl = AppDbGetHdl((dmConnId_t)pEvt->hdr.param)) != APP_DB_HDL_NONE) &&
        AppCheckBonded((dmConnId_t)pEvt->hdr.param)) {
        AppDbSetCccTblValue(dbHdl, pEvt->idx, pEvt->value);
    }

    if ((pMsg = WsfMsgAlloc(sizeof(attsCccEvt_t))) != NULL) {
        memcpy(pMsg, pEvt, sizeof(attsCccEvt_t));
        WsfMsgSend(bleHandlerId, pMsg);
    }
}

static void BLE_SetupAdvertising() {
    AppAdvSetData(APP_ADV_DATA_DISCOVERABLE, sizeof(avertisignData), (uint8_t *)avertisignData);
    AppAdvSetData(APP_SCAN_DATA_DISCOVERABLE, sizeof(scanData), (uint8_t *)scanData);

    AppAdvSetData(APP_ADV_DATA_CONNECTABLE, 0, NULL);
    AppAdvSetData(APP_SCAN_DATA_CONNECTABLE, 0, NULL);

    AppAdvStart(APP_MODE_AUTO_INIT);
}

static void BLE_ProcessMessage(wsfMsgHdr_t *pMsg) {
    switch (pMsg->event) {
        case ATTS_CCC_STATE_IND: {
            attsCccEvt_t *cccEvent = (attsCccEvt_t *)pMsg;
            APP_TRACE_INFO3("CCC (id=%d, handle=%d) changed state to 0x%02x", cccEvent->idx, cccEvent->handle, cccEvent->value);
            break;
        }

        case DM_RESET_CMPL_IND:
            AttsCalculateDbHash();
            DmSecGenerateEccKeyReq();
            BLE_SetupAdvertising();
            break;

        case DM_ADV_START_IND:
            GUI_SetBleAdvertisignStatus(1);
            break;

        case DM_ADV_STOP_IND:
            GUI_SetBleAdvertisignStatus(0);
            break;

        case DM_CONN_OPEN_IND:
            GUI_SetBleConnectionStatus(1);
            break;

        case DM_CONN_CLOSE_IND:
            GUI_SetBleConnectionStatus(0);
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

static uint8_t BLE_StopwatchWriteCallback(dmConnId_t connId, uint16_t handle, uint8_t operation, uint16_t offset, uint16_t len, uint8_t *pValue, attsAttr_t *pAttr) {
    uint8_t status;

    if (handle == STOPWATCH_LAP_SELECT_VALUE_HANDLE) {
        if (len < 1) {
            return ATT_ERR_LENGTH;
        }

        uint32_t time = GUI_GetLapTime(*pValue);

        status = AttsSetAttr(STOPWATCH_LAP_TIME_VALUE_HANDLE, sizeof(time), (uint8_t *)&time);
        if (status) {
            APP_TRACE_ERR1("Setting lap time attribute failed with status code 0x%02x", status);
            return ATT_ERR_UNLIKELY;
        }

        status = AttsSetAttr(STOPWATCH_LAP_SELECT_VALUE_HANDLE, sizeof(uint8_t), pValue);
        if (status) {
            APP_TRACE_ERR1("Setting lap select attribute failed with status code 0x%02x", status);
            return ATT_ERR_UNLIKELY;
        }

        return ATT_SUCCESS;
    }

    return ATT_ERR_NOT_FOUND;
}

void BLE_LapCountChanged(uint8_t newLapsCount) {
    uint8_t status;

    status = AttsSetAttr(STOPWATCH_STATUS_VALUE_HANDLE, sizeof(newLapsCount), (uint8_t *)&newLapsCount);
    if (status) {
        APP_TRACE_ERR1("Error while laps count value. Status 0x%02x", status);
        return;
    }

    dmConnId_t connId = AppConnIsOpen();
    if (stopwatchLapsCount != newLapsCount && connId != DM_CONN_ID_NONE && AttsCccEnabled(connId, STOPWATCH_LAPS_COUNT_IDX)) {
        AttsHandleValueNtf(connId, STOPWATCH_LAPS_COUNT_VALUE_HANDLE, sizeof(newLapsCount), (uint8_t *)&newLapsCount);
    }

    stopwatchLapsCount = newLapsCount;
}

void BLE_SetCurrentTime(uint32_t time) {
    uint8_t status;

    status = AttsSetAttr(STOPWATCH_ELAPSED_VALUE_HANDLE, sizeof(time), (uint8_t *)&time);
    if (status) {
        APP_TRACE_ERR1("Error while setting current elapsed time. Status 0x%02x", status);
        return;
    }

    stopwatchElapsed = time;
}

void BLE_SetStatus(uint8_t newStatus) {
    uint8_t status;

    status = AttsSetAttr(STOPWATCH_STATUS_VALUE_HANDLE, sizeof(newStatus), &newStatus);
    if (status) {
        APP_TRACE_ERR1("Error while setting status value. Status 0x%02x", status);
        return;
    }

    dmConnId_t connId = AppConnIsOpen();
    if (connId != DM_CONN_ID_NONE && AttsCccEnabled(connId, STOPWATCH_STATUS_IDX)) {
        AttsHandleValueNtf(connId, STOPWATCH_STATUS_VALUE_HANDLE, sizeof(newStatus), (uint8_t *)&newStatus);
    }

    stopwatchStatus = newStatus;
}