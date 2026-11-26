#include "app_tasks.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "app_config.h"
#include "bsp.h"
#include "flash_log.h"
#include "lcd1602.h"
#include "washer_control.h"
#include "washer_plant.h"
#include "washer_types.h"

typedef struct
{
    SensorInput_t sensor;
    PlantState_t plant;
    UiSnapshot_t ui;
    LogRecord_t lastLog;
} SharedState_t;

static SharedState_t s_shared;
static xSemaphoreHandle s_sharedMutex;
static xQueueHandle s_sensorQueue;
static xQueueHandle s_actuatorQueue;
static xQueueHandle s_eventQueue;
static volatile uint32_t s_uptimeMs;
static xQueueHandle s_commandQueue;
static xTaskHandle s_taskHandles[6];
static const char *const s_taskNames[6] =
    {"safety", "control", "plant", "input", "display", "logger"};
static LogRecord_t s_logReadback[LOG_MAX_RECORDS];
static uint8_t s_stream;
/* 0～2 表示固定显示对应页面，3 表示自动轮换页面。 */
static volatile uint8_t s_lcdSelection = 0U;
typedef struct
{
    char key;
    uint8_t faultMask;
} CommandReceipt_t;
static xQueueHandle s_receiptQueue;
static volatile uint32_t s_receiptDropped;
static volatile uint32_t s_taskLoops[6];
static volatile portTickType s_taskLastTick[6];

/* 记录任务循环次数和最近活动时刻，供串口诊断查询。 */
static void TaskHeartbeat(uint8_t index)
{
    s_taskLastTick[index] = xTaskGetTickCount();
    ++s_taskLoops[index];
}


/* 逐字节复制共享快照，避免本机单片机仿真模型的多字传输导致数据错位。 */
static void CopySnapshot(void *destination, const void *source, size_t size)
{
    volatile unsigned char *out = destination;
    const volatile unsigned char *in = source;
    while (size-- != 0U)
        *out++ = *in++;
}

/* 判断当前流程是否需要执行门安全联锁。 */
static uint8_t StateNeedsDoorInterlock(WashingState_t state)
{
    return (state >= WSTATE_LOCKING && state <= WSTATE_FINISH_STOP &&
            state != WSTATE_FAULT)
               ? 1U
               : 0U;
}

/* 读取共享传感器、对象状态、界面和日志快照。 */
static void Shared_Get(SharedState_t *copy)
{
    if (xSemaphoreTake(s_sharedMutex, 20U / portTICK_RATE_MS) == pdTRUE)
    {
        CopySnapshot(copy, &s_shared, sizeof(*copy));
        xSemaphoreGive(s_sharedMutex);
    }
    else
    {
        CopySnapshot(copy, &s_shared, sizeof(*copy));
    }
}

static void Shared_SetSensor(const SensorInput_t *sensor)
{
    if (xSemaphoreTake(s_sharedMutex, 20U / portTICK_RATE_MS) == pdTRUE)
    {
        CopySnapshot(&s_shared.sensor, sensor, sizeof(s_shared.sensor));
        xSemaphoreGive(s_sharedMutex);
    }
}

static void Shared_SetPlant(const PlantState_t *plant)
{
    if (xSemaphoreTake(s_sharedMutex, 20U / portTICK_RATE_MS) == pdTRUE)
    {
        CopySnapshot(&s_shared.plant, plant, sizeof(s_shared.plant));
        xSemaphoreGive(s_sharedMutex);
    }
}

static void Shared_SetUi(const UiSnapshot_t *ui)
{
    if (xSemaphoreTake(s_sharedMutex, 20U / portTICK_RATE_MS) == pdTRUE)
    {
        CopySnapshot(&s_shared.ui, ui, sizeof(s_shared.ui));
        xSemaphoreGive(s_sharedMutex);
    }
}

static void Shared_SetLastLog(const LogRecord_t *record)
{
    if (xSemaphoreTake(s_sharedMutex, 20U / portTICK_RATE_MS) == pdTRUE)
    {
        CopySnapshot(&s_shared.lastLog, record, sizeof(s_shared.lastLog));
        xSemaphoreGive(s_sharedMutex);
    }
}

/* 把任务事件转换为定长的 Flash 日志记录。 */
static void BuildLogRecord(const EventMessage_t *message, LogRecord_t *record)
{
    memset(record, 0, sizeof(*record));
    record->timestampSec = message->timestampSec;
    record->waterMl = message->waterMl;
    record->waterTempDeciC = message->waterTempDeciC;
    record->drumRpm = message->drumRpm;
    record->magic = LOG_RECORD_MAGIC;
    record->state = (uint8_t)message->state;
    record->fault = (uint8_t)message->fault;
    record->program = (uint8_t)message->program;
    record->event = (uint8_t)message->event;
}

/* 安全任务：周期检查急停、开门和超温条件，并触发安全输出。 */
static void SafetyTask(void *argument)
{
    portTickType lastWake = xTaskGetTickCount();
    SharedState_t shared;

    (void)argument;
    for (;;)
    {
        TaskHeartbeat(0U);
        Shared_Get(&shared);

        if ((shared.sensor.buttonMask & 0x04U) != 0U &&
            Control_IsActive() != 0U && shared.ui.selfTest == 0U)
        {
            Control_ForceFault(FAULT_EMERGENCY_STOP);
            BSP_EmergencySafe();
        }

        if (StateNeedsDoorInterlock(shared.ui.state) != 0U &&
            shared.sensor.doorOpen != 0U)
        {
            Control_ForceFault(FAULT_DOOR_OPEN);
            BSP_EmergencySafe();
        }

        if (shared.plant.waterTempDeciC > 850U)
        {
            Control_ForceFault(FAULT_OVER_TEMPERATURE);
            BSP_EmergencySafe();
        }

        vTaskDelayUntil(&lastWake, MS_TO_TICKS(SAFETY_PERIOD_MS));
    }
}

/* 输入任务：按键消抖、采集重量和污浊度，并把串口命令转换为控制输入。 */
static void InputTask(void *argument)
{
    portTickType lastWake = xTaskGetTickCount();
    uint8_t stable = 0U;
    uint8_t debounce[4];
    uint8_t index;
    uint8_t counter = 4U;
    uint8_t stopHoldTicks = 0U;
    uint8_t pageWasDown = 0U;
    uint8_t remoteRelease = 0U;
    uint8_t remoteKey;
    char serialCommand;
    CommandReceipt_t receipt;
    SensorInput_t sensor;
    uint16_t load = 0U;
    uint16_t soil = 0U;

    (void)argument;
    memset(debounce, 0, sizeof(debounce));
    memset(&sensor, 0, sizeof(sensor));

    for (;;)
    {
        uint8_t raw = BSP_ReadButtonMask();
        TaskHeartbeat(3U);

        for (index = 0U; index < 4U; ++index)
        {
            uint8_t bit = (uint8_t)(1U << index);
            if ((raw & bit) != 0U)
            {
                if (debounce[index] < 5U)
                    ++debounce[index];
            }
            else
            {
                if (debounce[index] > 0U)
                    --debounce[index];
            }

            if (debounce[index] >= 3U)
                stable |= bit;
            else if (debounce[index] == 0U)
                stable &= (uint8_t)~bit;
        }
        if ((stable & 8U) != 0U && pageWasDown == 0U)
            s_lcdSelection = s_lcdSelection < 3U ? (uint8_t)((s_lcdSelection + 1U) % 3U) : 0U;
        pageWasDown = stable & 8U;
        sensor.buttonMask = stable;
        sensor.doorOpen = BSP_ReadDoorOpen();
        if ((stable & 0x04U) != 0U)
        {
            if (stopHoldTicks < 100U)
                ++stopHoldTicks;
        }
        else
        {
            stopHoldTicks = 0U;
        }
        sensor.selfTestRequested = stopHoldTicks >= 75U ? 1U : 0U;
        /* 每次扫描处理一条串口命令，下次扫描释放虚拟按键，确保控制器能检测到新的按下沿。 */
        remoteKey = 0U;
        if (remoteRelease != 0U)
            remoteRelease = 0U;
        else if (xQueueReceive(s_commandQueue, &serialCommand, 0) == pdTRUE)
        {
            remoteRelease = 1U;
            switch (serialCommand)
            {
            case 'm': remoteKey = 1U; break;
            case 's': remoteKey = 2U; break;
            case 'x':
                Control_ForceFault(FAULT_EMERGENCY_STOP);
                BSP_EmergencySafe();
                break;
            case 't': sensor.selfTestRequested = 1U; break;
            case 'i':
                Plant_SetFaultMask(Plant_GetFaultMask() ^ PLANT_FAULT_INLET);
                break;
            case 'd':
                Plant_SetFaultMask(Plant_GetFaultMask() ^ PLANT_FAULT_DRAIN);
                break;
            case 'b':
                Plant_SetFaultMask(Plant_GetFaultMask() ^ PLANT_FAULT_IMBALANCE);
                break;
            case 'h': Control_ForceFault(FAULT_OVER_TEMPERATURE); break;
            case 'c': Plant_SetFaultMask(0U); break;
            default: break;
            }
            receipt.key = serialCommand;
            receipt.faultMask = Plant_GetFaultMask();
            if (xQueueSend(s_receiptQueue, &receipt, 0) != pdTRUE)
                ++s_receiptDropped;
        }
        sensor.buttonMask |= remoteKey;


        ++counter;
        if (counter >= 5U)
        {
            counter = 0U;
            load = BSP_ReadLoadPermille();
            soil = BSP_ReadSoilPermille();
        }
        sensor.loadPermille = load;
        sensor.soilPermille = soil;

        Shared_SetSensor(&sensor);
        (void)xQueueSend(s_sensorQueue, &sensor, 0);

        vTaskDelayUntil(&lastWake, MS_TO_TICKS(INPUT_PERIOD_MS));
    }
}

/* 控制任务：执行洗衣状态机，更新执行器、界面和事件队列。 */
static void ControlTask(void *argument)
{
    portTickType lastWake = xTaskGetTickCount();
    SensorInput_t sensor;
    SensorInput_t incoming;
    SharedState_t shared;
    PlantState_t plant;
    ActuatorCommand_t command;
    UiSnapshot_t ui;
    EventMessage_t eventMessage;
    uint8_t eventCode;

    (void)argument;
    memset(&sensor, 0, sizeof(sensor));
    memset(&ui, 0, sizeof(ui));

    for (;;)
    {
        TaskHeartbeat(1U);
        while (xQueueReceive(s_sensorQueue, &incoming, 0) == pdTRUE)
        {
            CopySnapshot(&sensor, &incoming, sizeof(sensor));
        }

        Shared_Get(&shared);
        CopySnapshot(&plant, &shared.plant, sizeof(plant));

        Control_Update(&sensor, &plant, sensor.buttonMask, &command, &ui,
                       &eventCode);
        BSP_ApplyActuators(&command);
        (void)xQueueSend(s_actuatorQueue, &command, 0);
        Shared_SetUi(&ui);

        if (eventCode != 0U)
        {
            eventMessage.timestampSec = s_uptimeMs / 1000U;
            eventMessage.waterMl = plant.waterMl;
            eventMessage.waterTempDeciC = plant.waterTempDeciC;
            eventMessage.drumRpm = plant.drumRpm;
            eventMessage.state = ui.state;
            eventMessage.fault = ui.fault;
            eventMessage.program = ui.program;
            eventMessage.event = (LogEvent_t)eventCode;
            (void)xQueueSend(s_eventQueue, &eventMessage, 0);
        }

        s_uptimeMs += CONTROL_PERIOD_MS;
        vTaskDelayUntil(&lastWake, MS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}

/* 对象模型任务：根据执行器命令推进水量、温度、转速等仿真状态。 */
static void PlantTask(void *argument)
{
    portTickType lastWake = xTaskGetTickCount();
    ActuatorCommand_t command;
    ActuatorCommand_t incoming;
    SharedState_t shared;
    PlantState_t plant;
    uint8_t cycleStartPending;

    (void)argument;
    memset(&command, 0, sizeof(command));
    cycleStartPending = 0U;

    for (;;)
    {
        TaskHeartbeat(2U);
        while (xQueueReceive(s_actuatorQueue, &incoming, 0) == pdTRUE)
        {
            cycleStartPending |= incoming.cycleStart;
            CopySnapshot(&command, &incoming, sizeof(command));
        }
        command.cycleStart = cycleStartPending;

        Shared_Get(&shared);
        Plant_Update(&command, shared.sensor.loadPermille,
                     shared.sensor.soilPermille, PLANT_PERIOD_MS);
        cycleStartPending = 0U;
        CopySnapshot(&plant, Plant_GetState(), sizeof(plant));
        Shared_SetPlant(&plant);

        vTaskDelayUntil(&lastWake, MS_TO_TICKS(PLANT_PERIOD_MS));
    }
}

/* 显示任务：刷新液晶页面；故障页面优先于手动选页和自动轮换。 */
static const char *StateChinese(WashingState_t state)
{
    static const char *const names[]={"\xE5\xBE\x85\xE6\x9C\xBA","\xE9\x94\x81\xE9\x97\xA8","\xE8\xBF\x9B\xE6\xB0\xB4","\xE5\x8A\xA0\xE7\x83\xAD","\xE6\xAD\xA3\xE8\xBD\xAC\xE6\xB4\x97\xE6\xB6\xA4","\xE6\xB4\x97\xE6\xB6\xA4\xE5\x81\x9C\xE9\xA1\xBF","\xE5\x8F\x8D\xE8\xBD\xAC\xE6\xB4\x97\xE6\xB6\xA4","\xE6\x8E\x92\xE6\xB0\xB4","\xE6\xBC\x82\xE6\xB4\x97\xE8\xBF\x9B\xE6\xB0\xB4","\xE6\xAD\xA3\xE8\xBD\xAC\xE6\xBC\x82\xE6\xB4\x97","\xE6\xBC\x82\xE6\xB4\x97\xE5\x81\x9C\xE9\xA1\xBF","\xE5\x8F\x8D\xE8\xBD\xAC\xE6\xBC\x82\xE6\xB4\x97","\xE8\x84\xB1\xE6\xB0\xB4\xE5\x8A\xA0\xE9\x80\x9F","\xE9\xAB\x98\xE9\x80\x9F\xE8\x84\xB1\xE6\xB0\xB4","\xE9\x87\x8D\xE6\x96\xB0\xE5\x88\x86\xE5\xB8\x83","\xE5\x81\x9C\xE6\x9C\xBA\xE7\xAD\x89\xE5\xBE\x85","\xE8\xA7\xA3\xE9\x94\x81","\xE6\x95\x85\xE9\x9A\x9C"};
    return (unsigned)state < 18U ? names[state] : "\xE6\x9C\xAA\xE7\x9F\xA5\xE7\x8A\xB6\xE6\x80\x81";
}
static const char *ProgramChinese(WashingProgram_t program)
{
    static const char *const names[]={"\xE8\x87\xAA\xE5\x8A\xA8","\xE5\xBF\xAB\xE6\xB4\x97","\xE6\xA0\x87\xE5\x87\x86","\xE5\xBC\xBA\xE6\xB4\x97","\xE7\xBE\x8A\xE6\xAF\x9B"};
    return (unsigned)program < 5U ? names[program] : "\xE6\x9C\xAA\xE7\x9F\xA5";
}
static const char *FaultChinese(WashingFault_t fault)
{
    static const char *const names[]={"\xE6\x97\xA0\xE6\x95\x85\xE9\x9A\x9C","\xE9\x97\xA8\xE6\x9C\xAA\xE5\x85\xB3\xE9\x97\xAD","\xE8\xBF\x9B\xE6\xB0\xB4\xE8\xB6\x85\xE6\x97\xB6","\xE6\x8E\x92\xE6\xB0\xB4\xE8\xB6\x85\xE6\x97\xB6","\xE6\xB8\xA9\xE5\xBA\xA6\xE8\xBF\x87\xE9\xAB\x98","\xE8\xB4\x9F\xE8\xBD\xBD\xE4\xB8\x8D\xE5\xB9\xB3\xE8\xA1\xA1","\xE7\xB4\xA7\xE6\x80\xA5\xE5\x81\x9C\xE6\xAD\xA2","\xE5\x81\x9C\xE8\xBD\xAC\xE8\xB6\x85\xE6\x97\xB6"};
    return (unsigned)fault < 8U ? names[fault] : "\xE6\x9C\xAA\xE7\x9F\xA5\xE6\x95\x85\xE9\x9A\x9C";
}

static void DisplayTask(void *argument)
{
    portTickType lastWake = xTaskGetTickCount();
    uint32_t pageTimer = 0U;
    uint8_t page = 0U;
    uint8_t previousSelection = 3U;
    SharedState_t shared;
    char line[32];

    /* OLED 已在硬件初始化时清屏，不再重复传输整屏。 */
    (void)argument;

    for (;;)
    {
        uint8_t selection = s_lcdSelection;
        TaskHeartbeat(4U);
        Shared_Get(&shared);

        if (selection != previousSelection)
            pageTimer = 0U;
        previousSelection = selection;
        if (shared.ui.state == WSTATE_FAULT)
        {
            page = 0U;
        }
        else if (selection < 3U)
        {
            page = selection;
        }
        else
        {
            ++pageTimer;
            if (pageTimer >= DISPLAY_PAGE_PERIOD_MS / DISPLAY_PERIOD_MS)
            {
                pageTimer = 0U;
                page = (uint8_t)((page + 1U) % 3U);
            }
        }

        if (shared.ui.state == WSTATE_FAULT)
        {
            LCD_PrintLine(0U, "\xE6\x95\x85\xE9\x9A\x9C\xE6\x8A\xA5\xE8\xAD\xA6");
            LCD_PrintLine(1U, FaultChinese(shared.ui.fault));
            LCD_PrintLine(2U, shared.ui.selfTest ? "\xE8\x87\xAA\xE6\xA3\x80\xE8\x87\xAA\xE5\x8A\xA8\xE6\x81\xA2\xE5\xA4\x8D" : "\xE6\x8E\x92\xE9\x99\xA4\xE6\x95\x85\xE9\x9A\x9C\xE5\x85\xB3\xE9\x97\xA8");
            LCD_PrintLine(3U, shared.ui.selfTest ? "\xE8\xAF\xB7\xE7\xA8\x8D\xE5\x80\x99" : "\xE6\x8C\x89\xE5\x90\xAF\xE5\x8A\xA8\xE9\x94\xAE\xE5\xA4\x8D\xE4\xBD\x8D");
        }
        else if (page == 0U)
        {
            LCD_PrintLine(0U, shared.ui.selfTest ? "\xE8\x87\xAA\xE6\xA3\x80\xE8\xBF\x90\xE8\xA1\x8C" :
                (shared.ui.paused ? "\xE5\xB7\xB2\xE6\x9A\x82\xE5\x81\x9C" : (shared.ui.cycleComplete ? "\xE6\xB4\x97\xE6\xB6\xA4\xE5\xAE\x8C\xE6\x88\x90" : StateChinese(shared.ui.state))));
            (void)snprintf(line,sizeof(line),"\xE7\xA8\x8B\xE5\xBA\x8F\x3A\x25\x73",ProgramChinese(shared.ui.program)); LCD_PrintLine(1U,line);
            (void)snprintf(line,sizeof(line),"\xE6\xB0\xB4\xE9\x87\x8F\x25\x75\x2F\x25\x75",shared.ui.waterMl,shared.ui.targetWaterMl); LCD_PrintLine(2U,line);
            (void)snprintf(line,sizeof(line),"\xE8\xBF\x9B\xE5\xBA\xA6\x25\x75\x25\x25\x20\x20\xE9\xA1\xB5\x31\x2F\x33",shared.ui.progressPercent); LCD_PrintLine(3U,line);
        }
        else if (page == 1U)
        {
            (void)snprintf(line,sizeof(line),"\xE9\x87\x8D\xE9\x87\x8F\x25\x75\x25\x25\x20\xE6\xB1\xA1\x25\x75\x25\x25",shared.ui.loadPermille/10U,shared.ui.soilPermille/10U); LCD_PrintLine(0U,line);
            (void)snprintf(line,sizeof(line),"\xE6\xB0\xB4\xE6\xB8\xA9\x25\x75\x2E\x25\x75\xE5\xBA\xA6",shared.ui.waterTempDeciC/10U,shared.ui.waterTempDeciC%10U); LCD_PrintLine(1U,line);
            (void)snprintf(line,sizeof(line),"\xE8\xBD\xAC\xE9\x80\x9F\x25\x64",shared.ui.drumRpm); LCD_PrintLine(2U,line);
            (void)snprintf(line,sizeof(line),"\xE4\xB8\x8D\xE5\xB9\xB3\xE8\xA1\xA1\x25\x75",shared.ui.imbalancePermille); LCD_PrintLine(3U,line);
        }
        else
        {
            (void)snprintf(line,sizeof(line),"\xE8\xBF\x9B\xE6\xB0\xB4\x25\x73\x20\xE6\x8E\x92\xE6\xB0\xB4\x25\x73",shared.ui.inletOn?"\xE5\xBC\x80":"\xE5\x85\xB3",shared.ui.drainOn?"\xE5\xBC\x80":"\xE5\x85\xB3"); LCD_PrintLine(0U,line);
            (void)snprintf(line,sizeof(line),"\xE5\x8A\xA0\xE7\x83\xAD\x25\x73\x20\xE9\x97\xA8\xE9\x94\x81\x25\x73",shared.ui.heaterOn?"\xE5\xBC\x80":"\xE5\x85\xB3",shared.ui.doorLocked?"\xE5\xBC\x80":"\xE5\x85\xB3"); LCD_PrintLine(1U,line);
            (void)snprintf(line,sizeof(line),"\xE6\xB3\xA8\xE5\x85\xA5\xE6\x8E\xA9\xE7\xA0\x81\x25\x75",Plant_GetFaultMask()); LCD_PrintLine(2U,line);
            LCD_PrintLine(3U, "\xE7\xBF\xBB\xE9\xA1\xB5\xE9\x94\xAE\xE5\x88\x87\xE6\x8D\xA2\xE9\xA1\xB5\xE9\x9D\xA2");
        }

        vTaskDelayUntil(&lastWake, MS_TO_TICKS(DISPLAY_PERIOD_MS));
    }
}

/* 输出当前运行状态；温度单位为摄氏度，重量和污浊度按千分比表示。 */
static void Console_Status(void)
{
    SharedState_t shared;
    Shared_Get(&shared);
    UART1_Printf("STATUS %s %s fault=%s pause=%u\r\n",
        Washer_StateName(shared.ui.state), Washer_ProgramName(shared.ui.program),
        Washer_FaultName(shared.ui.fault), shared.ui.paused);
    UART1_Printf("W=%u/%u T=%u.%u RPM=%d load=%u soil=%u\r\n",
        shared.ui.waterMl, shared.ui.targetWaterMl,
        shared.ui.waterTempDeciC / 10U, shared.ui.waterTempDeciC % 10U,
        shared.ui.drumRpm, shared.ui.loadPermille, shared.ui.soilPermille);
    UART1_Printf("OUT inlet=%u drain=%u heat=%u lock=%u inject=%u\r\n",
        shared.ui.inletOn, shared.ui.drainOn, shared.ui.heaterOn,
        shared.ui.doorLocked, Plant_GetFaultMask());
    UART1_Printf("UP=%lus phase=%lus door_open=%u selftest=%u imbalance=%u\r\n",
        (unsigned long)(xTaskGetTickCount() / MS_TO_TICKS(1000)),
        (unsigned long)shared.ui.phaseSeconds, shared.ui.doorOpen,
        shared.ui.selfTest, shared.ui.imbalancePermille);
}

/* 输出任务、队列和内存诊断；循环次数和活动时间差不代表处理器占用率。 */
static void Console_Diagnostics(void)
{
    uint8_t index;
    uint32_t loops[6];
    portTickType lastTick[6];
    portTickType now;
    taskENTER_CRITICAL();
    now = xTaskGetTickCount();
    for (index = 0U; index < 6U; ++index)
    {
        loops[index] = s_taskLoops[index];
        lastTick[index] = s_taskLastTick[index];
    }
    taskEXIT_CRITICAL();
    UART1_Printf("RTOS free_heap=%u bytes rx_dropped=%lu\r\n",
        (unsigned)xPortGetFreeHeapSize(), (unsigned long)UART1_RxDropped());
    UART1_Printf("QUEUES sensor=%u actuator=%u event=%u command=%u\r\n",
        (unsigned)uxQueueMessagesWaiting(s_sensorQueue),
        (unsigned)uxQueueMessagesWaiting(s_actuatorQueue),
        (unsigned)uxQueueMessagesWaiting(s_eventQueue),
        (unsigned)uxQueueMessagesWaiting(s_commandQueue));
    UART1_Printf("receipt_dropped=%lu\r\n", (unsigned long)s_receiptDropped);
    UART1_WriteString("TASK priority stack_min_bytes loops age_ms\r\n");
    for (index = 0U; index < 6U; ++index)
        UART1_Printf("%s %u %u %lu %lu\r\n", s_taskNames[index],
            (unsigned)uxTaskPriorityGet(s_taskHandles[index]),
            (unsigned)(uxTaskGetStackHighWaterMark(s_taskHandles[index]) *
                       sizeof(portSTACK_TYPE)), (unsigned long)loops[index],
            (unsigned long)((portTickType)(now - lastTick[index]) * portTICK_RATE_MS));
}

/* 根据当前状态输出演示提示，自检期间优先提示等待自动恢复。 */
static void Console_Guide(void)
{
    SharedState_t shared;
    Shared_Get(&shared);
    UART1_Printf("GUIDE %s %s inject=%u\r\n", Washer_StateName(shared.ui.state),
        Washer_ProgramName(shared.ui.program), Plant_GetFaultMask());
    if (shared.ui.selfTest != 0U)
        UART1_WriteString("SELFTEST: automatic door-fault demonstration and recovery; wait for IDLE.\r\n");
    else if (shared.ui.state == WSTATE_FAULT)
    {
        UART1_Printf("FAULT %s: read l; remove cause, close door, then s.\r\n",
            Washer_FaultName(shared.ui.fault));
        UART1_WriteString("c clears inlet/drain/imbalance injections; it does not reset the fault.\r\n");
    }
    else if (shared.ui.paused != 0U)
        UART1_WriteString("PAUSED: p shows outputs and RPM; s resumes. x forces emergency stop.\r\n");
    else if (shared.ui.state == WSTATE_IDLE)
        UART1_WriteString("IDLE: m selects program, s starts, t runs selftest (door closed).\r\n"
            "For a fault demo: choose QUICK; i/d/b toggles blockage or imbalance; s starts.\r\n");
    else if (shared.ui.state == WSTATE_FILLING || shared.ui.state == WSTATE_RINSE_FILL)
        UART1_WriteString("FILL: compare inlet output with water rise. i blocks flow; timeout is 30s.\r\n");
    else if (shared.ui.state == WSTATE_DRAIN)
        UART1_WriteString("DRAIN: compare drain output with water fall. d blocks flow; timeout is 30s.\r\n");
    else if (shared.ui.state == WSTATE_HEATING)
        UART1_WriteString("HEAT: watch temperature rise on LCD page 1. h injects an overtemp fault code.\r\n");
    else if (shared.ui.state == WSTATE_SPIN_UP || shared.ui.state == WSTATE_SPIN ||
             shared.ui.state == WSTATE_SPIN_REBALANCE)
        UART1_WriteString("SPIN: b forces imbalance and redistribution retries; p shows imbalance and RPM.\r\n");
    else
        UART1_WriteString("RUNNING: s pauses/resumes, x stops. p=status, v=stream, r=tasks.\r\n");
    UART1_WriteString("LCD 0=overview 1=sensors 2=outputs a=auto. Fault page always overrides.\r\n");
}

/* 读取输入任务的执行回执，由日志任务统一发送到串口。 */
static void Console_Receipts(void)
{
    CommandReceipt_t receipt;
    while (xQueueReceive(s_receiptQueue, &receipt, 0) == pdTRUE)
    {
        if (strchr("idbc", receipt.key) != 0)
            UART1_Printf("APPLIED %c inject=%u (I=%u D=%u B=%u)\r\n", receipt.key,
                receipt.faultMask, (receipt.faultMask & PLANT_FAULT_INLET) != 0U,
                (receipt.faultMask & PLANT_FAULT_DRAIN) != 0U,
                (receipt.faultMask & PLANT_FAULT_IMBALANCE) != 0U);
        else
            UART1_Printf("DISPATCHED %c; p checks resulting state, g shows conditions.\r\n",
                receipt.key);
    }
}

/* 回读当前 Flash 页中的有效事件记录。 */
static void Console_ReadLogs(void)
{
    uint8_t count = FlashLog_ReadAll(s_logReadback, LOG_MAX_RECORDS);
    uint8_t index;
    UART1_Printf("FLASH LOG records=%u\r\n", count);
    for (index = 0U; index < count; ++index)
        UART1_Printf("%lu %s %s %s\r\n",
            (unsigned long)s_logReadback[index].timestampSec,
            Washer_EventName((LogEvent_t)s_logReadback[index].event),
            Washer_StateName((WashingState_t)s_logReadback[index].state),
            Washer_FaultName((WashingFault_t)s_logReadback[index].fault));
}

/* 解析单字符命令；查询直接处理，控制命令通过队列交给输入任务。 */
static void Console_Poll(void)
{
    char key;
    uint8_t budget = 8U;
    while (budget-- != 0U && UART1_ReadChar(&key) != 0U)
    {
        if (key == '\r' || key == '\n' || key == ' ')
            continue;
        switch (key)
        {
        case '?':
            UART1_WriteString(
                "\r\nm=mode s=start/pause/reset x=E-STOP t=selftest\r\n"
                "p=status r=RTOS l=Flash logs v=stream on/off\r\n"
                "i=inlet block d=drain block b=imbalance (toggle)\r\n"
                "h=inject overtemp fault c=clear plant injections\r\n");
            UART1_WriteString("g=context guide 0/1/2=fixed LCD page a=auto pages\r\n"
                "QUEUED=accepted; APPLIED=injection set; DISPATCHED=control input sent.\r\n");
            break;
        case 'g': Console_Guide(); break;
        case '0': case '1': case '2':
            s_lcdSelection = (uint8_t)(key - '0');
            UART1_Printf("LCD fixed page %u (fault overrides)\r\n", s_lcdSelection);
            break;
        case 'a':
            s_lcdSelection = 3U;
            UART1_WriteString("LCD auto pages\r\n");
            break;
        case 'p': Console_Status(); break;
        case 'r': Console_Diagnostics(); break;
        case 'l': Console_ReadLogs(); break;
        case 'v':
            s_stream = (uint8_t)!s_stream;
            UART1_Printf("STREAM %u\r\n", s_stream);
            break;
        default:
            if (strchr("msxtidbhc", key) == 0)
                UART1_Printf("ERROR unknown key %c; ?=help\r\n", key);
            else if (xQueueSend(s_commandQueue, &key, 0) != pdTRUE)
                UART1_WriteString("BUSY command queue full\r\n");
            else
                UART1_Printf("QUEUED %c\r\n", key);
            break;
        }
    }
}

/* 日志任务：统一处理串口输出、命令解析、周期遥测和 Flash 事件写入。 */
static void LoggerTask(void *argument)
{
    EventMessage_t message;
    LogRecord_t record;
    portTickType lastStream = xTaskGetTickCount();

    (void)argument;
    for (;;)
    {
        TaskHeartbeat(5U);
        Console_Receipts();
        Console_Poll();
        if (s_stream != 0U &&
            (portTickType)(xTaskGetTickCount() - lastStream) >= MS_TO_TICKS(1000))
        {
            lastStream = xTaskGetTickCount();
            Console_Status();
        }
        if (xQueueReceive(s_eventQueue, &message, MS_TO_TICKS(20)) == pdTRUE)
        {
            BuildLogRecord(&message, &record);
            if (FlashLog_Append(&record) == 0U)
            {
                UART1_WriteString("Flash log write failed\r\n");
            }
            Shared_SetLastLog(&record);
            UART1_Printf("[%lu s] %-8s state=%s fault=%s W=%u T=%u RPM=%d\r\n",
                         (unsigned long)record.timestampSec,
                         Washer_EventName((LogEvent_t)record.event),
                         Washer_StateName((WashingState_t)record.state),
                         Washer_FaultName((WashingFault_t)record.fault),
                         record.waterMl,
                         record.waterTempDeciC,
                         record.drumRpm);
        }
    }
}

/* 动态内存申请失败时关闭中断并保持报警，停止正常任务运行。 */
void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    BSP_SetBuzzer(1U);
    for (;;)
    {
    }
}

/* 检测到任务栈溢出时关闭中断并保持报警。 */
void vApplicationStackOverflowHook(xTaskHandle task, signed char *name)
{
    (void)task;
    (void)name;
    taskDISABLE_INTERRUPTS();
    BSP_SetBuzzer(1U);
    for (;;)
    {
    }
}

/* 初始化硬件、对象模型、状态机和队列，创建六个任务并启动调度器。 */
void App_Run(void)
{
    LogRecord_t boot;
    ActuatorCommand_t initialCommand;
    uint8_t initialEvent;
    SensorInput_t initialSensor;
    PlantState_t initialPlant;
    UiSnapshot_t initialUi;
    EventMessage_t bootMessage;
    portBASE_TYPE result;

    memset(&s_shared, 0, sizeof(s_shared));
    memset(&initialCommand, 0, sizeof(initialCommand));
    BSP_Init();
    BSP_ClearEmergencyLockout();
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    Plant_Init();
    Control_Init();
    FlashLog_Init();

    s_shared.sensor.loadPermille = BSP_ReadLoadPermille();
    s_shared.sensor.soilPermille = BSP_ReadSoilPermille();
    s_shared.sensor.doorOpen = BSP_ReadDoorOpen();
    CopySnapshot(&s_shared.plant, Plant_GetState(), sizeof(s_shared.plant));
    s_shared.lastLog = FlashLog_Last();
    CopySnapshot(&initialSensor, &s_shared.sensor, sizeof(initialSensor));
    CopySnapshot(&initialPlant, &s_shared.plant, sizeof(initialPlant));
    Control_Update(&initialSensor, &initialPlant, 0U, &initialCommand,
                   &initialUi, &initialEvent);
    CopySnapshot(&s_shared.ui, &initialUi, sizeof(s_shared.ui));

    bootMessage.timestampSec = 0U;
    bootMessage.waterMl = 0U;
    bootMessage.waterTempDeciC = 250U;
    bootMessage.drumRpm = 0;
    bootMessage.state = WSTATE_IDLE;
    bootMessage.fault = FAULT_NONE;
    bootMessage.program = PROGRAM_AUTO;
    bootMessage.event = LOG_EVENT_BOOT;
    BuildLogRecord(&bootMessage, &boot);
    (void)FlashLog_Append(&boot);
    CopySnapshot(&s_shared.lastLog, &boot, sizeof(s_shared.lastLog));

    s_sharedMutex = xSemaphoreCreateMutex();
    s_sensorQueue = xQueueCreate(SENSOR_QUEUE_LENGTH, sizeof(SensorInput_t));
    s_actuatorQueue = xQueueCreate(ACTUATOR_QUEUE_LENGTH,
                                   sizeof(ActuatorCommand_t));
    s_commandQueue = xQueueCreate(COMMAND_QUEUE_LENGTH, sizeof(char));
    s_receiptQueue = xQueueCreate(COMMAND_QUEUE_LENGTH, sizeof(CommandReceipt_t));
    s_eventQueue = xQueueCreate(EVENT_QUEUE_LENGTH, sizeof(EventMessage_t));

    if (s_sharedMutex == 0 || s_sensorQueue == 0 || s_actuatorQueue == 0 ||
        s_eventQueue == 0 || s_commandQueue == 0 || s_receiptQueue == 0)
    {
        BSP_SetBuzzer(1U);
        for (;;)
        {
        }
    }

    UART1_WriteString("\r\nClassroom washer v2 ready. Type ? for help, g for guide.\r\n");
    UART1_Printf("Last log: event=%s state=%s fault=%s\r\n",
                 Washer_EventName((LogEvent_t)s_shared.lastLog.event),
                 Washer_StateName((WashingState_t)s_shared.lastLog.state),
                 Washer_FaultName((WashingFault_t)s_shared.lastLog.fault));

    result = xTaskCreate((pdTASK_CODE)SafetyTask, (const signed char *)"safety",
                         TASK_STACK_SAFETY, 0, PRIORITY_SAFETY, &s_taskHandles[0]);
    if (result == pdPASS)
        result = xTaskCreate((pdTASK_CODE)ControlTask,
                             (const signed char *)"control",
                             TASK_STACK_CONTROL, 0, PRIORITY_CONTROL, &s_taskHandles[1]);
    if (result == pdPASS)
        result = xTaskCreate((pdTASK_CODE)PlantTask,
                             (const signed char *)"plant",
                             TASK_STACK_PLANT, 0, PRIORITY_PLANT, &s_taskHandles[2]);
    if (result == pdPASS)
        result = xTaskCreate((pdTASK_CODE)InputTask,
                             (const signed char *)"input",
                             TASK_STACK_INPUT, 0, PRIORITY_INPUT, &s_taskHandles[3]);
    if (result == pdPASS)
        result = xTaskCreate((pdTASK_CODE)DisplayTask,
                             (const signed char *)"display",
                             TASK_STACK_DISPLAY, 0, PRIORITY_DISPLAY, &s_taskHandles[4]);
    if (result == pdPASS)
        result = xTaskCreate((pdTASK_CODE)LoggerTask,
                             (const signed char *)"logger",
                             TASK_STACK_LOGGER, 0, PRIORITY_LOGGER, &s_taskHandles[5]);

    if (result != pdPASS)
    {
        BSP_SetBuzzer(1U);
        for (;;)
        {
        }
    }

    vTaskStartScheduler();
    for (;;)
    {
    }
}

