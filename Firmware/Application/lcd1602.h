#ifndef LCD1602_H
#define LCD1602_H

#include "stdint.h"

uint8_t LCD_BusReady(void);
void LCD_Init(void);
void LCD_Clear(void);
void LCD_SetCursor(uint8_t row, uint8_t column);
void LCD_WriteChar(char value);
void LCD_WriteString(const char *text);
void LCD_PrintLine(uint8_t row, const char *text);
void LCD_Printf(uint8_t row, const char *format, ...);

#endif

