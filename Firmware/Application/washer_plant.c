#include "washer_plant.h"

#include "app_config.h"

static PlantState_t s_plant;
static volatile uint8_t s_faultMask;

/* 设置对象模型故障掩码：进水堵塞为 1，排水堵塞为 2，不平衡为 4。 */
void Plant_SetFaultMask(uint8_t mask)
{
    s_faultMask = mask;
}

/* 读取当前故障注入掩码；多个注入条件可以同时启用。 */
uint8_t Plant_GetFaultMask(void)
{
    return s_faultMask;
}

static int32_t s_tempMilliC;
static int32_t s_rpmMilli;
static uint32_t s_modelSubTick;

static int32_t Abs32(int32_t value)
{
    return value < 0 ? -value : value;
}

static int16_t ClampRpm(int32_t value)
{
    if (value > PLANT_MAX_RPM)
        return (int16_t)PLANT_MAX_RPM;
    if (value < -PLANT_MAX_RPM)
        return (int16_t)-PLANT_MAX_RPM;
    return (int16_t)value;
}

static uint16_t CalculateImbalance(uint16_t loadPermille)
{
    uint32_t imbalance = ((uint32_t)loadPermille * 700U) / 1000U;

    if (loadPermille > 850U)
        imbalance += 300U;
    if (imbalance > 1000U)
        imbalance = 1000U;
    return (uint16_t)imbalance;
}

/* 重置新周期的水量、温度、转速和浊度模型。 */
static void Plant_StartCycle(uint16_t loadPermille, uint16_t soilPermille)
{
    s_plant.waterMl = 0U;
    s_plant.waterTempDeciC = (uint16_t)(PLANT_AMBIENT_MILLI_C / 100L);
    s_plant.drumRpm = 0;
    s_plant.turbidityPermille = (uint16_t)(100U + ((uint32_t)soilPermille * 900U) / 1000U);
    s_plant.imbalancePermille = CalculateImbalance(loadPermille);
    s_plant.powerMw = 0U;
    s_tempMilliC = PLANT_AMBIENT_MILLI_C;
    s_rpmMilli = 0;
    s_modelSubTick = 0U;
}

/* 初始化教学用洗衣机对象模型。 */
void Plant_Init(void)
{
    Plant_StartCycle(300U, 300U);
}

/* 根据经过的毫秒数推进软件对象模型；这些数值不是实物传感器测量结果。 */
void Plant_Update(const ActuatorCommand_t *command,
                  uint16_t loadPermille,
                  uint16_t soilPermille,
                  uint32_t elapsedMs)
{
    int32_t inletAdd = 0;
    int32_t drainRemove;
    int32_t targetRpm;
    int32_t difference;
    int32_t step;
    uint32_t oldWater;
    uint32_t newWater;
    uint32_t powerMw = 0U;

    if (command == 0)
    {
        return;
    }

    if (command->cycleStart != 0U)
    {
        Plant_StartCycle(loadPermille, soilPermille);
    }
    if (command->inlet != 0U && (s_faultMask & PLANT_FAULT_INLET) == 0U)
    {
        inletAdd = (PLANT_INLET_ML_PER_SEC * (int32_t)elapsedMs) / 1000L;
        oldWater = s_plant.waterMl;
        if ((oldWater + (uint32_t)inletAdd) > PLANT_MAX_WATER_ML)
        {
            inletAdd = (int32_t)(PLANT_MAX_WATER_ML - oldWater);
        }
        if (inletAdd > 0)
        {
            newWater = oldWater + (uint32_t)inletAdd;
            /* 按水量加权混合新进冷水和桶内原有水；教学模型使用整数运算计算混合后的水温。 */
            s_tempMilliC =
                ((s_tempMilliC * (int32_t)oldWater) +
                 (PLANT_AMBIENT_MILLI_C * inletAdd)) /
                (int32_t)newWater;
            s_plant.waterMl = (uint16_t)newWater;
            s_plant.turbidityPermille =
                (uint16_t)(((uint32_t)s_plant.turbidityPermille * oldWater) /
                           newWater);
        }
        powerMw += 12000U;
    }

    if (command->drain != 0U && s_plant.waterMl > 0U &&
        (s_faultMask & PLANT_FAULT_DRAIN) == 0U)
    {
        drainRemove = (PLANT_DRAIN_ML_PER_SEC * (int32_t)elapsedMs) / 1000L;
        if (drainRemove > s_plant.waterMl)
            drainRemove = s_plant.waterMl;
        s_plant.waterMl = (uint16_t)(s_plant.waterMl - (uint16_t)drainRemove);
        powerMw += 8000U;
    }

    if (command->heater != 0U && s_plant.waterMl >= 600U)
    {
        s_tempMilliC +=
            (PLANT_HEAT_MILLI_C_SEC * (int32_t)elapsedMs) / 1000L;
        if (s_tempMilliC > 90000L)
            s_tempMilliC = 90000L;
        powerMw += 900000U;
    }
    else if (s_tempMilliC > PLANT_AMBIENT_MILLI_C)
    {
        s_tempMilliC -=
            (PLANT_COOL_MILLI_C_SEC * (int32_t)elapsedMs) / 1000L;
        if (s_tempMilliC < PLANT_AMBIENT_MILLI_C)
            s_tempMilliC = PLANT_AMBIENT_MILLI_C;
    }

    targetRpm = 0;
    if (command->motorPercent != 0U && command->targetRpm > 0)
    {
        targetRpm = command->targetRpm;
        if (command->motorDirection < 0)
            targetRpm = -targetRpm;
    }

    difference = (targetRpm * 1000L) - s_rpmMilli;
    step = (int32_t)elapsedMs * 700L;
    if (difference > 0)
    {
        s_rpmMilli += step;
        if (s_rpmMilli > targetRpm * 1000L)
            s_rpmMilli = targetRpm * 1000L;
    }
    else if (difference < 0)
    {
        s_rpmMilli -= step;
        if (s_rpmMilli < targetRpm * 1000L)
            s_rpmMilli = targetRpm * 1000L;
    }

    s_plant.drumRpm = ClampRpm(s_rpmMilli / 1000L);
    if (command->motorPercent != 0U)
    {
        powerMw += 15000U + (uint32_t)command->motorPercent * 450U;
    }

    /* 每隔 100 毫秒更新污浊度模型：新周期开始时将衣物污浊度映射为水体浊度，随后随搅动逐步降低。 */
    s_modelSubTick += elapsedMs;
    if (s_modelSubTick >= 100U)
    {
        s_modelSubTick -= 100U;
        if (s_plant.waterMl > 600U &&
            Abs32(s_plant.drumRpm) > 80 &&
            s_plant.turbidityPermille > 0U)
        {
            uint16_t drop =
                (uint16_t)(1U + ((uint16_t)Abs32(s_plant.drumRpm) / 90U) +
                           (s_plant.waterTempDeciC / 450U));
            if (drop > s_plant.turbidityPermille)
                s_plant.turbidityPermille = 0U;
            else
                s_plant.turbidityPermille -= drop;
        }

        if (s_plant.waterMl == 0U)
        {
            s_plant.imbalancePermille = CalculateImbalance(loadPermille);
        }
    }

    if ((s_faultMask & PLANT_FAULT_IMBALANCE) != 0U)
        s_plant.imbalancePermille = 950U;
    else
        s_plant.imbalancePermille = CalculateImbalance(loadPermille);
    s_plant.waterTempDeciC = (uint16_t)(s_tempMilliC / 100L);
    s_plant.powerMw = powerMw;
}

const PlantState_t *Plant_GetState(void)
{
    return &s_plant;
}

