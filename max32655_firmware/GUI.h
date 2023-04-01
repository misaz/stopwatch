#ifndef GUI_H
#define GUI_H

#include <stdint.h>

void GUI_Init();
void GUI_HandleButtonPress(int buttonNumber, uint32_t pressTime);
void GUI_SetBleAdvertisignStatus(int isAdvertisign);
void GUI_SetBleConnectionStatus(int isConnected);
uint32_t GUI_GetLapTime(uint8_t lapNumber);

#endif