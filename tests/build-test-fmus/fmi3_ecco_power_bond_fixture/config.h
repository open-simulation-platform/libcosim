#ifndef config_h
#define config_h

#define MODEL_IDENTIFIER PowerBond
#define INSTANTIATION_TOKEN "{0CE2A5F4-5EE2-48B4-89F0-F6A09BB6D7B8}"

#define CO_SIMULATION
#define SET_FLOAT64

#define DEFAULT_STOP_TIME 10

#ifdef _MSC_VER
extern __declspec(thread) double variableSolverStepSize;
#else
extern _Thread_local double variableSolverStepSize;
#endif

typedef enum {
    vr_time,
    vr_input,
    vr_output,
    vr_gain,
    vr_bias
} ValueReference;

typedef struct {
    double input;
    double output;
    double gain;
    double bias;
} ModelData;

#endif
