#ifndef FLASH_LOG_H
#define FLASH_LOG_H

#include "washer_types.h"

void FlashLog_Init(void);
uint8_t FlashLog_Append(const LogRecord_t *record);
uint8_t FlashLog_ReadAll(LogRecord_t *records, uint8_t capacity);
LogRecord_t FlashLog_Last(void);
uint8_t FlashLog_Clear(void);

#endif

