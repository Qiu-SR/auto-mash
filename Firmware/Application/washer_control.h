#ifndef WASHER_CONTROL_H
#define WASHER_CONTROL_H

#include "washer_types.h"

void Control_Init(void);
void Control_Update(const SensorInput_t *sensor,
                    const PlantState_t *plant,
                    uint8_t buttonMask,
                    ActuatorCommand_t *command,
                    UiSnapshot_t *ui,
                    uint8_t *eventOut);
void Control_ForceFault(WashingFault_t fault);
uint8_t Control_IsActive(void);

#endif

