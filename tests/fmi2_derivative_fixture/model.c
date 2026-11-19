#include "fmi2Functions.h"

#include "derivative_fixture.h"

#include <stdlib.h>
#include <string.h>


typedef enum
{
    state_instantiated,
    state_initialization,
    state_step,
    state_terminated
} fixture_state;


typedef struct
{
    fmi2CallbackFunctions callbacks;
    fmi2Real known[3];
    fmi2Real unknown[2];
    fmi2Real time;
    fmi2Integer derivativeStatus;
    fmi2Integer directionalCallCount;
    fmi2Boolean nonReal;
    fixture_state state;
} derivative_instance;


static derivative_instance* instance_of(fmi2Component component)
{
    return (derivative_instance*)component;
}


static fmi2Status require_instance(fmi2Component component)
{
    return component == NULL ? fmi2Fatal : fmi2OK;
}


static void update_outputs(derivative_instance* instance)
{
    instance->unknown[0] =
        2.0 * instance->known[0] -
        3.0 * instance->known[1] +
        0.5 * instance->known[2] +
        instance->time;
    instance->unknown[1] =
        -instance->known[0] +
        4.0 * instance->known[1] +
        5.0 * instance->known[2] -
        instance->time;
}


static void reset_instance(derivative_instance* instance)
{
    memset(instance->known, 0, sizeof(instance->known));
    memset(instance->unknown, 0, sizeof(instance->unknown));
    instance->time = 0.0;
    instance->derivativeStatus = fmi2OK;
    instance->directionalCallCount = 0;
    instance->nonReal = fmi2False;
    instance->state = state_instantiated;
    update_outputs(instance);
}


static void* allocate_instance(
    const fmi2CallbackFunctions* callbacks,
    size_t count,
    size_t size)
{
    if (callbacks != NULL && callbacks->allocateMemory != NULL) {
        return callbacks->allocateMemory(count, size);
    }
    return calloc(count, size);
}


static void free_instance(derivative_instance* instance)
{
    if (instance->callbacks.freeMemory != NULL) {
        instance->callbacks.freeMemory(instance);
    } else {
        free(instance);
    }
}


static int real_unknown_index(fmi2ValueReference reference)
{
    switch (reference) {
        case fmi2_vr_unknown_0:
            return 0;
        case fmi2_vr_unknown_1:
            return 1;
        default:
            return -1;
    }
}


static int real_known_index(fmi2ValueReference reference)
{
    switch (reference) {
        case fmi2_vr_known_0:
            return 0;
        case fmi2_vr_known_1:
            return 1;
        case fmi2_vr_known_2:
            return 2;
        default:
            return -1;
    }
}


static fmi2Real partial_derivative(int unknown, int known)
{
    static const fmi2Real jacobian[2][3] = {
        {2.0, -3.0, 0.5},
        {-1.0, 4.0, 5.0}
    };
    return jacobian[unknown][known];
}


const char* fmi2GetTypesPlatform(void)
{
    return fmi2TypesPlatform;
}


const char* fmi2GetVersion(void)
{
    return fmi2Version;
}


fmi2Status fmi2SetDebugLogging(
    fmi2Component component,
    fmi2Boolean loggingOn,
    size_t numberOfCategories,
    const fmi2String categories[])
{
    (void)loggingOn;
    (void)numberOfCategories;
    (void)categories;
    return require_instance(component);
}


fmi2Component fmi2Instantiate(
    fmi2String instanceName,
    fmi2Type fmuType,
    fmi2String fmuGUID,
    fmi2String resourceLocation,
    const fmi2CallbackFunctions* callbacks,
    fmi2Boolean visible,
    fmi2Boolean loggingOn)
{
    (void)instanceName;
    (void)resourceLocation;
    (void)visible;
    (void)loggingOn;
    if (fmuType != fmi2CoSimulation ||
        fmuGUID == NULL ||
        strcmp(fmuGUID, FMI2_DERIVATIVE_FIXTURE_GUID) != 0) {
        return NULL;
    }

    derivative_instance* instance =
        (derivative_instance*)allocate_instance(callbacks, 1, sizeof(*instance));
    if (instance == NULL) {
        return NULL;
    }
    if (callbacks != NULL) {
        instance->callbacks = *callbacks;
    } else {
        memset(&instance->callbacks, 0, sizeof(instance->callbacks));
    }
    reset_instance(instance);
    return (fmi2Component)instance;
}


void fmi2FreeInstance(fmi2Component component)
{
    if (component != NULL) {
        free_instance(instance_of(component));
    }
}


fmi2Status fmi2SetupExperiment(
    fmi2Component component,
    fmi2Boolean toleranceDefined,
    fmi2Real tolerance,
    fmi2Real startTime,
    fmi2Boolean stopTimeDefined,
    fmi2Real stopTime)
{
    (void)toleranceDefined;
    (void)tolerance;
    (void)stopTimeDefined;
    (void)stopTime;
    if (require_instance(component) != fmi2OK ||
        instance_of(component)->state != state_instantiated) {
        return fmi2Error;
    }
    instance_of(component)->time = startTime;
    update_outputs(instance_of(component));
    return fmi2OK;
}


fmi2Status fmi2EnterInitializationMode(fmi2Component component)
{
    if (require_instance(component) != fmi2OK) {
        return fmi2Fatal;
    }
    if (instance_of(component)->state != state_instantiated) {
        return fmi2Error;
    }
    instance_of(component)->state = state_initialization;
    return fmi2OK;
}


fmi2Status fmi2ExitInitializationMode(fmi2Component component)
{
    if (require_instance(component) != fmi2OK) {
        return fmi2Fatal;
    }
    if (instance_of(component)->state != state_initialization) {
        return fmi2Error;
    }
    update_outputs(instance_of(component));
    instance_of(component)->state = state_step;
    return fmi2OK;
}


fmi2Status fmi2Terminate(fmi2Component component)
{
    if (require_instance(component) != fmi2OK) {
        return fmi2Fatal;
    }
    instance_of(component)->state = state_terminated;
    return fmi2OK;
}


fmi2Status fmi2Reset(fmi2Component component)
{
    if (require_instance(component) != fmi2OK) {
        return fmi2Fatal;
    }
    reset_instance(instance_of(component));
    return fmi2OK;
}


fmi2Status fmi2GetReal(
    fmi2Component component,
    const fmi2ValueReference references[],
    size_t count,
    fmi2Real values[])
{
    if (require_instance(component) != fmi2OK) {
        return fmi2Fatal;
    }
    if (count != 0 && (references == NULL || values == NULL)) {
        return fmi2Error;
    }
    derivative_instance* instance = instance_of(component);
    update_outputs(instance);
    for (size_t i = 0; i < count; ++i) {
        const int known = real_known_index(references[i]);
        const int unknown = real_unknown_index(references[i]);
        if (known >= 0) {
            values[i] = instance->known[known];
        } else if (unknown >= 0) {
            values[i] = instance->unknown[unknown];
        } else {
            return fmi2Error;
        }
    }
    return fmi2OK;
}


fmi2Status fmi2SetReal(
    fmi2Component component,
    const fmi2ValueReference references[],
    size_t count,
    const fmi2Real values[])
{
    if (require_instance(component) != fmi2OK) {
        return fmi2Fatal;
    }
    if (count != 0 && (references == NULL || values == NULL)) {
        return fmi2Error;
    }
    derivative_instance* instance = instance_of(component);
    for (size_t i = 0; i < count; ++i) {
        const int known = real_known_index(references[i]);
        if (known < 0) {
            return fmi2Error;
        }
        instance->known[known] = values[i];
    }
    update_outputs(instance);
    return fmi2OK;
}


fmi2Status fmi2GetInteger(
    fmi2Component component,
    const fmi2ValueReference references[],
    size_t count,
    fmi2Integer values[])
{
    if (require_instance(component) != fmi2OK) {
        return fmi2Fatal;
    }
    if (count != 0 && (references == NULL || values == NULL)) {
        return fmi2Error;
    }
    derivative_instance* instance = instance_of(component);
    for (size_t i = 0; i < count; ++i) {
        switch (references[i]) {
            case fmi2_vr_derivative_status:
                values[i] = instance->derivativeStatus;
                break;
            case fmi2_vr_directional_call_count:
                values[i] = instance->directionalCallCount;
                break;
            default:
                return fmi2Error;
        }
    }
    return fmi2OK;
}


fmi2Status fmi2SetInteger(
    fmi2Component component,
    const fmi2ValueReference references[],
    size_t count,
    const fmi2Integer values[])
{
    if (require_instance(component) != fmi2OK) {
        return fmi2Fatal;
    }
    if (count != 0 && (references == NULL || values == NULL)) {
        return fmi2Error;
    }
    derivative_instance* instance = instance_of(component);
    for (size_t i = 0; i < count; ++i) {
        if (references[i] != fmi2_vr_derivative_status ||
            values[i] < fmi2OK || values[i] > fmi2Pending) {
            return fmi2Error;
        }
        instance->derivativeStatus = values[i];
    }
    return fmi2OK;
}


fmi2Status fmi2GetBoolean(
    fmi2Component component,
    const fmi2ValueReference references[],
    size_t count,
    fmi2Boolean values[])
{
    if (require_instance(component) != fmi2OK) {
        return fmi2Fatal;
    }
    if (count != 0 && (references == NULL || values == NULL)) {
        return fmi2Error;
    }
    for (size_t i = 0; i < count; ++i) {
        if (references[i] != fmi2_vr_non_real) {
            return fmi2Error;
        }
        values[i] = instance_of(component)->nonReal;
    }
    return fmi2OK;
}


fmi2Status fmi2SetBoolean(
    fmi2Component component,
    const fmi2ValueReference references[],
    size_t count,
    const fmi2Boolean values[])
{
    if (require_instance(component) != fmi2OK) {
        return fmi2Fatal;
    }
    if (count != 0 && (references == NULL || values == NULL)) {
        return fmi2Error;
    }
    for (size_t i = 0; i < count; ++i) {
        if (references[i] != fmi2_vr_non_real) {
            return fmi2Error;
        }
        instance_of(component)->nonReal = values[i];
    }
    return fmi2OK;
}


fmi2Status fmi2GetString(
    fmi2Component component,
    const fmi2ValueReference references[],
    size_t count,
    fmi2String values[])
{
    (void)references;
    (void)count;
    (void)values;
    return require_instance(component) == fmi2OK ? fmi2Error : fmi2Fatal;
}


fmi2Status fmi2SetString(
    fmi2Component component,
    const fmi2ValueReference references[],
    size_t count,
    const fmi2String values[])
{
    (void)references;
    (void)count;
    (void)values;
    return require_instance(component) == fmi2OK ? fmi2Error : fmi2Fatal;
}


#define FMI2_UNSUPPORTED_STATE_FUNCTION(NAME, STATE_TYPE)                    \
    fmi2Status NAME(fmi2Component component, STATE_TYPE state)               \
    {                                                                         \
        (void)state;                                                           \
        return require_instance(component) == fmi2OK ? fmi2Error : fmi2Fatal; \
    }

FMI2_UNSUPPORTED_STATE_FUNCTION(fmi2GetFMUstate, fmi2FMUstate*)
FMI2_UNSUPPORTED_STATE_FUNCTION(fmi2SetFMUstate, fmi2FMUstate)
FMI2_UNSUPPORTED_STATE_FUNCTION(fmi2FreeFMUstate, fmi2FMUstate*)


fmi2Status fmi2SerializedFMUstateSize(
    fmi2Component component,
    fmi2FMUstate state,
    size_t* size)
{
    (void)state;
    (void)size;
    return require_instance(component) == fmi2OK ? fmi2Error : fmi2Fatal;
}


fmi2Status fmi2SerializeFMUstate(
    fmi2Component component,
    fmi2FMUstate state,
    fmi2Byte serializedState[],
    size_t size)
{
    (void)state;
    (void)serializedState;
    (void)size;
    return require_instance(component) == fmi2OK ? fmi2Error : fmi2Fatal;
}


fmi2Status fmi2DeSerializeFMUstate(
    fmi2Component component,
    const fmi2Byte serializedState[],
    size_t size,
    fmi2FMUstate* state)
{
    (void)serializedState;
    (void)size;
    (void)state;
    return require_instance(component) == fmi2OK ? fmi2Error : fmi2Fatal;
}


fmi2Status fmi2GetDirectionalDerivative(
    fmi2Component component,
    const fmi2ValueReference unknowns[],
    size_t numberOfUnknowns,
    const fmi2ValueReference knowns[],
    size_t numberOfKnowns,
    const fmi2Real seed[],
    fmi2Real sensitivity[])
{
    if (require_instance(component) != fmi2OK) {
        return fmi2Fatal;
    }
    derivative_instance* instance = instance_of(component);
    ++instance->directionalCallCount;

    if (numberOfUnknowns == 0 || numberOfKnowns == 0 ||
        unknowns == NULL || knowns == NULL ||
        seed == NULL || sensitivity == NULL) {
        return fmi2Error;
    }
    for (size_t i = 0; i < numberOfUnknowns; ++i) {
        if (real_unknown_index(unknowns[i]) < 0) {
            return fmi2Error;
        }
    }
    for (size_t i = 0; i < numberOfKnowns; ++i) {
        if (real_known_index(knowns[i]) < 0) {
            return fmi2Error;
        }
    }

    for (size_t i = 0; i < numberOfUnknowns; ++i) {
        const int unknown = real_unknown_index(unknowns[i]);
        sensitivity[i] = 0.0;
        for (size_t j = 0; j < numberOfKnowns; ++j) {
            const int known = real_known_index(knowns[j]);
            sensitivity[i] += partial_derivative(unknown, known) * seed[j];
        }
        if (instance->derivativeStatus >= fmi2Discard) {
            return (fmi2Status)instance->derivativeStatus;
        }
    }
    return (fmi2Status)instance->derivativeStatus;
}


fmi2Status fmi2DoStep(
    fmi2Component component,
    fmi2Real currentCommunicationPoint,
    fmi2Real communicationStepSize,
    fmi2Boolean noSetFMUStatePriorToCurrentPoint)
{
    (void)noSetFMUStatePriorToCurrentPoint;
    if (require_instance(component) != fmi2OK) {
        return fmi2Fatal;
    }
    derivative_instance* instance = instance_of(component);
    if (instance->state != state_step) {
        return fmi2Error;
    }
    instance->time = currentCommunicationPoint + communicationStepSize;
    update_outputs(instance);
    return fmi2OK;
}


fmi2Status fmi2CancelStep(fmi2Component component)
{
    return require_instance(component);
}


fmi2Status fmi2GetStatus(
    fmi2Component component,
    const fmi2StatusKind statusKind,
    fmi2Status* value)
{
    (void)statusKind;
    if (require_instance(component) != fmi2OK || value == NULL) {
        return fmi2Fatal;
    }
    *value = fmi2OK;
    return fmi2OK;
}


fmi2Status fmi2GetRealStatus(
    fmi2Component component,
    const fmi2StatusKind statusKind,
    fmi2Real* value)
{
    (void)statusKind;
    if (require_instance(component) != fmi2OK || value == NULL) {
        return fmi2Fatal;
    }
    *value = instance_of(component)->time;
    return fmi2OK;
}


fmi2Status fmi2GetIntegerStatus(
    fmi2Component component,
    const fmi2StatusKind statusKind,
    fmi2Integer* value)
{
    (void)statusKind;
    if (require_instance(component) != fmi2OK || value == NULL) {
        return fmi2Fatal;
    }
    *value = 0;
    return fmi2OK;
}


fmi2Status fmi2GetBooleanStatus(
    fmi2Component component,
    const fmi2StatusKind statusKind,
    fmi2Boolean* value)
{
    (void)statusKind;
    if (require_instance(component) != fmi2OK || value == NULL) {
        return fmi2Fatal;
    }
    *value = fmi2False;
    return fmi2OK;
}


fmi2Status fmi2GetStringStatus(
    fmi2Component component,
    const fmi2StatusKind statusKind,
    fmi2String* value)
{
    (void)statusKind;
    if (require_instance(component) != fmi2OK || value == NULL) {
        return fmi2Fatal;
    }
    *value = "";
    return fmi2OK;
}


#define FMI2_UNSUPPORTED(NAME, PARAMETERS)                                   \
    fmi2Status NAME PARAMETERS                                                \
    {                                                                         \
        return require_instance(component) == fmi2OK ? fmi2Error : fmi2Fatal; \
    }

FMI2_UNSUPPORTED(fmi2EnterEventMode, (fmi2Component component))
FMI2_UNSUPPORTED(
    fmi2EnterContinuousTimeMode,
    (fmi2Component component))


fmi2Status fmi2NewDiscreteStates(
    fmi2Component component,
    fmi2EventInfo* eventInfo)
{
    (void)eventInfo;
    return require_instance(component) == fmi2OK ? fmi2Error : fmi2Fatal;
}


fmi2Status fmi2CompletedIntegratorStep(
    fmi2Component component,
    fmi2Boolean noSetFMUStatePriorToCurrentPoint,
    fmi2Boolean* enterEventMode,
    fmi2Boolean* terminateSimulation)
{
    (void)noSetFMUStatePriorToCurrentPoint;
    (void)enterEventMode;
    (void)terminateSimulation;
    return require_instance(component) == fmi2OK ? fmi2Error : fmi2Fatal;
}


fmi2Status fmi2SetTime(fmi2Component component, fmi2Real time)
{
    (void)time;
    return require_instance(component) == fmi2OK ? fmi2Error : fmi2Fatal;
}


fmi2Status fmi2SetContinuousStates(
    fmi2Component component,
    const fmi2Real states[],
    size_t count)
{
    (void)states;
    (void)count;
    return require_instance(component) == fmi2OK ? fmi2Error : fmi2Fatal;
}


fmi2Status fmi2GetDerivatives(
    fmi2Component component,
    fmi2Real values[],
    size_t count)
{
    (void)values;
    (void)count;
    return require_instance(component) == fmi2OK ? fmi2Error : fmi2Fatal;
}


fmi2Status fmi2GetEventIndicators(
    fmi2Component component,
    fmi2Real values[],
    size_t count)
{
    (void)values;
    (void)count;
    return require_instance(component) == fmi2OK ? fmi2Error : fmi2Fatal;
}


fmi2Status fmi2GetContinuousStates(
    fmi2Component component,
    fmi2Real values[],
    size_t count)
{
    (void)values;
    (void)count;
    return require_instance(component) == fmi2OK ? fmi2Error : fmi2Fatal;
}


fmi2Status fmi2GetNominalsOfContinuousStates(
    fmi2Component component,
    fmi2Real values[],
    size_t count)
{
    (void)values;
    (void)count;
    return require_instance(component) == fmi2OK ? fmi2Error : fmi2Fatal;
}


fmi2Status fmi2SetRealInputDerivatives(
    fmi2Component component,
    const fmi2ValueReference references[],
    size_t count,
    const fmi2Integer orders[],
    const fmi2Real values[])
{
    (void)references;
    (void)count;
    (void)orders;
    (void)values;
    return require_instance(component) == fmi2OK ? fmi2Error : fmi2Fatal;
}


fmi2Status fmi2GetRealOutputDerivatives(
    fmi2Component component,
    const fmi2ValueReference references[],
    size_t count,
    const fmi2Integer orders[],
    fmi2Real values[])
{
    (void)references;
    (void)count;
    (void)orders;
    (void)values;
    return require_instance(component) == fmi2OK ? fmi2Error : fmi2Fatal;
}
