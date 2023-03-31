#ifndef BLE_H
#define BLE_H

#include <stdint.h>

void BLE_Init();
void BLE_LapCountChanged(uint8_t newLapsCount);
void BLE_SetCurrentTime(uint32_t currentTime);
void BLE_SetStatus(uint8_t status);

#endif