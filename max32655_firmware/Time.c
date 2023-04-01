/* project */
#include "time.h"

/* max32655 + mbed + cordio */
#include <max32655.h>
#include <tmr.h>
#include <wsf_trace.h>

void Time_Init() {
    int status;

    mxc_tmr_cfg_t cfg;
    cfg.pres = TMR_PRES_1;
    cfg.mode = TMR_MODE_CONTINUOUS;
    cfg.bitMode = TMR_BIT_MODE_32;
    cfg.clock = MXC_TMR_32K_CLK;
    cfg.cmp_cnt = 0xFFFFFFFF;
    cfg.pol = 0;

    status = MXC_TMR_Init(TIME_TIMER, &cfg, FALSE);
    if (status) {
        APP_TRACE_ERR1("MXC_TMR_Init failed with status code %d", status);
        return;
    }

    MXC_TMR_Start(TIME_TIMER);
}