#ifndef strict_clock_config_h
#define strict_clock_config_h

#include <stdint.h>

#define MODEL_IDENTIFIER StrictClock
#define INSTANTIATION_TOKEN "{73431829-2AB6-4D98-9AAA-32C23A4F2706}"

#define CO_SIMULATION
#define GET_INT32
#define SET_INT32
#define GET_UINT64
#define SET_UINT64
#define GET_CLOCK
#define ACTIVATE_CLOCK
#define EVENT_UPDATE

#define FIXED_SOLVER_STEP 0.1
#define DEFAULT_STOP_TIME 10

typedef enum {
    vr_periodic_clock = 100,
    vr_triggered_clock = 101,
    vr_output_clock = 102,
    vr_input = 200,
    vr_output = 201,
    vr_snapshot = 202,
    vr_updates = 203,
    vr_periodic_input = 204,
    vr_periodic_snapshot = 205,
    vr_periodic_array = 206,
    vr_periodic_array_snapshot = 207
} ValueReference;

typedef struct {
    uint64_t periodic_array[2];
    uint64_t periodic_array_snapshot[2];
    int32_t input;
    int32_t snapshot;
    int32_t updates;
    int32_t periodic_input;
    int32_t periodic_snapshot;
    int periodic_active;
    int triggered_active;
    int output_observed;
} ModelData;

#endif
