#include "washer_control.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app_config.h"
#include "bsp.h"

typedef struct
{
    uint16_t targetWaterMl;
    uint16_t targetTempDeciC;
    uint16_t washSpinMs;
    uint16_t washPauseMs;
    uint8_t washPairs;
    uint8_t rinseCount;
    uint16_t rinseSpinMs;
    uint16_t spinRpm;
    uint16_t spinTimeMs;
} WashRecipe_t;

static WashingState_t s_state;
static WashingProgram_t s_selectedProgram;
static WashingProgram_t s_activeProgram;
static WashingFault_t s_fault;
static WashRecipe_t s_recipe;
static uint32_t s_phaseMs;
static uint8_t s_previousButtons;
static uint8_t s_paused;
static uint8_t s_cycleComplete;
static uint8_t s_washPairsLeft;
static uint8_t s_rinseLeft;
static uint8_t s_imbalanceRetries;
static uint8_t s_pauseNextState;
static uint8_t s_demoMode;
static uint8_t s_demoFaultInjected;
static uint16_t s_demoFaultMs;
static uint8_t s_previousSelfTest;
static volatile uint16_t s_forcedFault;
static uint8_t s_lastEvent;

static void EnterState(WashingState_t state, uint8_t event);

static void ClearCommand(ActuatorCommand_t *command)
{
    memset(command, 0, sizeof(*command));
}

static void SafetyOutputs(ActuatorCommand_t *command, uint8_t lockDoor)
{
    ClearCommand(command);
    command->doorLock = lockDoor;
}

/* 根据用户选择、重量和污浊度生成水量、温度、洗涤和脱水配方。 */
static void BuildRecipe(WashingProgram_t requested,
                        const SensorInput_t *sensor)
{
    uint32_t score = ((uint32_t)sensor->loadPermille * 2U +
                      (uint32_t)sensor->soilPermille) / 3U;

    s_activeProgram = requested;
    if (requested == PROGRAM_AUTO)
    {
        if (sensor->loadPermille > 760U || sensor->soilPermille > 720U)
            s_activeProgram = PROGRAM_HEAVY;
        else if (sensor->loadPermille < 320U && sensor->soilPermille < 280U)
            s_activeProgram = PROGRAM_QUICK;
        else
            s_activeProgram = PROGRAM_STANDARD;
    }

    switch (s_activeProgram)
    {
    case PROGRAM_QUICK:
        s_recipe.targetWaterMl =
            (uint16_t)(1000UL + ((uint32_t)sensor->loadPermille * 2U));
        s_recipe.targetTempDeciC = 300U;
        s_recipe.washSpinMs = 2500U;
        s_recipe.washPauseMs = 600U;
        s_recipe.washPairs = 1U;
        s_recipe.rinseCount = 1U;
        s_recipe.rinseSpinMs = 1800U;
        s_recipe.spinRpm = 500U;
        s_recipe.spinTimeMs = 3000U;
        break;
    case PROGRAM_HEAVY:
        s_recipe.targetWaterMl =
            (uint16_t)(2200UL + ((uint32_t)sensor->loadPermille * 4U));
        s_recipe.targetTempDeciC = 500U;
        s_recipe.washSpinMs = 4200U;
        s_recipe.washPauseMs = 800U;
        s_recipe.washPairs = 3U;
        s_recipe.rinseCount = 2U;
        s_recipe.rinseSpinMs = 2600U;
        s_recipe.spinRpm = 1000U;
        s_recipe.spinTimeMs = 5500U;
        break;
    case PROGRAM_WOOL:
        s_recipe.targetWaterMl =
            (uint16_t)(1800UL + ((uint32_t)sensor->loadPermille * 2U));
        s_recipe.targetTempDeciC = 300U;
        s_recipe.washSpinMs = 1600U;
        s_recipe.washPauseMs = 1200U;
        s_recipe.washPairs = 1U;
        s_recipe.rinseCount = 1U;
        s_recipe.rinseSpinMs = 1200U;
        s_recipe.spinRpm = 350U;
        s_recipe.spinTimeMs = 3000U;
        break;
    case PROGRAM_AUTO:
    case PROGRAM_STANDARD:
    default:
        s_activeProgram = PROGRAM_STANDARD;
        s_recipe.targetWaterMl =
            (uint16_t)(1600UL + ((uint32_t)sensor->loadPermille * 3U));
        s_recipe.targetTempDeciC = 400U;
        s_recipe.washSpinMs = 3400U;
        s_recipe.washPauseMs = 700U;
        s_recipe.washPairs = 2U;
        s_recipe.rinseCount = 2U;
        s_recipe.rinseSpinMs = 2200U;
        s_recipe.spinRpm = 800U;
        s_recipe.spinTimeMs = 4500U;
        break;
    }

    if (s_recipe.targetWaterMl < 500U)
        s_recipe.targetWaterMl = 500U;
    if (s_recipe.targetWaterMl > 5200U)
        s_recipe.targetWaterMl = 5200U;

    /* 根据重量与污浊度的综合评分微调洗涤时间，便于在课堂中解释输入参数对配方的影响。 */
    if (score > 780U && s_recipe.washSpinMs < 4200U)
        s_recipe.washSpinMs += 400U;
}

/* 装载配方并清除上一周期标志，从锁门阶段开始新周期。 */
static void BeginCycle(const SensorInput_t *sensor,
                       WashingProgram_t requested,
                       ActuatorCommand_t *command)
{
    BuildRecipe(requested, sensor);
    s_fault = FAULT_NONE;
    s_paused = 0U;
    s_cycleComplete = 0U;
    s_washPairsLeft = s_recipe.washPairs;
    s_rinseLeft = s_recipe.rinseCount;
    s_imbalanceRetries = 0U;
    command->cycleStart = 1U;
    EnterState(WSTATE_LOCKING, LOG_EVENT_CYCLE_START);
}

/* 切换流程状态并重置阶段计时，按需记录状态事件。 */
static void EnterState(WashingState_t state, uint8_t event)
{
    if (state == WSTATE_IDLE || state == WSTATE_LOCKING)
        BSP_ClearEmergencyLockout();

    s_state = state;
    s_phaseMs = 0U;
    if (event != 0U)
        s_lastEvent = event;
}

/* 锁存故障并退出暂停状态，后续由故障分支控制安全输出。 */
static void EnterFault(WashingFault_t fault, uint8_t event)
{
    s_fault = fault;
    s_state = WSTATE_FAULT;
    s_phaseMs = 0U;
    s_paused = 0U;
    s_cycleComplete = 0U;
    if (event != 0U)
        s_lastEvent = event;
}

/* 先处理强制故障，再检查运行期间的开门和超温条件。 */
static uint8_t UpdateFaultFromInputs(const SensorInput_t *sensor,
                                     const PlantState_t *plant)
{
    if (s_forcedFault != 0U)
    {
        uint16_t forced = s_forcedFault;
        s_forcedFault = 0U;
        EnterFault((WashingFault_t)forced, LOG_EVENT_FAULT);
        return 1U;
    }

    if (s_state == WSTATE_FAULT || s_state == WSTATE_IDLE)
        return 0U;

    if (sensor->doorOpen != 0U && s_state != WSTATE_UNLOCK)
    {
        EnterFault(FAULT_DOOR_OPEN, LOG_EVENT_FAULT);
        return 1U;
    }
    if (plant->waterTempDeciC > 850U)
    {
        EnterFault(FAULT_OVER_TEMPERATURE, LOG_EVENT_FAULT);
        return 1U;
    }
    return 0U;
}

/* 将流程阶段映射为展示用进度；该值不是剩余时间估计。 */
static uint8_t ProgressForState(WashingState_t state)
{
    switch (state)
    {
    case WSTATE_IDLE:           return 0U;
    case WSTATE_LOCKING:        return 2U;
    case WSTATE_FILLING:        return 8U;
    case WSTATE_HEATING:        return 16U;
    case WSTATE_WASH_FORWARD:
    case WSTATE_WASH_PAUSE:
    case WSTATE_WASH_REVERSE:   return 32U;
    case WSTATE_DRAIN:          return 46U;
    case WSTATE_RINSE_FILL:     return 52U;
    case WSTATE_RINSE_FORWARD:
    case WSTATE_RINSE_PAUSE:
    case WSTATE_RINSE_REVERSE:  return 62U;
    case WSTATE_SPIN_UP:        return 76U;
    case WSTATE_SPIN:           return 84U;
    case WSTATE_SPIN_REBALANCE: return 80U;
    case WSTATE_FINISH_STOP:    return 92U;
    case WSTATE_UNLOCK:         return 98U;
    case WSTATE_FAULT:          return 0U;
    default:                    return 0U;
    }
}

/* 汇总传感器、对象模型与控制状态，生成液晶和串口使用的界面快照。 */
static void FillUi(const SensorInput_t *sensor,
                   const PlantState_t *plant,
                   const ActuatorCommand_t *command,
                   UiSnapshot_t *ui)
{
    ui->state = s_state;
    ui->program = (s_state == WSTATE_IDLE || s_state == WSTATE_FAULT)
                      ? s_selectedProgram
                      : s_activeProgram;
    ui->fault = s_fault;
    ui->paused = s_paused;
    ui->cycleComplete = s_cycleComplete;
    ui->selfTest = s_demoMode;
    ui->doorOpen = sensor->doorOpen;
    ui->doorLocked = command->doorLock;
    ui->inletOn = command->inlet;
    ui->drainOn = command->drain;
    ui->heaterOn = command->heater;
    ui->progressPercent = ProgressForState(s_state);
    ui->loadPermille = sensor->loadPermille;
    ui->soilPermille = sensor->soilPermille;
    ui->waterMl = plant->waterMl;
    ui->targetWaterMl = s_recipe.targetWaterMl;
    ui->waterTempDeciC = plant->waterTempDeciC;
    ui->drumRpm = plant->drumRpm;
    ui->turbidityPermille = plant->turbidityPermille;
    ui->imbalancePermille = plant->imbalancePermille;
    ui->phaseSeconds = s_phaseMs / 1000U;
}

/* 初始化控制器，恢复自动程序和待机状态。 */
void Control_Init(void)
{
    s_state = WSTATE_IDLE;
    s_selectedProgram = PROGRAM_AUTO;
    s_activeProgram = PROGRAM_AUTO;
    s_fault = FAULT_NONE;
    s_phaseMs = 0U;
    s_previousButtons = 0U;
    s_paused = 0U;
    s_cycleComplete = 0U;
    s_washPairsLeft = 0U;
    s_rinseLeft = 0U;
    s_imbalanceRetries = 0U;
    s_pauseNextState = WSTATE_IDLE;
    s_demoMode = 0U;
    s_demoFaultInjected = 0U;
    s_demoFaultMs = 0U;
    s_previousSelfTest = 0U;
    s_forcedFault = 0U;
    s_lastEvent = 0U;
}

/* 提交强制故障请求，在后续控制周期中进入故障处理分支。 */
void Control_ForceFault(WashingFault_t fault)
{
    taskENTER_CRITICAL();
    s_forcedFault = (uint16_t)fault;
    taskEXIT_CRITICAL();
}

/* 判断控制器是否处于运行流程。 */
uint8_t Control_IsActive(void)
{
    return (s_state != WSTATE_IDLE && s_state != WSTATE_FAULT) ? 1U : 0U;
}

/* 按固定控制周期执行状态转移、暂停继续、安全保护及执行器输出。 */
void Control_Update(const SensorInput_t *sensor,
                    const PlantState_t *plant,
                    uint8_t buttonMask,
                    ActuatorCommand_t *command,
                    UiSnapshot_t *ui,
                    uint8_t *eventOut)
{
    uint8_t pressed = (uint8_t)(buttonMask & (uint8_t)~s_previousButtons);
    uint8_t event = 0U;

    s_previousButtons = buttonMask;
    *eventOut = 0U;
    SafetyOutputs(command, s_state != WSTATE_IDLE && s_state != WSTATE_FAULT);

    if ((pressed & 0x04U) != 0U && s_state != WSTATE_IDLE)
    {
        s_demoMode = 0U;
        EnterFault(FAULT_EMERGENCY_STOP, LOG_EVENT_FAULT);
        FillUi(sensor, plant, command, ui);
        *eventOut = LOG_EVENT_FAULT;
        return;
    }

    if ((pressed & 0x01U) != 0U && s_state == WSTATE_IDLE)
    {
        s_selectedProgram =
            (WashingProgram_t)(((uint8_t)s_selectedProgram + 1U) %
                               (uint8_t)PROGRAM_COUNT);
    }

    if ((pressed & 0x02U) != 0U)
    {
        if (s_state == WSTATE_FAULT)
        {
            if (sensor->doorOpen == 0U)
            {
                s_fault = FAULT_NONE;
                s_paused = 0U;
                s_cycleComplete = 0U;
                s_demoMode = 0U;
                EnterState(WSTATE_IDLE, LOG_EVENT_RESET);
                event = LOG_EVENT_RESET;
            }
        }
        else if (s_state == WSTATE_IDLE)
        {
            if (sensor->doorOpen != 0U)
            {
                EnterFault(FAULT_DOOR_OPEN, LOG_EVENT_FAULT);
                event = LOG_EVENT_FAULT;
            }
            else
            {
                s_demoMode = 0U;
                BeginCycle(sensor, s_selectedProgram, command);
                event = LOG_EVENT_CYCLE_START;
            }
        }
        else
        {
            s_paused = (uint8_t)(s_paused == 0U ? 1U : 0U);
            event = s_paused != 0U ? LOG_EVENT_PAUSE : LOG_EVENT_RESUME;
        }
    }

    if (sensor->selfTestRequested != 0U &&
        s_previousSelfTest == 0U &&
        s_state == WSTATE_IDLE)
    {
        if (sensor->doorOpen != 0U)
        {
            EnterFault(FAULT_DOOR_OPEN, LOG_EVENT_FAULT);
            event = LOG_EVENT_FAULT;
        }
        else
        {
            s_selectedProgram = PROGRAM_STANDARD;
            s_demoMode = 1U;
            s_demoFaultInjected = 0U;
            s_demoFaultMs = 0U;
            BeginCycle(sensor, PROGRAM_STANDARD, command);
            event = LOG_EVENT_CYCLE_START;
        }
    }
    s_previousSelfTest = sensor->selfTestRequested;

    if (UpdateFaultFromInputs(sensor, plant) != 0U)
    {
        event = LOG_EVENT_FAULT;
    }

    if (event != 0U)
    {
        s_lastEvent = event;
    }

    if (s_state == WSTATE_FAULT)
    {
        if (s_demoMode != 0U)
        {
            s_demoFaultMs += CONTROL_PERIOD_MS;
            if (s_demoFaultMs >= 2000U)
            {
                s_demoMode = 0U;
                s_demoFaultMs = 0U;
                s_fault = FAULT_NONE;
                EnterState(WSTATE_IDLE, LOG_EVENT_RESET);
                event = LOG_EVENT_RESET;
                SafetyOutputs(command, 0U);
                FillUi(sensor, plant, command, ui);
                *eventOut = event;
                return;
            }
        }
        SafetyOutputs(command, 1U);
        command->drain = 1U;
        command->buzzer = 1U;
        FillUi(sensor, plant, command, ui);
        *eventOut = event;
        return;
    }

    if (s_state == WSTATE_IDLE)
    {
        SafetyOutputs(command, 0U);
        FillUi(sensor, plant, command, ui);
        *eventOut = event;
        return;
    }

    command->doorLock = 1U;
    if (s_paused != 0U)
    {
        command->motorPercent = 0U;
        command->inlet = 0U;
        command->drain = 0U;
        command->heater = 0U;
        FillUi(sensor, plant, command, ui);
        *eventOut = event;
        return;
    }

    s_phaseMs += CONTROL_PERIOD_MS;

    if (s_demoMode != 0U && s_demoFaultInjected == 0U &&
        s_state == WSTATE_FILLING && s_phaseMs >= 1000U)
    {
        s_demoFaultInjected = 1U;
        s_demoFaultMs = 0U;
        EnterFault(FAULT_DOOR_OPEN, LOG_EVENT_FAULT);
        event = LOG_EVENT_FAULT;
    }

    if (s_state == WSTATE_FAULT)
    {
        SafetyOutputs(command, 1U);
        command->drain = 1U;
        command->buzzer = 1U;
        FillUi(sensor, plant, command, ui);
        *eventOut = event;
        return;
    }

    switch (s_state)
    {
    case WSTATE_LOCKING:
        command->doorLock = 1U;
        if (s_phaseMs >= 500U)
            EnterState(WSTATE_FILLING, LOG_EVENT_DOOR_LOCK);
        break;

    case WSTATE_FILLING:
        command->inlet = 1U;
        if (plant->waterMl >= s_recipe.targetWaterMl)
            EnterState(WSTATE_HEATING, LOG_EVENT_FILL);
        else if (s_phaseMs >= 30000U)
            EnterFault(FAULT_FILL_TIMEOUT, LOG_EVENT_FAULT);
        break;

    case WSTATE_HEATING:
        command->heater = 1U;
        if (plant->waterTempDeciC >= s_recipe.targetTempDeciC)
            EnterState(WSTATE_WASH_FORWARD, LOG_EVENT_HEAT);
        else if (s_phaseMs >= 60000U)
            EnterFault(FAULT_OVER_TEMPERATURE, LOG_EVENT_FAULT);
        break;

    case WSTATE_WASH_FORWARD:
        command->motorPercent = s_activeProgram == PROGRAM_WOOL ? 30U : 55U;
        command->motorDirection = 1;
        command->targetRpm = s_activeProgram == PROGRAM_WOOL ? 260 : 450;
        if (s_phaseMs >= s_recipe.washSpinMs)
        {
            s_pauseNextState = WSTATE_WASH_REVERSE;
            EnterState(WSTATE_WASH_PAUSE, 0U);
        }
        break;

    case WSTATE_WASH_PAUSE:
        if (s_phaseMs >= s_recipe.washPauseMs)
            EnterState((WashingState_t)s_pauseNextState, 0U);
        break;

    case WSTATE_WASH_REVERSE:
        command->motorPercent = s_activeProgram == PROGRAM_WOOL ? 30U : 55U;
        command->motorDirection = -1;
        command->targetRpm = s_activeProgram == PROGRAM_WOOL ? 260 : 450;
        if (s_phaseMs >= s_recipe.washSpinMs)
        {
            if (s_washPairsLeft > 1U)
            {
                --s_washPairsLeft;
                s_pauseNextState = WSTATE_WASH_FORWARD;
            }
            else
            {
                s_pauseNextState = WSTATE_DRAIN;
            }
            EnterState(WSTATE_WASH_PAUSE, LOG_EVENT_WASH);
        }
        break;

    case WSTATE_DRAIN:
        command->drain = 1U;
        if (plant->waterMl <= 100U)
        {
            if (s_rinseLeft > 0U)
                EnterState(WSTATE_RINSE_FILL, LOG_EVENT_DRAIN);
            else
                EnterState(WSTATE_SPIN_UP, LOG_EVENT_DRAIN);
        }
        else if (s_phaseMs >= 30000U)
        {
            EnterFault(FAULT_DRAIN_TIMEOUT, LOG_EVENT_FAULT);
        }
        break;

    case WSTATE_RINSE_FILL:
        command->inlet = 1U;
        if (plant->waterMl >= s_recipe.targetWaterMl)
            EnterState(WSTATE_RINSE_FORWARD, LOG_EVENT_RINSE);
        else if (s_phaseMs >= 30000U)
            EnterFault(FAULT_FILL_TIMEOUT, LOG_EVENT_FAULT);
        break;

    case WSTATE_RINSE_FORWARD:
        command->motorPercent = 40U;
        command->motorDirection = 1;
        command->targetRpm = 320;
        if (s_phaseMs >= s_recipe.rinseSpinMs)
        {
            s_pauseNextState = WSTATE_RINSE_REVERSE;
            EnterState(WSTATE_RINSE_PAUSE, 0U);
        }
        break;

    case WSTATE_RINSE_PAUSE:
        if (s_phaseMs >= s_recipe.washPauseMs)
            EnterState((WashingState_t)s_pauseNextState, 0U);
        break;

    case WSTATE_RINSE_REVERSE:
        command->motorPercent = 40U;
        command->motorDirection = -1;
        command->targetRpm = 320;
        if (s_phaseMs >= s_recipe.rinseSpinMs)
        {
            --s_rinseLeft;
            s_pauseNextState = WSTATE_DRAIN;
            EnterState(WSTATE_RINSE_PAUSE, 0U);
        }
        break;

    case WSTATE_SPIN_UP:
        command->drain = 1U;
        command->motorPercent = 100U;
        command->motorDirection = 1;
        command->targetRpm = (int16_t)s_recipe.spinRpm;
        if (plant->drumRpm > 300 &&
            plant->imbalancePermille > PLANT_IMBALANCE_FAULT)
        {
            if (++s_imbalanceRetries > 2U)
            {
                EnterFault(FAULT_MOTOR_IMBALANCE, LOG_EVENT_FAULT);
            }
            else
            {
                EnterState(WSTATE_SPIN_REBALANCE, 0U);
            }
        }
        else if (s_phaseMs >= 3500U)
        {
            EnterState(WSTATE_SPIN, LOG_EVENT_SPIN);
        }
        break;

    case WSTATE_SPIN:
        command->drain = 1U;
        command->motorPercent = 100U;
        command->motorDirection = 1;
        command->targetRpm = (int16_t)s_recipe.spinRpm;
        if (s_phaseMs >= s_recipe.spinTimeMs)
            EnterState(WSTATE_FINISH_STOP, 0U);
        break;

    case WSTATE_SPIN_REBALANCE:
        command->drain = 1U;
        command->motorPercent = 22U;
        command->motorDirection = 1;
        command->targetRpm = 180;
        if (s_phaseMs >= 2500U)
            EnterState(WSTATE_SPIN_UP, 0U);
        break;

    case WSTATE_FINISH_STOP:
        command->drain = 1U;
        command->motorPercent = 0U;
        if (plant->drumRpm < 50 && plant->waterMl <= 100U)
            EnterState(WSTATE_UNLOCK, LOG_EVENT_COMPLETE);
        else if (s_phaseMs >= 5000U)
            EnterFault(FAULT_SPIN_STOP_TIMEOUT, LOG_EVENT_FAULT);
        break;

    case WSTATE_UNLOCK:
        command->doorLock = 0U;
        if (s_phaseMs >= 1200U)
        {
            s_cycleComplete = 1U;
            EnterState(WSTATE_IDLE, 0U);
        }
        break;

    case WSTATE_FAULT:
    case WSTATE_IDLE:
    default:
        break;
    }

    if (event == 0U)
        event = s_lastEvent;
    s_lastEvent = 0U;
    FillUi(sensor, plant, command, ui);
    *eventOut = event;
}
