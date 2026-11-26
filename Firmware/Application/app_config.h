#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "stm32f10x.h"

/* Proteus 仿真工程的 STM32F103C8T6 引脚分配。 */
#define PIN_LOAD_ADC        GPIO_Pin_0
#define PIN_SOIL_ADC        GPIO_Pin_1
#define PIN_DOOR_SWITCH     GPIO_Pin_2
#define PIN_MOTOR_PWM       GPIO_Pin_6
#define PIN_BUZZER          GPIO_Pin_7

/* 新增中文 OLED 使用 PB0/PB1，手动翻页按钮使用 PA3。 */
#define PIN_OLED_SCL        GPIO_Pin_0
#define PIN_OLED_SDA        GPIO_Pin_1
#define PIN_BUTTON_PAGE     GPIO_Pin_3

/* 保留旧液晶接口的引脚宏，便于兼容已有板级初始化。 */
#define PIN_LCD_RS          GPIO_Pin_0
#define PIN_LCD_EN          GPIO_Pin_1
#define PIN_LCD_D4          GPIO_Pin_3
#define PIN_LCD_D5          GPIO_Pin_4
#define PIN_LCD_D6          GPIO_Pin_5
#define PIN_LCD_D7          GPIO_Pin_15

#define PIN_MOTOR_IN1       GPIO_Pin_6
#define PIN_MOTOR_IN2       GPIO_Pin_7
#define PIN_INLET           GPIO_Pin_8
#define PIN_DRAIN           GPIO_Pin_9
#define PIN_HEATER          GPIO_Pin_10
#define PIN_DOOR_LOCK       GPIO_Pin_11

#define PIN_BUTTON_MODE     GPIO_Pin_12
#define PIN_BUTTON_START    GPIO_Pin_13
#define PIN_BUTTON_STOP     GPIO_Pin_14

#define ADC_CHANNEL_LOAD    0
#define ADC_CHANNEL_SOIL    1

#define CONTROL_PERIOD_MS       20
#define PLANT_PERIOD_MS         50
#define INPUT_PERIOD_MS         20
#define SAFETY_PERIOD_MS        10
#define DISPLAY_PERIOD_MS       100
#define DISPLAY_PAGE_PERIOD_MS  3000

#define SENSOR_QUEUE_LENGTH     8
#define COMMAND_QUEUE_LENGTH    8
#define ACTUATOR_QUEUE_LENGTH   4
#define EVENT_QUEUE_LENGTH      16

#define TASK_STACK_SAFETY       192
#define TASK_STACK_CONTROL      384
#define TASK_STACK_PLANT        192
#define TASK_STACK_INPUT        192
#define TASK_STACK_DISPLAY      256
#define TASK_STACK_LOGGER       384

#define PRIORITY_SAFETY         5
#define PRIORITY_CONTROL        4
#define PRIORITY_PLANT          3
#define PRIORITY_INPUT          3
#define PRIORITY_DISPLAY        2
#define PRIORITY_LOGGER         1

#define TICK_TO_MS(t)           ((uint32_t)(t) / portTICK_RATE_MS)
#define MS_TO_TICKS(ms)         ((portTickType)((ms) / portTICK_RATE_MS))

#define LOG_FLASH_ADDRESS       0x0800FC00UL
#define LOG_FLASH_PAGE_SIZE     1024UL
#define LOG_MAX_RECORDS         64U
#define LOG_RECORD_MAGIC        0x574DU

#define PLANT_MAX_WATER_ML      6000L
#define PLANT_AMBIENT_MILLI_C   25000L
#define PLANT_INLET_ML_PER_SEC  900L
#define PLANT_DRAIN_ML_PER_SEC  1600L
#define PLANT_HEAT_MILLI_C_SEC  1500L
#define PLANT_COOL_MILLI_C_SEC  20L
#define PLANT_MAX_RPM           1200
#define PLANT_IMBALANCE_FAULT   700U

#endif

