#include "bsp.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "lcd1602.h"

static volatile uint32_t s_dummy;
static volatile uint8_t s_rxHead;
static volatile uint8_t s_rxTail;
static volatile uint32_t s_rxDropped;
static char s_rxBuffer[64];

/* 串口接收中断：将字节写入环形缓冲区，记录溢出，不调用操作系统接口。 */
void UART1_ReceiveIrq(void)
{
    uint32_t status = USART1->SR;
    uint8_t value = (uint8_t)USART1->DR;
    uint8_t next = (uint8_t)((s_rxHead + 1U) % sizeof(s_rxBuffer));
    if ((status & USART_SR_ORE) != 0U)
        ++s_rxDropped;
    if ((status & USART_SR_RXNE) == 0U)
        return;
    if (next == s_rxTail)
        ++s_rxDropped;
    else
    {
        s_rxBuffer[s_rxHead] = (char)value;
        s_rxHead = next;
    }
}

/* 从接收缓冲区取一个字符；返回 1 表示成功，返回 0 表示缓冲区为空。 */
uint8_t UART1_ReadChar(char *value)
{
    if (s_rxTail == s_rxHead)
        return 0U;
    *value = s_rxBuffer[s_rxTail];
    s_rxTail = (uint8_t)((s_rxTail + 1U) % sizeof(s_rxBuffer));
    return 1U;
}

/* 返回接收缓冲区满和硬件溢出的累计计数。 */
uint32_t UART1_RxDropped(void)
{
    return s_rxDropped;
}

static volatile uint8_t s_emergencyLockout;

/* 配置电机方向、执行器、液晶、按键和门开关引脚。 */
static void BSP_ConfigureGpio(void)
{
    GPIO_InitTypeDef gpio;

    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;

    /* 液晶屏使用 PB3/PB4，因此关闭 JTAG，并保留 PA13/PA14 上的 SWD 调试接口。 */
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    gpio.GPIO_Pin = PIN_BUZZER;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = PIN_MOTOR_IN1 | PIN_MOTOR_IN2 | PIN_INLET | PIN_DRAIN |
                    PIN_HEATER | PIN_DOOR_LOCK | PIN_LCD_RS | PIN_LCD_EN |
                    PIN_LCD_D4 | PIN_LCD_D5 | PIN_LCD_D6 | PIN_LCD_D7;
    GPIO_Init(GPIOB, &gpio);

    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Pin = PIN_BUTTON_MODE | PIN_BUTTON_START | PIN_BUTTON_STOP;
    GPIO_Init(GPIOB, &gpio);

    gpio.GPIO_Pin = PIN_DOOR_SWITCH | PIN_BUTTON_PAGE;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Mode = GPIO_Mode_AIN;
    gpio.GPIO_Pin = PIN_LOAD_ADC | PIN_SOIL_ADC;
    GPIO_Init(GPIOA, &gpio);

    GPIO_ResetBits(GPIOA, PIN_BUZZER);
    GPIO_ResetBits(GPIOB, PIN_INLET | PIN_DRAIN | PIN_HEATER |
                          PIN_DOOR_LOCK | PIN_MOTOR_IN1 | PIN_MOTOR_IN2);
}

/* 初始化串口及接收中断，使用 115200 波特率、8 位数据、无校验、1 位停止位。 */
static void BSP_ConfigureUart(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef uart;

    gpio.GPIO_Pin = GPIO_Pin_9;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &gpio);

    uart.USART_BaudRate = 115200;
    uart.USART_WordLength = USART_WordLength_8b;
    uart.USART_StopBits = USART_StopBits_1;
    uart.USART_Parity = USART_Parity_No;
    uart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    uart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &uart);
    USART_Cmd(USART1, ENABLE);
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    NVIC_SetPriority(USART1_IRQn, 14U);
    NVIC_EnableIRQ(USART1_IRQn);
}

/* 配置重量和污浊度两路模拟输入使用的模数转换器。 */
static void BSP_ConfigureAdc(void)
{
    ADC_InitTypeDef adc;

    RCC_ADCCLKConfig(RCC_PCLK2_Div2);

    adc.ADC_Mode = ADC_Mode_Independent;
    adc.ADC_ScanConvMode = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &adc);

    ADC_Cmd(ADC1, ENABLE);
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1) != RESET)
    {
    }
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1) != RESET)
    {
    }
}

/* 配置电机调速所需的定时器 PWM 输出。 */
static void BSP_ConfigureMotorPwm(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef timeBase;
    TIM_OCInitTypeDef outputCompare;

    gpio.GPIO_Pin = PIN_MOTOR_PWM;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &gpio);

    /* 课堂仿真使用 1 kHz PWM：8 MHz / (79+1) / (99+1)。占空比刻度不变，减少电机驱动模型的开关事件。 */
    timeBase.TIM_Prescaler = 79;
    timeBase.TIM_CounterMode = TIM_CounterMode_Up;
    timeBase.TIM_Period = 99;
    timeBase.TIM_ClockDivision = TIM_CKD_DIV1;
    timeBase.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM3, &timeBase);

    outputCompare.TIM_OCMode = TIM_OCMode_PWM1;
    outputCompare.TIM_OutputState = TIM_OutputState_Enable;
    outputCompare.TIM_OCPolarity = TIM_OCPolarity_High;
    outputCompare.TIM_Pulse = 0;
    TIM_OC1Init(TIM3, &outputCompare);
    TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM3, ENABLE);
    TIM_Cmd(TIM3, ENABLE);
}

static uint16_t BSP_ReadAdcAveraged(uint8_t channel)
{
    uint32_t sum = 0U;
    uint8_t index;

    for (index = 0U; index < 4U; ++index)
    {
        ADC_RegularChannelConfig(ADC1, channel, 1U, ADC_SampleTime_55Cycles5);
        ADC_SoftwareStartConvCmd(ADC1, ENABLE);
        while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET)
        {
        }
        sum += ADC_GetConversionValue(ADC1);
    }
    return (uint16_t)(sum / 4U);
}

/* 初始化板级时钟、引脚、串口、模数转换、电机调速和液晶。 */
void BSP_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB | RCC_APB2Periph_ADC1 |
                           RCC_APB2Periph_USART1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
    BSP_ConfigureGpio();
    BSP_ConfigureUart();
    BSP_ConfigureAdc();
    BSP_ConfigureMotorPwm();
    LCD_Init();
    BSP_EmergencySafe();
}

/* 设置电机占空比与旋转方向。 */
void BSP_SetMotor(uint8_t percent, int8_t direction)
{
    uint16_t compare;

    if (percent > 100U)
    {
        percent = 100U;
    }

    if (direction > 0)
    {
        GPIO_SetBits(GPIOB, PIN_MOTOR_IN1);
        GPIO_ResetBits(GPIOB, PIN_MOTOR_IN2);
    }
    else if (direction < 0)
    {
        GPIO_ResetBits(GPIOB, PIN_MOTOR_IN1);
        GPIO_SetBits(GPIOB, PIN_MOTOR_IN2);
    }
    else
    {
        GPIO_ResetBits(GPIOB, PIN_MOTOR_IN1 | PIN_MOTOR_IN2);
    }

    compare = (uint16_t)(((uint32_t)percent * 99U) / 100U);
    TIM_SetCompare1(TIM3, compare);
}

void BSP_SetBuzzer(uint8_t on)
{
    if (on != 0U)
    {
        GPIO_SetBits(GPIOA, PIN_BUZZER);
    }
    else
    {
        GPIO_ResetBits(GPIOA, PIN_BUZZER);
    }
}

/* 把状态机输出写到硬件引脚；急停锁定期间执行安全限制。 */
void BSP_ApplyActuators(const ActuatorCommand_t *command)
{
    if (command == 0)
    {
        return;
    }

    if (s_emergencyLockout != 0U)
    {
        GPIO_ResetBits(GPIOB, PIN_INLET | PIN_HEATER);
        GPIO_SetBits(GPIOB, PIN_DRAIN);
        BSP_SetMotor(0U, 0);
        BSP_SetBuzzer(1U);
        return;
    }

    if (command->inlet != 0U)
        GPIO_SetBits(GPIOB, PIN_INLET);
    else
        GPIO_ResetBits(GPIOB, PIN_INLET);

    if (command->drain != 0U)
        GPIO_SetBits(GPIOB, PIN_DRAIN);
    else
        GPIO_ResetBits(GPIOB, PIN_DRAIN);

    if (command->heater != 0U)
        GPIO_SetBits(GPIOB, PIN_HEATER);
    else
        GPIO_ResetBits(GPIOB, PIN_HEATER);

    if (command->doorLock != 0U)
        GPIO_SetBits(GPIOB, PIN_DOOR_LOCK);
    else
        GPIO_ResetBits(GPIOB, PIN_DOOR_LOCK);

    BSP_SetMotor(command->motorPercent, command->motorDirection);
    BSP_SetBuzzer(command->buzzer);
}

/* 锁定紧急安全输出，关闭电机与加热等危险动作。 */
void BSP_EmergencySafe(void)
{
    s_emergencyLockout = 1U;
    GPIO_ResetBits(GPIOB, PIN_INLET | PIN_HEATER);
    GPIO_SetBits(GPIOB, PIN_DRAIN);
    BSP_SetMotor(0U, 0);
}

/* 解除硬件输出的急停锁定，供复位或重新启动使用。 */
void BSP_ClearEmergencyLockout(void)
{
    s_emergencyLockout = 0U;
}

/* 读取三个低电平有效的按键，并转换为按键位掩码。 */
uint8_t BSP_ReadButtonMask(void)
{
    uint8_t mask = 0U;

    if (GPIO_ReadInputDataBit(GPIOB, PIN_BUTTON_MODE) == Bit_RESET)
        mask |= 0x01U;
    if (GPIO_ReadInputDataBit(GPIOB, PIN_BUTTON_START) == Bit_RESET)
        mask |= 0x02U;
    if (GPIO_ReadInputDataBit(GPIOB, PIN_BUTTON_STOP) == Bit_RESET)
        mask |= 0x04U;
    if (GPIO_ReadInputDataBit(GPIOA, PIN_BUTTON_PAGE) == Bit_RESET)
        mask |= 0x08U;
    return mask;
}

uint8_t BSP_ReadDoorOpen(void)
{
    /* 门开关采用上拉输入；关门时将 PA2 接地，低电平表示门已关闭。 */
    return (GPIO_ReadInputDataBit(GPIOA, PIN_DOOR_SWITCH) == Bit_SET) ? 1U : 0U;
}

/* 读取衣物重量模拟量，并换算为 0～1000 的千分比。 */
uint16_t BSP_ReadLoadPermille(void)
{
    return (uint16_t)(((uint32_t)BSP_ReadAdcAveraged(ADC_CHANNEL_LOAD) *
                       1000U) / 4095U);
}

/* 读取衣物污浊度模拟量，并换算为 0～1000 的千分比。 */
uint16_t BSP_ReadSoilPermille(void)
{
    return (uint16_t)(((uint32_t)BSP_ReadAdcAveraged(ADC_CHANNEL_SOIL) *
                       1000U) / 4095U);
}

void BSP_DelayUs(uint32_t us)
{
    uint32_t cycles = us * (SystemCoreClock / 1000000UL);
    while (cycles-- != 0UL)
    {
        s_dummy++;
    }
}

void BSP_DelayMs(uint32_t ms)
{
    while (ms-- != 0UL)
    {
        BSP_DelayUs(1000UL);
    }
}

void UART1_WriteChar(char value)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)
    {
    }
    USART_SendData(USART1, (uint16_t)(uint8_t)value);
}

void UART1_WriteString(const char *text)
{
    while (*text != '\0')
    {
        UART1_WriteChar(*text++);
    }
}

void UART1_Printf(const char *format, ...)
{
    char buffer[96];
    va_list args;

    va_start(args, format);
    (void)vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    UART1_WriteString(buffer);
}

