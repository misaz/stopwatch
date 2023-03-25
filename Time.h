#ifndef TIME_H
#define TIME_H

#include <max32655.h>
#include <tmr.h>

#define TIME_TICK_PER_SEC 32678
#define TIME_TICK_PER_MSEC 33
#define TIME_TIMER MXC_TMR3

void Time_Init();

#endif