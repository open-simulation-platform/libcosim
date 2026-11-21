#ifndef enumeration_config_h
#define enumeration_config_h

#include <stdbool.h>
#include <stdint.h>

#define MODEL_IDENTIFIER Enumeration
#define INSTANTIATION_TOKEN "{4D8C2B8A-2D7F-4D43-9C6D-8A0B4F2C7E91}"

#define CO_SIMULATION
#define GET_INT32
#define SET_INT32
#define GET_INT64
#define SET_INT64
#define GET_CLOCK
#define EVENT_UPDATE

#define FIXED_SOLVER_STEP 0.1
#define DEFAULT_STOP_TIME 10

typedef enum {
    vr_status = 1,
    vr_scalar_input = 2,
    vr_array_input = 3,
    vr_empty_input = 4,
    vr_scalar_output = 12,
    vr_array_output = 13,
    vr_empty_output = 14,
    vr_input_clock = 1001,
    vr_output_clock = 1002
} ValueReference;

typedef struct {
    int32_t status;
    int64_t scalar_input;
    int64_t array_input[2];
    int64_t scalar_output;
    int64_t array_output[2];
    bool input_clock;
    bool output_clock;
} ModelData;

#endif
