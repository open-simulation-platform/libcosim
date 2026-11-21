#ifndef status_fault_config_h
#define status_fault_config_h

#include <stdint.h>

#define MODEL_IDENTIFIER StatusFault
#define INSTANTIATION_TOKEN "{610F836A-58B3-4D2D-BB8C-9D60A80F6453}"

#define CO_SIMULATION
#define GET_INT32
#define SET_INT32
#define EVENT_UPDATE

#define FIXED_SOLVER_STEP 0.1
#define DEFAULT_STOP_TIME 10

typedef enum {
    vr_fault_status = 1,
    vr_fault_operation = 2,
    vr_value = 3
} ValueReference;

typedef enum {
    fault_operation_legacy_step = 0,
    fault_operation_enter_initialization = 1,
    fault_operation_exit_initialization = 2,
    fault_operation_enter_configuration = 3,
    fault_operation_exit_configuration = 4,
    fault_operation_update_discrete_states = 5,
    fault_operation_get_int32 = 6,
    fault_operation_set_int32 = 7,
    fault_operation_get_fmu_state = 8,
    fault_operation_set_fmu_state = 9,
    fault_operation_free_fmu_state = 10,
    fault_operation_serialized_fmu_state_size = 11,
    fault_operation_serialize_fmu_state = 12,
    fault_operation_deserialize_fmu_state = 13,
    fault_operation_terminate = 14,
    fault_operation_reset = 15,
    fault_operation_do_step = 16,
    fault_operation_terminate_after_step = 17,
    fault_operation_invalid_next_event = 18,
    fault_operation_endpoint_early_return = 19,
    fault_operation_endpoint_early_event = 20,
    fault_operation_after_endpoint = 21
} FaultOperation;

typedef struct {
    int32_t fault_status;
    int32_t fault_operation;
    int32_t value;
} ModelData;

#endif
