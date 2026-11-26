#ifndef WASHER_TYPES_H
#define WASHER_TYPES_H

#include "stdint.h"

typedef enum
{
    WSTATE_IDLE = 0,
    WSTATE_LOCKING,
    WSTATE_FILLING,
    WSTATE_HEATING,
    WSTATE_WASH_FORWARD,
    WSTATE_WASH_PAUSE,
    WSTATE_WASH_REVERSE,
    WSTATE_DRAIN,
    WSTATE_RINSE_FILL,
    WSTATE_RINSE_FORWARD,
    WSTATE_RINSE_PAUSE,
    WSTATE_RINSE_REVERSE,
    WSTATE_SPIN_UP,
    WSTATE_SPIN,
    WSTATE_SPIN_REBALANCE,
    WSTATE_FINISH_STOP,
    WSTATE_UNLOCK,
    WSTATE_FAULT
} WashingState_t;

typedef enum
{
    PROGRAM_AUTO = 0,
    PROGRAM_QUICK,
    PROGRAM_STANDARD,
    PROGRAM_HEAVY,
    PROGRAM_WOOL,
    PROGRAM_COUNT
} WashingProgram_t;

typedef enum
{
    FAULT_NONE = 0,
    FAULT_DOOR_OPEN,
    FAULT_FILL_TIMEOUT,
    FAULT_DRAIN_TIMEOUT,
    FAULT_OVER_TEMPERATURE,
    FAULT_MOTOR_IMBALANCE,
    FAULT_EMERGENCY_STOP,
    FAULT_SPIN_STOP_TIMEOUT
} WashingFault_t;

typedef enum
{
    LOG_EVENT_BOOT = 0,
    LOG_EVENT_CYCLE_START,
    LOG_EVENT_DOOR_LOCK,
    LOG_EVENT_FILL,
    LOG_EVENT_HEAT,
    LOG_EVENT_WASH,
    LOG_EVENT_DRAIN,
    LOG_EVENT_RINSE,
    LOG_EVENT_SPIN,
    LOG_EVENT_PAUSE,
    LOG_EVENT_RESUME,
    LOG_EVENT_COMPLETE,
    LOG_EVENT_FAULT,
    LOG_EVENT_RESET
} LogEvent_t;

typedef struct
{
    uint16_t loadPermille;
    uint16_t soilPermille;
    uint8_t doorOpen;
    uint8_t buttonMask;
    uint8_t selfTestRequested;
} SensorInput_t;

typedef struct
{
    uint8_t cycleStart;
    uint8_t inlet;
    uint8_t drain;
    uint8_t heater;
    uint8_t doorLock;
    uint8_t buzzer;
    uint8_t motorPercent;
    int8_t motorDirection;
    int16_t targetRpm;
} ActuatorCommand_t;

typedef struct
{
    uint16_t waterMl;
    uint16_t waterTempDeciC;
    int16_t drumRpm;
    uint16_t turbidityPermille;
    uint16_t imbalancePermille;
    uint32_t powerMw;
} PlantState_t;

typedef struct
{
    WashingState_t state;
    WashingProgram_t program;
    WashingFault_t fault;
    uint8_t paused;
    uint8_t cycleComplete;
    uint8_t selfTest;
    uint8_t doorOpen;
    uint8_t doorLocked;
    uint8_t inletOn;
    uint8_t drainOn;
    uint8_t heaterOn;
    uint8_t progressPercent;
    uint16_t loadPermille;
    uint16_t soilPermille;
    uint16_t waterMl;
    uint16_t targetWaterMl;
    uint16_t waterTempDeciC;
    int16_t drumRpm;
    uint16_t turbidityPermille;
    uint16_t imbalancePermille;
    uint32_t phaseSeconds;
} UiSnapshot_t;

/* 保持日志记录自然对齐，总长度严格为 16 字节：4 + 2 + 2 + 2 + 2 + 1 + 1 + 1 + 1 = 16。 */
typedef struct
{
    uint32_t timestampSec;
    uint16_t waterMl;
    uint16_t waterTempDeciC;
    int16_t drumRpm;
    uint16_t magic;
    uint8_t state;
    uint8_t fault;
    uint8_t program;
    uint8_t event;
} LogRecord_t;

typedef struct
{
    uint32_t timestampSec;
    uint16_t waterMl;
    uint16_t waterTempDeciC;
    int16_t drumRpm;
    WashingState_t state;
    WashingFault_t fault;
    WashingProgram_t program;
    LogEvent_t event;
} EventMessage_t;

const char *Washer_StateName(WashingState_t state);
const char *Washer_ProgramName(WashingProgram_t program);
const char *Washer_FaultName(WashingFault_t fault);
const char *Washer_EventName(LogEvent_t event);

#endif

