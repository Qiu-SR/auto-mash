#include "washer_types.h"

const char *Washer_StateName(WashingState_t state)
{
    switch (state)
    {
    case WSTATE_IDLE:           return "IDLE";
    case WSTATE_LOCKING:        return "LOCKING";
    case WSTATE_FILLING:        return "FILLING";
    case WSTATE_HEATING:        return "HEATING";
    case WSTATE_WASH_FORWARD:   return "WASH FWD";
    case WSTATE_WASH_PAUSE:     return "WASH PAUSE";
    case WSTATE_WASH_REVERSE:   return "WASH REV";
    case WSTATE_DRAIN:          return "DRAINING";
    case WSTATE_RINSE_FILL:     return "RINSE FILL";
    case WSTATE_RINSE_FORWARD:  return "RINSE FWD";
    case WSTATE_RINSE_PAUSE:    return "RINSE PAUSE";
    case WSTATE_RINSE_REVERSE:  return "RINSE REV";
    case WSTATE_SPIN_UP:        return "SPIN UP";
    case WSTATE_SPIN:           return "SPIN";
    case WSTATE_SPIN_REBALANCE: return "REBALANCE";
    case WSTATE_FINISH_STOP:    return "STOPPING";
    case WSTATE_UNLOCK:         return "UNLOCK";
    case WSTATE_FAULT:          return "FAULT";
    default:                    return "UNKNOWN";
    }
}

const char *Washer_ProgramName(WashingProgram_t program)
{
    switch (program)
    {
    case PROGRAM_AUTO:     return "AUTO";
    case PROGRAM_QUICK:    return "QUICK";
    case PROGRAM_STANDARD: return "STD";
    case PROGRAM_HEAVY:    return "HEAVY";
    case PROGRAM_WOOL:     return "WOOL";
    default:               return "?";
    }
}

const char *Washer_FaultName(WashingFault_t fault)
{
    switch (fault)
    {
    case FAULT_NONE:              return "NONE";
    case FAULT_DOOR_OPEN:         return "DOOR OPEN";
    case FAULT_FILL_TIMEOUT:      return "FILL TIMEOUT";
    case FAULT_DRAIN_TIMEOUT:     return "DRAIN TIMEOUT";
    case FAULT_OVER_TEMPERATURE:  return "OVER TEMP";
    case FAULT_MOTOR_IMBALANCE:   return "IMBALANCE";
    case FAULT_EMERGENCY_STOP:    return "E-STOP";
    case FAULT_SPIN_STOP_TIMEOUT: return "STOP TIMEOUT";
    default:                      return "UNKNOWN";
    }
}

const char *Washer_EventName(LogEvent_t event)
{
    switch (event)
    {
    case LOG_EVENT_BOOT:        return "BOOT";
    case LOG_EVENT_CYCLE_START: return "START";
    case LOG_EVENT_DOOR_LOCK:   return "LOCK";
    case LOG_EVENT_FILL:        return "FILL";
    case LOG_EVENT_HEAT:        return "HEAT";
    case LOG_EVENT_WASH:        return "WASH";
    case LOG_EVENT_DRAIN:       return "DRAIN";
    case LOG_EVENT_RINSE:       return "RINSE";
    case LOG_EVENT_SPIN:        return "SPIN";
    case LOG_EVENT_PAUSE:       return "PAUSE";
    case LOG_EVENT_RESUME:      return "RESUME";
    case LOG_EVENT_COMPLETE:    return "DONE";
    case LOG_EVENT_FAULT:       return "FAULT";
    case LOG_EVENT_RESET:       return "RESET";
    default:                    return "UNKNOWN";
    }
}
