#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

#define DISPLAY_WIDTH 64
#define DISPLAY_LINES 6

void Display_Init();
void Display_SetPixelBuffer(int x, int row, uint8_t value);
void Display_OrPixelBuffer(int x, int row, uint8_t value);
void Display_InvertPixelBuffer(int x, int row);
void Display_ShiftLeftPixelBuffer(int x, int row, int shift);
void Display_ShiftRightPixelBuffer(int x, int row, int shift);
void Display_Show();
void Display_Clear();
int Display_PrintChar(int x, int row, char ch);
int Display_PrintString(int x, int row, char *str);
int Display_GetCharLength(char ch);
int Display_GetStringLength(char *str);

#endif