#ifndef WASHER_PLANT_H
#define WASHER_PLANT_H

#include "washer_types.h"

#define PLANT_FAULT_INLET 1U
#define PLANT_FAULT_DRAIN 2U
#define PLANT_FAULT_IMBALANCE 4U

void Plant_SetFaultMask(uint8_t mask);
uint8_t Plant_GetFaultMask(void);
void Plant_Init(void);
void Plant_Update(const ActuatorCommand_t *command,
                  uint16_t loadPermille,
                  uint16_t soilPermille,
                  uint32_t elapsedMs);
const PlantState_t *Plant_GetState(void);

#endif

