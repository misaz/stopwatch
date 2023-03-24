#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

void Display_Init();
void Display_SetPixelBuffer(int x, int row, uint8_t value);
void Display_Show();
void Display_Clear();
int Display_PrintChar(int x, int row, char ch);
int Display_PrintString(int x, int row, char *str);

#endif