#include "flash_log.h"

#include <string.h>

#include "app_config.h"

typedef char FlashLogRecordMustBe16Bytes[(sizeof(LogRecord_t) == 16U) ? 1 : -1];

static const LogRecord_t *const s_page =
    (const LogRecord_t *)LOG_FLASH_ADDRESS;
static uint8_t s_count;
static uint8_t s_next;
static uint8_t s_needRepair;

static uint8_t Record_IsValid(const LogRecord_t *record)
{
    return (record->magic == LOG_RECORD_MAGIC &&
            record->state <= (uint8_t)WSTATE_FAULT &&
            record->fault <= (uint8_t)FAULT_SPIN_STOP_TIMEOUT &&
            record->program < (uint8_t)PROGRAM_COUNT &&
            record->event <= (uint8_t)LOG_EVENT_RESET)
               ? 1U
               : 0U;
}

static uint8_t FlashLog_EraseAndStart(void)
{
    FLASH_Status status;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR |
                    FLASH_FLAG_WRPRTERR);
    status = FLASH_ErasePage(LOG_FLASH_ADDRESS);
    FLASH_Lock();
    if (status != FLASH_COMPLETE)
        return 0U;

    s_count = 0U;
    s_next = 0U;
    s_needRepair = 0U;
    return 1U;
}

/* 扫描日志页并恢复有效记录数量，识别中断写入造成的异常槽位。 */
void FlashLog_Init(void)
{
    uint8_t index;

    s_count = 0U;
    s_next = 0U;
    s_needRepair = 0U;

    for (index = 0U; index < LOG_MAX_RECORDS; ++index)
    {
        if (Record_IsValid(&s_page[index]) == 0U)
        {
            break;
        }
        ++s_count;
    }

    s_next = s_count;
    if (s_next < LOG_MAX_RECORDS)
    {
        /* 若下一个空槽并非已擦除状态，说明上次写入可能中断；记录修复标志，待下次追加日志前擦除。 */
        uint16_t magic = *(const volatile uint16_t *)&s_page[s_next].magic;
        if (magic != 0xFFFFU)
            s_needRepair = 1U;
    }
    else
    {
        s_needRepair = 1U;
    }
}

/* 追加一条日志；页满或需要修复时擦除当前页后重用。 */
uint8_t FlashLog_Append(const LogRecord_t *record)
{
    uint32_t address;
    const uint32_t *words;
    uint8_t wordIndex;
    FLASH_Status status;

    if (record == 0)
    {
        return 0U;
    }

    if (s_needRepair != 0U)
    {
        if (FlashLog_EraseAndStart() == 0U)
            return 0U;
    }
    if (s_next >= LOG_MAX_RECORDS)
    {
        if (FlashLog_EraseAndStart() == 0U)
            return 0U;
    }

    address = LOG_FLASH_ADDRESS + ((uint32_t)s_next * sizeof(LogRecord_t));
    words = (const uint32_t *)record;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR |
                    FLASH_FLAG_WRPRTERR);
    status = FLASH_COMPLETE;
    for (wordIndex = 0U; wordIndex < 4U; ++wordIndex)
    {
        status = FLASH_ProgramWord(address + ((uint32_t)wordIndex * 4U),
                                   words[wordIndex]);
        if (status != FLASH_COMPLETE)
            break;
    }
    FLASH_Lock();

    if (status != FLASH_COMPLETE)
        return 0U;

    ++s_next;
    if (s_count < LOG_MAX_RECORDS)
        ++s_count;
    return 1U;
}

/* 按当前页的记录顺序读出有效日志。 */
uint8_t FlashLog_ReadAll(LogRecord_t *records, uint8_t capacity)
{
    uint8_t count = s_count < capacity ? s_count : capacity;
    uint8_t index;

    if (records == 0)
        return 0U;
    for (index = 0U; index < count; ++index)
    {
        records[index] = s_page[index];
    }
    return count;
}

LogRecord_t FlashLog_Last(void)
{
    LogRecord_t empty;

    memset(&empty, 0, sizeof(empty));
    if (s_count == 0U)
        return empty;
    return s_page[s_count - 1U];
}

uint8_t FlashLog_Clear(void)
{
    return FlashLog_EraseAndStart();
}
