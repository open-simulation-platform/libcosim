#include "fmi3Functions.h"
#include "model.h"

#include <float.h>
#include <math.h>


fmi3Status reference_fmi3EnterInitializationMode(
    fmi3Instance instance,
    fmi3Boolean toleranceDefined,
    fmi3Float64 tolerance,
    fmi3Float64 startTime,
    fmi3Boolean stopTimeDefined,
    fmi3Float64 stopTime);
fmi3Status reference_fmi3ExitInitializationMode(fmi3Instance instance);
fmi3Status reference_fmi3EnterConfigurationMode(fmi3Instance instance);
fmi3Status reference_fmi3ExitConfigurationMode(fmi3Instance instance);
fmi3Status reference_fmi3Terminate(fmi3Instance instance);
fmi3Status reference_fmi3Reset(fmi3Instance instance);
fmi3Status reference_fmi3GetInt32(
    fmi3Instance instance,
    const fmi3ValueReference valueReferences[],
    size_t nValueReferences,
    fmi3Int32 values[],
    size_t nValues);
fmi3Status reference_fmi3SetInt32(
    fmi3Instance instance,
    const fmi3ValueReference valueReferences[],
    size_t nValueReferences,
    const fmi3Int32 values[],
    size_t nValues);
fmi3Status reference_fmi3GetFMUState(
    fmi3Instance instance,
    fmi3FMUState* FMUState);
fmi3Status reference_fmi3SetFMUState(
    fmi3Instance instance,
    fmi3FMUState FMUState);
fmi3Status reference_fmi3FreeFMUState(
    fmi3Instance instance,
    fmi3FMUState* FMUState);
fmi3Status reference_fmi3SerializedFMUStateSize(
    fmi3Instance instance,
    fmi3FMUState FMUState,
    size_t* size);
fmi3Status reference_fmi3SerializeFMUState(
    fmi3Instance instance,
    fmi3FMUState FMUState,
    fmi3Byte serializedState[],
    size_t size);
fmi3Status reference_fmi3DeserializeFMUState(
    fmi3Instance instance,
    const fmi3Byte serializedState[],
    size_t size,
    fmi3FMUState* FMUState);
fmi3Status reference_fmi3UpdateDiscreteStates(
    fmi3Instance instance,
    fmi3Boolean* discreteStatesNeedUpdate,
    fmi3Boolean* terminateSimulation,
    fmi3Boolean* nominalsOfContinuousStatesChanged,
    fmi3Boolean* valuesOfContinuousStatesChanged,
    fmi3Boolean* nextEventTimeDefined,
    fmi3Float64* nextEventTime);
fmi3Status reference_fmi3DoStep(
    fmi3Instance instance,
    fmi3Float64 currentCommunicationPoint,
    fmi3Float64 communicationStepSize,
    fmi3Boolean noSetFMUStatePriorToCurrentPoint,
    fmi3Boolean* eventHandlingNeeded,
    fmi3Boolean* terminateSimulation,
    fmi3Boolean* earlyReturn,
    fmi3Float64* lastSuccessfulTime);


static Status requestedStatus(
    fmi3Instance instance,
    FaultOperation operation)
{
    if (instance == NULL) {
        return Error;
    }

    ModelInstance* comp = (ModelInstance*)instance;
    const FaultOperation selected = (FaultOperation)M(fault_operation);
    if (selected == operation ||
        (selected == fault_operation_legacy_step &&
         operation == fault_operation_do_step)) {
        return (Status)M(fault_status);
    }
    return OK;
}


static fmi3Status injectedStatus(
    Status requested,
    fmi3Status referenceStatus)
{
    return referenceStatus == fmi3OK && requested == Warning
        ? fmi3Warning
        : referenceStatus;
}


#define RETURN_INJECTED_STATUS(INSTANCE, OPERATION, CALL) \
    do {                                                  \
        const Status requested =                          \
            requestedStatus(INSTANCE, OPERATION);         \
        if (requested >= Discard) {                       \
            return (fmi3Status)requested;                 \
        }                                                 \
        return injectedStatus(requested, (CALL));         \
    } while (0)


fmi3Status fmi3EnterInitializationMode(
    fmi3Instance instance,
    fmi3Boolean toleranceDefined,
    fmi3Float64 tolerance,
    fmi3Float64 startTime,
    fmi3Boolean stopTimeDefined,
    fmi3Float64 stopTime)
{
    RETURN_INJECTED_STATUS(
        instance,
        fault_operation_enter_initialization,
        reference_fmi3EnterInitializationMode(
            instance,
            toleranceDefined,
            tolerance,
            startTime,
            stopTimeDefined,
            stopTime));
}


fmi3Status fmi3ExitInitializationMode(fmi3Instance instance)
{
    RETURN_INJECTED_STATUS(
        instance,
        fault_operation_exit_initialization,
        reference_fmi3ExitInitializationMode(instance));
}


fmi3Status fmi3EnterConfigurationMode(fmi3Instance instance)
{
    RETURN_INJECTED_STATUS(
        instance,
        fault_operation_enter_configuration,
        reference_fmi3EnterConfigurationMode(instance));
}


fmi3Status fmi3ExitConfigurationMode(fmi3Instance instance)
{
    RETURN_INJECTED_STATUS(
        instance,
        fault_operation_exit_configuration,
        reference_fmi3ExitConfigurationMode(instance));
}


fmi3Status fmi3Terminate(fmi3Instance instance)
{
    RETURN_INJECTED_STATUS(
        instance,
        fault_operation_terminate,
        reference_fmi3Terminate(instance));
}


fmi3Status fmi3Reset(fmi3Instance instance)
{
    RETURN_INJECTED_STATUS(
        instance,
        fault_operation_reset,
        reference_fmi3Reset(instance));
}


fmi3Status fmi3GetInt32(
    fmi3Instance instance,
    const fmi3ValueReference valueReferences[],
    size_t nValueReferences,
    fmi3Int32 values[],
    size_t nValues)
{
    RETURN_INJECTED_STATUS(
        instance,
        fault_operation_get_int32,
        reference_fmi3GetInt32(
            instance,
            valueReferences,
            nValueReferences,
            values,
            nValues));
}


fmi3Status fmi3SetInt32(
    fmi3Instance instance,
    const fmi3ValueReference valueReferences[],
    size_t nValueReferences,
    const fmi3Int32 values[],
    size_t nValues)
{
    RETURN_INJECTED_STATUS(
        instance,
        fault_operation_set_int32,
        reference_fmi3SetInt32(
            instance,
            valueReferences,
            nValueReferences,
            values,
            nValues));
}


fmi3Status fmi3GetFMUState(
    fmi3Instance instance,
    fmi3FMUState* FMUState)
{
    RETURN_INJECTED_STATUS(
        instance,
        fault_operation_get_fmu_state,
        reference_fmi3GetFMUState(instance, FMUState));
}


fmi3Status fmi3SetFMUState(
    fmi3Instance instance,
    fmi3FMUState FMUState)
{
    RETURN_INJECTED_STATUS(
        instance,
        fault_operation_set_fmu_state,
        reference_fmi3SetFMUState(instance, FMUState));
}


fmi3Status fmi3FreeFMUState(
    fmi3Instance instance,
    fmi3FMUState* FMUState)
{
    RETURN_INJECTED_STATUS(
        instance,
        fault_operation_free_fmu_state,
        reference_fmi3FreeFMUState(instance, FMUState));
}


fmi3Status fmi3SerializedFMUStateSize(
    fmi3Instance instance,
    fmi3FMUState FMUState,
    size_t* size)
{
    RETURN_INJECTED_STATUS(
        instance,
        fault_operation_serialized_fmu_state_size,
        reference_fmi3SerializedFMUStateSize(instance, FMUState, size));
}


fmi3Status fmi3SerializeFMUState(
    fmi3Instance instance,
    fmi3FMUState FMUState,
    fmi3Byte serializedState[],
    size_t size)
{
    RETURN_INJECTED_STATUS(
        instance,
        fault_operation_serialize_fmu_state,
        reference_fmi3SerializeFMUState(
            instance,
            FMUState,
            serializedState,
            size));
}


fmi3Status fmi3DeserializeFMUState(
    fmi3Instance instance,
    const fmi3Byte serializedState[],
    size_t size,
    fmi3FMUState* FMUState)
{
    RETURN_INJECTED_STATUS(
        instance,
        fault_operation_deserialize_fmu_state,
        reference_fmi3DeserializeFMUState(
            instance,
            serializedState,
            size,
            FMUState));
}


fmi3Status fmi3UpdateDiscreteStates(
    fmi3Instance instance,
    fmi3Boolean* discreteStatesNeedUpdate,
    fmi3Boolean* terminateSimulation,
    fmi3Boolean* nominalsOfContinuousStatesChanged,
    fmi3Boolean* valuesOfContinuousStatesChanged,
    fmi3Boolean* nextEventTimeDefined,
    fmi3Float64* nextEventTime)
{
    ModelInstance* comp = (ModelInstance*)instance;
    if (comp && M(fault_operation) == fault_operation_invalid_next_event) {
        const fmi3Status status = reference_fmi3UpdateDiscreteStates(
            instance, discreteStatesNeedUpdate, terminateSimulation,
            nominalsOfContinuousStatesChanged, valuesOfContinuousStatesChanged,
            nextEventTimeDefined, nextEventTime);
        if (status == fmi3OK) {
            *nextEventTimeDefined = fmi3True;
            *nextEventTime = M(fault_status) == 0 ? -1.0 :
                M(fault_status) == 1 ? NAN : DBL_MAX;
        }
        return status;
    }
    RETURN_INJECTED_STATUS(
        instance,
        fault_operation_update_discrete_states,
        reference_fmi3UpdateDiscreteStates(
            instance,
            discreteStatesNeedUpdate,
            terminateSimulation,
            nominalsOfContinuousStatesChanged,
            valuesOfContinuousStatesChanged,
            nextEventTimeDefined,
            nextEventTime));
}


fmi3Status fmi3DoStep(
    fmi3Instance instance,
    fmi3Float64 currentCommunicationPoint,
    fmi3Float64 communicationStepSize,
    fmi3Boolean noSetFMUStatePriorToCurrentPoint,
    fmi3Boolean* eventHandlingNeeded,
    fmi3Boolean* terminateSimulation,
    fmi3Boolean* earlyReturn,
    fmi3Float64* lastSuccessfulTime)
{
    ModelInstance* comp = (ModelInstance*)instance;
    if (comp && M(fault_operation) == fault_operation_terminate_after_step) {
        const fmi3Status status = reference_fmi3DoStep(
            instance,
            currentCommunicationPoint,
            communicationStepSize,
            noSetFMUStatePriorToCurrentPoint,
            eventHandlingNeeded,
            terminateSimulation,
            earlyReturn,
            lastSuccessfulTime);
        if (status == fmi3OK || status == fmi3Warning) {
            *terminateSimulation = fmi3True;
        }
        return status;
    }
    if (comp && M(fault_operation) >= fault_operation_endpoint_early_return &&
        M(fault_operation) <= fault_operation_after_endpoint) {
        const fmi3Status status = reference_fmi3DoStep(
            instance, currentCommunicationPoint, communicationStepSize,
            noSetFMUStatePriorToCurrentPoint, eventHandlingNeeded,
            terminateSimulation, earlyReturn, lastSuccessfulTime);
        if (status == fmi3OK || status == fmi3Warning) {
            *earlyReturn = fmi3True;
            *eventHandlingNeeded = M(fault_operation) == fault_operation_endpoint_early_event;
            *lastSuccessfulTime = currentCommunicationPoint + communicationStepSize;
            if (M(fault_operation) == fault_operation_after_endpoint) {
                *lastSuccessfulTime = nextafter(*lastSuccessfulTime, INFINITY);
            }
        }
        return status;
    }
    RETURN_INJECTED_STATUS(
        instance,
        fault_operation_do_step,
        reference_fmi3DoStep(
            instance,
            currentCommunicationPoint,
            communicationStepSize,
            noSetFMUStatePriorToCurrentPoint,
            eventHandlingNeeded,
            terminateSimulation,
            earlyReturn,
            lastSuccessfulTime));
}
