
#include <stdio.h>
#include <assert.h>
#include "washer_control.h"
#include "washer_plant.h"
static SensorInput_t sensor;
static ActuatorCommand_t cmd;
static UiSnapshot_t ui;
static unsigned char event;
void BSP_ClearEmergencyLockout(void) {}
static void step(unsigned char keys) {
    Control_Update(&sensor, Plant_GetState(), keys, &cmd, &ui, &event);
    Plant_Update(&cmd,sensor.loadPermille,sensor.soilPermille,20);
}
static void init(void) {
    sensor.loadPermille=400; sensor.soilPermille=400;
    sensor.doorOpen=0; sensor.selfTestRequested=0;
    Plant_SetFaultMask(0); Plant_Init(); Control_Init(); step(0);
    step(1); step(0); /* QUICK */
}
static void run(unsigned ms) {unsigned i;for(i=0;i<ms/20;i++)step(0);}
int main(void) {
    init(); step(2); run(60000);
    assert(ui.cycleComplete && ui.state==WSTATE_IDLE); puts("quick complete PASS");
    init(); Plant_SetFaultMask(PLANT_FAULT_INLET);step(2);run(32000);
    assert(ui.fault==FAULT_FILL_TIMEOUT && ui.waterMl==0);puts("inlet timeout PASS");
    init(); Plant_SetFaultMask(PLANT_FAULT_DRAIN);step(2);run(60000);
    assert(ui.fault==FAULT_DRAIN_TIMEOUT && ui.waterMl>100);puts("drain timeout PASS");
    init(); Plant_SetFaultMask(PLANT_FAULT_IMBALANCE);step(2);run(60000);
    assert(ui.fault==FAULT_MOTOR_IMBALANCE); puts("imbalance retries PASS");
    init();step(2);run(8000);step(2);run(2000);
    assert(ui.paused && Plant_GetState()->drumRpm==0);
    step(2);run(1000);assert(!ui.paused);puts("pause resume PASS");
    Control_ForceFault(FAULT_OVER_TEMPERATURE);step(0);
    assert(ui.fault==FAULT_OVER_TEMPERATURE && !cmd.heater && !cmd.motorPercent && cmd.drain);
    puts("overtemp branch PASS");
    init();sensor.selfTestRequested=1;step(0);sensor.selfTestRequested=0;run(5000);
    assert(ui.state==WSTATE_IDLE && ui.fault==FAULT_NONE);puts("selftest restore PASS");
    return 0;
}
