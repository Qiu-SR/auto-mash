#ifndef BSP_H
#define BSP_H

#include "stdint.h"
#include "washer_types.h"

void BSP_Init(void);
void BSP_ApplyActuators(const ActuatorCommand_t *command);
void BSP_EmergencySafe(void);
void BSP_ClearEmergencyLockout(void);
void BSP_SetMotor(uint8_t percent, int8_t direction);
void BSP_SetBuzzer(uint8_t on);

uint8_t BSP_ReadButtonMask(void);
uint8_t BSP_ReadDoorOpen(void);
uint16_t BSP_ReadLoadPermille(void);
uint16_t BSP_ReadSoilPermille(void);

void BSP_DelayUs(uint32_t us);
void BSP_DelayMs(uint32_t ms);

uint8_t UART1_ReadChar(char *value);
uint32_t UART1_RxDropped(void);
void UART1_ReceiveIrq(void);

void UART1_WriteChar(char value);
void UART1_WriteString(const char *text);
void UART1_Printf(const char *format, ...);

#endif

