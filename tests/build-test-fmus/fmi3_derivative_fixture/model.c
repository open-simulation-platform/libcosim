#include "fmi3Functions.h"

#include "derivative_fixture.h"
#include "fixture_instances.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>


typedef enum
{
    mode_instantiated,
    mode_configuration,
    mode_initialization,
    mode_event,
    mode_step,
    mode_terminated
} fixture_mode;


typedef struct
{
    fmi3InstanceEnvironment environment;
    fmi3LogMessageCallback logger;
    fmi3Boolean testLogging;
    fmi3Boolean logA;
    fmi3Boolean logB;
    fmi3IntermediateUpdateCallback intermediateUpdate;
    fmi3Boolean eventModeUsed;
    fixture_mode mode;
    fixture_mode modeBeforeConfiguration;
    fmi3Float64 time;
    fmi3Float64 knownFloat64;
    fmi3Float32 knownFloat32;
    fmi3Float64 knownFixed[3];
    fmi3UInt64 structuralSize;
    fmi3Float64 knownStructural[3];
    fmi3Float64 outputMatrix[6];
    fmi3Float64 outputConstant;
    fmi3Float32 outputFloat32;
    fmi3Float64 outputStructural[3];
    fmi3Float64 state;
    fmi3Float64 stateDerivative;
    fmi3Int32 derivativeStatus;
    fmi3UInt64 derivativeCallCount;
    fmi3Boolean nonFloat;
} derivative_instance;


static const fmi3Float64 matrixJacobian[6][3] = {
    {1.0, 2.0, 3.0},
    {4.0, 5.0, 6.0},
    {-1.0, -2.0, -3.0},
    {0.5, 1.5, 2.5},
    {7.0, 0.0, -2.0},
    {3.0, -4.0, 5.0}
};


static derivative_instance* instance_of(fmi3Instance instance)
{
    return (derivative_instance*)instance;
}


static fmi3Status require_instance(fmi3Instance instance)
{
    return instance == NULL ? fmi3Fatal : fmi3OK;
}


static void update_outputs(derivative_instance* instance)
{
    for (size_t row = 0; row < 6; ++row) {
        instance->outputMatrix[row] = 0.0;
        for (size_t column = 0; column < 3; ++column) {
            instance->outputMatrix[row] +=
                matrixJacobian[row][column] * instance->knownFixed[column];
        }
    }
    instance->outputConstant = 42.0;
    instance->outputFloat32 =
        (fmi3Float32)(-4.0 * instance->knownFloat64 +
                      0.5 * instance->knownFloat32);
    for (size_t i = 0; i < instance->structuralSize; ++i) {
        instance->outputStructural[i] =
            instance->knownFloat64 +
            (fmi3Float64)(i + 1) * instance->knownStructural[i];
    }
    instance->stateDerivative =
        6.0 * instance->state + 7.0 * instance->knownFloat64;
}


static void reset_instance(derivative_instance* instance)
{
    instance->mode = mode_instantiated;
    instance->modeBeforeConfiguration = mode_instantiated;
    instance->time = 0.0;
    instance->knownFloat64 = 0.0;
    instance->knownFloat32 = 0.0f;
    memset(instance->knownFixed, 0, sizeof(instance->knownFixed));
    instance->structuralSize = 2;
    memset(instance->knownStructural, 0, sizeof(instance->knownStructural));
    memset(instance->outputMatrix, 0, sizeof(instance->outputMatrix));
    memset(instance->outputStructural, 0, sizeof(instance->outputStructural));
    instance->state = 1.0;
    instance->derivativeStatus = fmi3OK;
    instance->derivativeCallCount = 0;
    instance->nonFloat = fmi3False;
    update_outputs(instance);
}


static size_t float64_size(
    const derivative_instance* instance,
    fmi3ValueReference reference)
{
    switch (reference) {
        case fmi3_vr_known_float64:
        case fmi3_vr_output_constant:
        case fmi3_vr_state:
        case fmi3_vr_state_derivative:
            return 1;
        case fmi3_vr_known_fixed:
            return 3;
        case fmi3_vr_known_structural:
        case fmi3_vr_output_structural:
            return (size_t)instance->structuralSize;
        case fmi3_vr_output_matrix:
            return 6;
        default:
            return SIZE_MAX;
    }
}


static size_t derivative_unknown_size(
    const derivative_instance* instance,
    fmi3ValueReference reference)
{
    switch (reference) {
        case fmi3_vr_output_matrix:
            return 6;
        case fmi3_vr_output_constant:
        case fmi3_vr_output_float32:
        case fmi3_vr_state_derivative:
            return 1;
        case fmi3_vr_output_structural:
            return (size_t)instance->structuralSize;
        default:
            return SIZE_MAX;
    }
}


static size_t derivative_known_size(
    const derivative_instance* instance,
    fmi3ValueReference reference)
{
    switch (reference) {
        case fmi3_vr_known_float64:
        case fmi3_vr_known_float32:
        case fmi3_vr_state:
            return 1;
        case fmi3_vr_known_fixed:
            return 3;
        case fmi3_vr_known_structural:
            return (size_t)instance->structuralSize;
        default:
            return SIZE_MAX;
    }
}


static fmi3Float64 partial_derivative(
    fmi3ValueReference unknown,
    size_t unknownElement,
    fmi3ValueReference known,
    size_t knownElement)
{
    if (unknown == fmi3_vr_output_matrix &&
        known == fmi3_vr_known_fixed) {
        return matrixJacobian[unknownElement][knownElement];
    }
    if (unknown == fmi3_vr_output_float32) {
        if (known == fmi3_vr_known_float64) {
            return -4.0;
        }
        if (known == fmi3_vr_known_float32) {
            return 0.5;
        }
    }
    if (unknown == fmi3_vr_output_structural) {
        if (known == fmi3_vr_known_float64) {
            return 1.0;
        }
        if (known == fmi3_vr_known_structural &&
            unknownElement == knownElement) {
            return (fmi3Float64)(unknownElement + 1);
        }
    }
    if (unknown == fmi3_vr_state_derivative) {
        if (known == fmi3_vr_state) {
            return 6.0;
        }
        if (known == fmi3_vr_known_float64) {
            return 7.0;
        }
    }
    return 0.0;
}


static fmi3Status validate_derivative_arguments(
    derivative_instance* instance,
    const fmi3ValueReference unknowns[],
    size_t nUnknowns,
    const fmi3ValueReference knowns[],
    size_t nKnowns,
    const fmi3Float64 seed[],
    size_t nSeed,
    fmi3Float64 sensitivity[],
    size_t nSensitivity,
    size_t expectedSeed,
    size_t expectedSensitivity)
{
    (void)instance;
    if (nUnknowns == 0 || nKnowns == 0 ||
        unknowns == NULL || knowns == NULL ||
        (nSeed != 0 && seed == NULL) ||
        (nSensitivity != 0 && sensitivity == NULL) ||
        nSeed != expectedSeed || nSensitivity != expectedSensitivity) {
        return fmi3Error;
    }
    return fmi3OK;
}


const char* fmi3GetVersion(void)
{
    return fmi3Version;
}


fmi3Status fmi3SetDebugLogging(
    fmi3Instance instance,
    fmi3Boolean loggingOn,
    size_t nCategories,
    const fmi3String categories[])
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    if ((nCategories == 0) != (categories == NULL)) {
        return fmi3Error;
    }
    derivative_instance* fixture = instance_of(instance);
    fixture->testLogging = fmi3True;
    if (nCategories == 0) {
        fixture->logA = loggingOn;
        fixture->logB = loggingOn;
    }
    for (size_t i = 0; i < nCategories; ++i) {
        if (strcmp(categories[i], "logFixtureA") == 0) {
            fixture->logA = loggingOn;
        } else if (strcmp(categories[i], "logFixtureB") == 0) {
            fixture->logB = loggingOn;
        } else {
            return fmi3Error;
        }
    }
    return fmi3OK;
}


fmi3Instance fmi3InstantiateModelExchange(
    fmi3String instanceName,
    fmi3String instantiationToken,
    fmi3String resourcePath,
    fmi3Boolean visible,
    fmi3Boolean loggingOn,
    fmi3InstanceEnvironment instanceEnvironment,
    fmi3LogMessageCallback logMessage)
{
    (void)instanceName;
    (void)instantiationToken;
    (void)resourcePath;
    (void)visible;
    (void)loggingOn;
    (void)instanceEnvironment;
    (void)logMessage;
    return NULL;
}


fmi3Instance fmi3InstantiateCoSimulation(
    fmi3String instanceName,
    fmi3String instantiationToken,
    fmi3String resourcePath,
    fmi3Boolean visible,
    fmi3Boolean loggingOn,
    fmi3Boolean eventModeUsed,
    fmi3Boolean earlyReturnAllowed,
    const fmi3ValueReference requiredIntermediateVariables[],
    size_t nRequiredIntermediateVariables,
    fmi3InstanceEnvironment instanceEnvironment,
    fmi3LogMessageCallback logMessage,
    fmi3IntermediateUpdateCallback intermediateUpdate)
{
    (void)instanceName;
    (void)resourcePath;
    (void)visible;
    (void)earlyReturnAllowed;
    (void)requiredIntermediateVariables;
    if (instantiationToken == NULL ||
        strcmp(
            instantiationToken,
            FMI3_DERIVATIVE_FIXTURE_TOKEN) != 0 ||
        nRequiredIntermediateVariables != 0) {
        return NULL;
    }

    derivative_instance* instance =
        (derivative_instance*)calloc(1, sizeof(*instance));
    if (instance == NULL) {
        return NULL;
    }
    instance->environment = instanceEnvironment;
    instance->logger = logMessage;
    instance->logA = loggingOn;
    instance->logB = loggingOn;
    instance->intermediateUpdate = intermediateUpdate;
    instance->eventModeUsed = eventModeUsed;
    reset_instance(instance);
    if (!fixture_track_instance(instance, free)) {
        free(instance);
        return NULL;
    }
    return (fmi3Instance)instance;
}


fmi3Instance fmi3InstantiateScheduledExecution(
    fmi3String instanceName,
    fmi3String instantiationToken,
    fmi3String resourcePath,
    fmi3Boolean visible,
    fmi3Boolean loggingOn,
    fmi3InstanceEnvironment instanceEnvironment,
    fmi3LogMessageCallback logMessage,
    fmi3ClockUpdateCallback clockUpdate,
    fmi3LockPreemptionCallback lockPreemption,
    fmi3UnlockPreemptionCallback unlockPreemption)
{
    (void)instanceName;
    (void)instantiationToken;
    (void)resourcePath;
    (void)visible;
    (void)loggingOn;
    (void)instanceEnvironment;
    (void)logMessage;
    (void)clockUpdate;
    (void)lockPreemption;
    (void)unlockPreemption;
    return NULL;
}


void fmi3FreeInstance(fmi3Instance instance)
{
    fixture_free_instance(instance);
}


fmi3Status fmi3EnterConfigurationMode(fmi3Instance instance)
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    derivative_instance* fixture = instance_of(instance);
    if (fixture->mode != mode_instantiated &&
        fixture->mode != mode_event &&
        fixture->mode != mode_step) {
        return fmi3Error;
    }
    fixture->modeBeforeConfiguration = fixture->mode;
    fixture->mode = mode_configuration;
    return fmi3OK;
}


fmi3Status fmi3ExitConfigurationMode(fmi3Instance instance)
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    derivative_instance* fixture = instance_of(instance);
    if (fixture->mode != mode_configuration) {
        return fmi3Error;
    }
    fixture->mode = fixture->modeBeforeConfiguration;
    update_outputs(fixture);
    return fmi3OK;
}


fmi3Status fmi3EnterInitializationMode(
    fmi3Instance instance,
    fmi3Boolean toleranceDefined,
    fmi3Float64 tolerance,
    fmi3Float64 startTime,
    fmi3Boolean stopTimeDefined,
    fmi3Float64 stopTime)
{
    (void)toleranceDefined;
    (void)tolerance;
    (void)stopTimeDefined;
    (void)stopTime;
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    derivative_instance* fixture = instance_of(instance);
    if (fixture->mode != mode_instantiated) {
        return fmi3Error;
    }
    fixture->time = startTime;
    fixture->mode = mode_initialization;
    update_outputs(fixture);
    return fmi3OK;
}


fmi3Status fmi3ExitInitializationMode(fmi3Instance instance)
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    derivative_instance* fixture = instance_of(instance);
    if (fixture->mode != mode_initialization) {
        return fmi3Error;
    }
    fixture->mode = fixture->eventModeUsed ? mode_event : mode_step;
    update_outputs(fixture);
    return fmi3OK;
}


fmi3Status fmi3EnterEventMode(fmi3Instance instance)
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    derivative_instance* fixture = instance_of(instance);
    if (!fixture->eventModeUsed || fixture->mode != mode_step) {
        return fmi3Error;
    }
    fixture->mode = mode_event;
    return fmi3OK;
}


fmi3Status fmi3EnterStepMode(fmi3Instance instance)
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    derivative_instance* fixture = instance_of(instance);
    if (fixture->mode != mode_event) {
        return fmi3Error;
    }
    fixture->mode = mode_step;
    return fmi3OK;
}


fmi3Status fmi3Terminate(fmi3Instance instance)
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    derivative_instance* fixture = instance_of(instance);
    if (fixture->mode != mode_initialization &&
        fixture->mode != mode_event &&
        fixture->mode != mode_step) {
        return fmi3Error;
    }
    fixture->mode = mode_terminated;
    return fmi3OK;
}


fmi3Status fmi3Reset(fmi3Instance instance)
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    reset_instance(instance_of(instance));
    return fmi3OK;
}


fmi3Status fmi3GetFloat64(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    fmi3Float64 values[],
    size_t nValues)
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    if ((nReferences != 0 && references == NULL) ||
        (nValues != 0 && values == NULL)) {
        return fmi3Error;
    }
    derivative_instance* fixture = instance_of(instance);
    size_t expectedValues = 0;
    for (size_t i = 0; i < nReferences; ++i) {
        const size_t size = float64_size(fixture, references[i]);
        if (size == SIZE_MAX || expectedValues > SIZE_MAX - size) {
            return fmi3Error;
        }
        expectedValues += size;
    }
    if (expectedValues != nValues) {
        return fmi3Error;
    }

    update_outputs(fixture);
    size_t offset = 0;
    for (size_t i = 0; i < nReferences; ++i) {
        switch (references[i]) {
            case fmi3_vr_known_float64:
                values[offset++] = fixture->knownFloat64;
                break;
            case fmi3_vr_known_fixed:
                memcpy(
                    &values[offset],
                    fixture->knownFixed,
                    sizeof(fixture->knownFixed));
                offset += 3;
                break;
            case fmi3_vr_known_structural:
                for (size_t j = 0; j < fixture->structuralSize; ++j) {
                    values[offset++] = fixture->knownStructural[j];
                }
                break;
            case fmi3_vr_output_matrix:
                memcpy(
                    &values[offset],
                    fixture->outputMatrix,
                    sizeof(fixture->outputMatrix));
                offset += 6;
                break;
            case fmi3_vr_output_constant:
                values[offset++] = fixture->outputConstant;
                break;
            case fmi3_vr_output_structural:
                for (size_t j = 0; j < fixture->structuralSize; ++j) {
                    values[offset++] = fixture->outputStructural[j];
                }
                break;
            case fmi3_vr_state:
                values[offset++] = fixture->state;
                break;
            case fmi3_vr_state_derivative:
                values[offset++] = fixture->stateDerivative;
                break;
            default:
                return fmi3Error;
        }
    }
    return fmi3OK;
}


fmi3Status fmi3SetFloat64(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    const fmi3Float64 values[],
    size_t nValues)
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    if ((nReferences != 0 && references == NULL) ||
        (nValues != 0 && values == NULL)) {
        return fmi3Error;
    }
    derivative_instance* fixture = instance_of(instance);
    size_t expectedValues = 0;
    for (size_t i = 0; i < nReferences; ++i) {
        size_t size;
        switch (references[i]) {
            case fmi3_vr_known_float64:
                size = 1;
                break;
            case fmi3_vr_known_fixed:
                size = 3;
                break;
            case fmi3_vr_known_structural:
                size = (size_t)fixture->structuralSize;
                break;
            default:
                return fmi3Error;
        }
        if (expectedValues > SIZE_MAX - size) {
            return fmi3Error;
        }
        expectedValues += size;
    }
    if (expectedValues != nValues) {
        return fmi3Error;
    }

    size_t offset = 0;
    for (size_t i = 0; i < nReferences; ++i) {
        switch (references[i]) {
            case fmi3_vr_known_float64:
                fixture->knownFloat64 = values[offset++];
                break;
            case fmi3_vr_known_fixed:
                memcpy(
                    fixture->knownFixed,
                    &values[offset],
                    sizeof(fixture->knownFixed));
                offset += 3;
                break;
            case fmi3_vr_known_structural:
                for (size_t j = 0; j < fixture->structuralSize; ++j) {
                    fixture->knownStructural[j] = values[offset++];
                }
                break;
            default:
                return fmi3Error;
        }
    }
    update_outputs(fixture);
    return fmi3OK;
}


fmi3Status fmi3GetFloat32(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    fmi3Float32 values[],
    size_t nValues)
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    if (nReferences != nValues ||
        (nReferences != 0 && (references == NULL || values == NULL))) {
        return fmi3Error;
    }
    derivative_instance* fixture = instance_of(instance);
    update_outputs(fixture);
    for (size_t i = 0; i < nReferences; ++i) {
        switch (references[i]) {
            case fmi3_vr_known_float32:
                values[i] = fixture->knownFloat32;
                break;
            case fmi3_vr_output_float32:
                values[i] = fixture->outputFloat32;
                break;
            default:
                return fmi3Error;
        }
    }
    return fmi3OK;
}


fmi3Status fmi3SetFloat32(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    const fmi3Float32 values[],
    size_t nValues)
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    if (nReferences != nValues ||
        (nReferences != 0 && (references == NULL || values == NULL))) {
        return fmi3Error;
    }
    derivative_instance* fixture = instance_of(instance);
    for (size_t i = 0; i < nReferences; ++i) {
        if (references[i] != fmi3_vr_known_float32) {
            return fmi3Error;
        }
        fixture->knownFloat32 = values[i];
    }
    update_outputs(fixture);
    return fmi3OK;
}


fmi3Status fmi3GetInt32(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    fmi3Int32 values[],
    size_t nValues)
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    if (nReferences != nValues ||
        (nReferences != 0 && (references == NULL || values == NULL))) {
        return fmi3Error;
    }
    for (size_t i = 0; i < nReferences; ++i) {
        if (references[i] != fmi3_vr_derivative_status) {
            return fmi3Error;
        }
        values[i] = instance_of(instance)->derivativeStatus;
    }
    return fmi3OK;
}


fmi3Status fmi3SetInt32(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    const fmi3Int32 values[],
    size_t nValues)
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    if (nReferences != nValues ||
        (nReferences != 0 && (references == NULL || values == NULL))) {
        return fmi3Error;
    }
    for (size_t i = 0; i < nReferences; ++i) {
        if (references[i] != fmi3_vr_derivative_status ||
            values[i] < fmi3OK || values[i] > fmi3Fatal) {
            return fmi3Error;
        }
        instance_of(instance)->derivativeStatus = values[i];
    }
    return fmi3OK;
}


fmi3Status fmi3GetUInt64(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    fmi3UInt64 values[],
    size_t nValues)
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    if (nReferences != nValues ||
        (nReferences != 0 && (references == NULL || values == NULL))) {
        return fmi3Error;
    }
    derivative_instance* fixture = instance_of(instance);
    for (size_t i = 0; i < nReferences; ++i) {
        switch (references[i]) {
            case fmi3_vr_structural_size:
                values[i] = fixture->structuralSize;
                break;
            case fmi3_vr_derivative_call_count:
                values[i] = fixture->derivativeCallCount;
                break;
            default:
                return fmi3Error;
        }
    }
    return fmi3OK;
}


fmi3Status fmi3SetUInt64(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    const fmi3UInt64 values[],
    size_t nValues)
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    if (nReferences != nValues ||
        (nReferences != 0 && (references == NULL || values == NULL))) {
        return fmi3Error;
    }
    derivative_instance* fixture = instance_of(instance);
    if (fixture->mode != mode_configuration) {
        return fmi3Error;
    }
    for (size_t i = 0; i < nReferences; ++i) {
        if (references[i] != fmi3_vr_structural_size || values[i] > 3) {
            return fmi3Error;
        }
        fixture->structuralSize = values[i];
    }
    update_outputs(fixture);
    return fmi3OK;
}


fmi3Status fmi3GetBoolean(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    fmi3Boolean values[],
    size_t nValues)
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    if (nReferences != nValues ||
        (nReferences != 0 && (references == NULL || values == NULL))) {
        return fmi3Error;
    }
    for (size_t i = 0; i < nReferences; ++i) {
        if (references[i] != fmi3_vr_non_float) {
            return fmi3Error;
        }
        values[i] = instance_of(instance)->nonFloat;
    }
    return fmi3OK;
}


fmi3Status fmi3SetBoolean(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    const fmi3Boolean values[],
    size_t nValues)
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    if (nReferences != nValues ||
        (nReferences != 0 && (references == NULL || values == NULL))) {
        return fmi3Error;
    }
    for (size_t i = 0; i < nReferences; ++i) {
        if (references[i] != fmi3_vr_non_float) {
            return fmi3Error;
        }
        instance_of(instance)->nonFloat = values[i];
    }
    return fmi3OK;
}


fmi3Status fmi3GetDirectionalDerivative(
    fmi3Instance instance,
    const fmi3ValueReference unknowns[],
    size_t nUnknowns,
    const fmi3ValueReference knowns[],
    size_t nKnowns,
    const fmi3Float64 seed[],
    size_t nSeed,
    fmi3Float64 sensitivity[],
    size_t nSensitivity)
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    derivative_instance* fixture = instance_of(instance);
    ++fixture->derivativeCallCount;
    if (fixture->mode != mode_initialization &&
        fixture->mode != mode_event &&
        fixture->mode != mode_step &&
        fixture->mode != mode_terminated) {
        return fmi3Error;
    }

    size_t expectedSeed = 0;
    for (size_t i = 0; i < nKnowns; ++i) {
        if (knowns == NULL) {
            return fmi3Error;
        }
        const size_t size = derivative_known_size(fixture, knowns[i]);
        if (size == SIZE_MAX || expectedSeed > SIZE_MAX - size) {
            return fmi3Error;
        }
        expectedSeed += size;
    }
    size_t expectedSensitivity = 0;
    for (size_t i = 0; i < nUnknowns; ++i) {
        if (unknowns == NULL) {
            return fmi3Error;
        }
        const size_t size =
            derivative_unknown_size(fixture, unknowns[i]);
        if (size == SIZE_MAX ||
            expectedSensitivity > SIZE_MAX - size) {
            return fmi3Error;
        }
        expectedSensitivity += size;
    }
    if (validate_derivative_arguments(
            fixture,
            unknowns,
            nUnknowns,
            knowns,
            nKnowns,
            seed,
            nSeed,
            sensitivity,
            nSensitivity,
            expectedSeed,
            expectedSensitivity) != fmi3OK) {
        return fmi3Error;
    }

    size_t sensitivityOffset = 0;
    for (size_t u = 0; u < nUnknowns; ++u) {
        const size_t unknownSize =
            derivative_unknown_size(fixture, unknowns[u]);
        for (size_t ue = 0; ue < unknownSize; ++ue) {
            fmi3Float64 value = 0.0;
            size_t seedOffset = 0;
            for (size_t k = 0; k < nKnowns; ++k) {
                const size_t knownSize =
                    derivative_known_size(fixture, knowns[k]);
                for (size_t ke = 0; ke < knownSize; ++ke) {
                    value += partial_derivative(
                                 unknowns[u],
                                 ue,
                                 knowns[k],
                                 ke) *
                             seed[seedOffset++];
                }
            }
            sensitivity[sensitivityOffset++] = value;
            if (fixture->derivativeStatus >= fmi3Discard) {
                return (fmi3Status)fixture->derivativeStatus;
            }
        }
    }
    return (fmi3Status)fixture->derivativeStatus;
}


fmi3Status fmi3GetAdjointDerivative(
    fmi3Instance instance,
    const fmi3ValueReference unknowns[],
    size_t nUnknowns,
    const fmi3ValueReference knowns[],
    size_t nKnowns,
    const fmi3Float64 seed[],
    size_t nSeed,
    fmi3Float64 sensitivity[],
    size_t nSensitivity)
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    derivative_instance* fixture = instance_of(instance);
    ++fixture->derivativeCallCount;
    if (fixture->mode != mode_initialization &&
        fixture->mode != mode_event &&
        fixture->mode != mode_step &&
        fixture->mode != mode_terminated) {
        return fmi3Error;
    }

    size_t expectedSeed = 0;
    for (size_t i = 0; i < nUnknowns; ++i) {
        if (unknowns == NULL) {
            return fmi3Error;
        }
        const size_t size =
            derivative_unknown_size(fixture, unknowns[i]);
        if (size == SIZE_MAX || expectedSeed > SIZE_MAX - size) {
            return fmi3Error;
        }
        expectedSeed += size;
    }
    size_t expectedSensitivity = 0;
    for (size_t i = 0; i < nKnowns; ++i) {
        if (knowns == NULL) {
            return fmi3Error;
        }
        const size_t size = derivative_known_size(fixture, knowns[i]);
        if (size == SIZE_MAX ||
            expectedSensitivity > SIZE_MAX - size) {
            return fmi3Error;
        }
        expectedSensitivity += size;
    }
    if (validate_derivative_arguments(
            fixture,
            unknowns,
            nUnknowns,
            knowns,
            nKnowns,
            seed,
            nSeed,
            sensitivity,
            nSensitivity,
            expectedSeed,
            expectedSensitivity) != fmi3OK) {
        return fmi3Error;
    }

    size_t sensitivityOffset = 0;
    for (size_t k = 0; k < nKnowns; ++k) {
        const size_t knownSize =
            derivative_known_size(fixture, knowns[k]);
        for (size_t ke = 0; ke < knownSize; ++ke) {
            fmi3Float64 value = 0.0;
            size_t seedOffset = 0;
            for (size_t u = 0; u < nUnknowns; ++u) {
                const size_t unknownSize =
                    derivative_unknown_size(fixture, unknowns[u]);
                for (size_t ue = 0; ue < unknownSize; ++ue) {
                    value += partial_derivative(
                                 unknowns[u],
                                 ue,
                                 knowns[k],
                                 ke) *
                             seed[seedOffset++];
                }
            }
            sensitivity[sensitivityOffset++] = value;
            if (fixture->derivativeStatus >= fmi3Discard) {
                return (fmi3Status)fixture->derivativeStatus;
            }
        }
    }
    return (fmi3Status)fixture->derivativeStatus;
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
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    if (instance_of(instance)->mode != mode_event ||
        discreteStatesNeedUpdate == NULL ||
        terminateSimulation == NULL ||
        nominalsOfContinuousStatesChanged == NULL ||
        valuesOfContinuousStatesChanged == NULL ||
        nextEventTimeDefined == NULL ||
        nextEventTime == NULL) {
        return fmi3Error;
    }
    *discreteStatesNeedUpdate = fmi3False;
    *terminateSimulation = fmi3False;
    *nominalsOfContinuousStatesChanged = fmi3False;
    *valuesOfContinuousStatesChanged = fmi3False;
    *nextEventTimeDefined = fmi3False;
    *nextEventTime = 0.0;
    return fmi3OK;
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
    (void)noSetFMUStatePriorToCurrentPoint;
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    derivative_instance* fixture = instance_of(instance);
    if (fixture->mode != mode_step ||
        eventHandlingNeeded == NULL ||
        terminateSimulation == NULL ||
        earlyReturn == NULL ||
        lastSuccessfulTime == NULL) {
        return fmi3Error;
    }
    update_outputs(fixture);
    fixture->state += communicationStepSize * fixture->stateDerivative;
    fixture->time = currentCommunicationPoint + communicationStepSize;
    update_outputs(fixture);
    *eventHandlingNeeded = fmi3False;
    *terminateSimulation = fmi3False;
    *earlyReturn = fmi3False;
    *lastSuccessfulTime = fixture->time;
    if (fixture->testLogging && fixture->logger) {
        if (fixture->logA) {
            fixture->logger(fixture->environment, fmi3OK, "logFixtureA", "Fixture step (A)");
        }
        if (fixture->logB) {
            fixture->logger(fixture->environment, fmi3OK, "logFixtureB", "Fixture step (B)");
        }
    }
    return fmi3OK;
}


static fmi3Status unsupported(fmi3Instance instance)
{
    return require_instance(instance) == fmi3OK ? fmi3Error : fmi3Fatal;
}


#define FMI3_GET_STUB(NAME, TYPE)                         \
    fmi3Status NAME(                                      \
        fmi3Instance instance,                            \
        const fmi3ValueReference references[],            \
        size_t nReferences,                               \
        TYPE values[],                                    \
        size_t nValues)                                   \
    {                                                     \
        (void)references;                                 \
        (void)nReferences;                                \
        (void)values;                                     \
        (void)nValues;                                    \
        return unsupported(instance);                     \
    }

#define FMI3_SET_STUB(NAME, TYPE)                         \
    fmi3Status NAME(                                      \
        fmi3Instance instance,                            \
        const fmi3ValueReference references[],            \
        size_t nReferences,                               \
        const TYPE values[],                              \
        size_t nValues)                                   \
    {                                                     \
        (void)references;                                 \
        (void)nReferences;                                \
        (void)values;                                     \
        (void)nValues;                                    \
        return unsupported(instance);                     \
    }

FMI3_GET_STUB(fmi3GetInt8, fmi3Int8)
FMI3_GET_STUB(fmi3GetUInt8, fmi3UInt8)
FMI3_GET_STUB(fmi3GetInt16, fmi3Int16)
FMI3_GET_STUB(fmi3GetUInt16, fmi3UInt16)
FMI3_GET_STUB(fmi3GetUInt32, fmi3UInt32)
FMI3_GET_STUB(fmi3GetInt64, fmi3Int64)
FMI3_GET_STUB(fmi3GetString, fmi3String)

FMI3_SET_STUB(fmi3SetInt8, fmi3Int8)
FMI3_SET_STUB(fmi3SetUInt8, fmi3UInt8)
FMI3_SET_STUB(fmi3SetInt16, fmi3Int16)
FMI3_SET_STUB(fmi3SetUInt16, fmi3UInt16)
FMI3_SET_STUB(fmi3SetUInt32, fmi3UInt32)
FMI3_SET_STUB(fmi3SetInt64, fmi3Int64)
FMI3_SET_STUB(fmi3SetString, fmi3String)


fmi3Status fmi3GetBinary(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    size_t valueSizes[],
    fmi3Binary values[],
    size_t nValues)
{
    (void)references;
    (void)nReferences;
    (void)valueSizes;
    (void)values;
    (void)nValues;
    return unsupported(instance);
}


fmi3Status fmi3SetBinary(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    const size_t valueSizes[],
    const fmi3Binary values[],
    size_t nValues)
{
    (void)references;
    (void)nReferences;
    (void)valueSizes;
    (void)values;
    (void)nValues;
    return unsupported(instance);
}


fmi3Status fmi3GetClock(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    fmi3Clock values[])
{
    (void)references;
    (void)nReferences;
    (void)values;
    return unsupported(instance);
}


fmi3Status fmi3SetClock(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    const fmi3Clock values[])
{
    (void)references;
    (void)nReferences;
    (void)values;
    return unsupported(instance);
}


fmi3Status fmi3GetNumberOfVariableDependencies(
    fmi3Instance instance,
    fmi3ValueReference valueReference,
    size_t* nDependencies)
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    if (nDependencies == NULL) {
        return fmi3Error;
    }
    switch (valueReference) {
        case 10:
            *nDependencies = 4;
            return fmi3OK;
        case 11:
        case 101:
            *nDependencies = 0;
            return fmi3OK;
        case 12:
            *nDependencies = 1;
            return fmi3OK;
        case 13:
            *nDependencies = (size_t)instance_of(instance)->structuralSize;
            return fmi3OK;
        default:
            return fmi3Error;
    }
}


fmi3Status fmi3GetVariableDependencies(
    fmi3Instance instance,
    fmi3ValueReference dependent,
    size_t elementIndicesOfDependent[],
    fmi3ValueReference independents[],
    size_t elementIndicesOfIndependents[],
    fmi3DependencyKind dependencyKinds[],
    size_t nDependencies)
{
    if (require_instance(instance) != fmi3OK) {
        return fmi3Fatal;
    }
    if (nDependencies != 0 &&
        (elementIndicesOfDependent == NULL || independents == NULL ||
         elementIndicesOfIndependents == NULL || dependencyKinds == NULL)) {
        return fmi3Error;
    }
    switch (dependent) {
        case 10:
            if (nDependencies != 4) {
                return fmi3Error;
            }
            elementIndicesOfDependent[0] = 1;
            independents[0] = 3;
            elementIndicesOfIndependents[0] = 1;
            dependencyKinds[0] = fmi3Dependent;
            elementIndicesOfDependent[1] = 2;
            independents[1] = 3;
            elementIndicesOfIndependents[1] = 2;
            dependencyKinds[1] = fmi3Discrete;
            elementIndicesOfDependent[2] = 0;
            independents[2] = 1;
            elementIndicesOfIndependents[2] = 0;
            dependencyKinds[2] = fmi3Constant;
            elementIndicesOfDependent[3] = 4;
            independents[3] = 2;
            elementIndicesOfIndependents[3] = 0;
            dependencyKinds[3] = fmi3Independent;
            return fmi3OK;
        case 12:
            if (nDependencies != 1) {
                return fmi3Error;
            }
            elementIndicesOfDependent[0] = 0;
            independents[0] = 2;
            elementIndicesOfIndependents[0] = 0;
            dependencyKinds[0] = (fmi3DependencyKind)42;
            return fmi3OK;
        case 13: {
            const derivative_instance* fixture = instance_of(instance);
            if (nDependencies != fixture->structuralSize) {
                return fmi3Error;
            }
            for (size_t i = 0; i < nDependencies; ++i) {
                elementIndicesOfDependent[i] = i + 1;
                independents[i] = 5;
                elementIndicesOfIndependents[i] = i + 1;
                dependencyKinds[i] = fmi3Dependent;
            }
            return fmi3OK;
        }
        case 11:
        case 101:
            return nDependencies == 0 ? fmi3OK : fmi3Error;
        default:
            return fmi3Error;
    }
}


fmi3Status fmi3GetFMUState(
    fmi3Instance instance,
    fmi3FMUState* state)
{
    (void)state;
    return unsupported(instance);
}


fmi3Status fmi3SetFMUState(
    fmi3Instance instance,
    fmi3FMUState state)
{
    (void)state;
    return unsupported(instance);
}


fmi3Status fmi3FreeFMUState(
    fmi3Instance instance,
    fmi3FMUState* state)
{
    (void)state;
    return unsupported(instance);
}


fmi3Status fmi3SerializedFMUStateSize(
    fmi3Instance instance,
    fmi3FMUState state,
    size_t* size)
{
    (void)state;
    (void)size;
    return unsupported(instance);
}


fmi3Status fmi3SerializeFMUState(
    fmi3Instance instance,
    fmi3FMUState state,
    fmi3Byte serializedState[],
    size_t size)
{
    (void)state;
    (void)serializedState;
    (void)size;
    return unsupported(instance);
}


fmi3Status fmi3DeserializeFMUState(
    fmi3Instance instance,
    const fmi3Byte serializedState[],
    size_t size,
    fmi3FMUState* state)
{
    (void)serializedState;
    (void)size;
    (void)state;
    return unsupported(instance);
}


fmi3Status fmi3GetIntervalDecimal(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    fmi3Float64 intervals[],
    fmi3IntervalQualifier qualifiers[])
{
    (void)references;
    (void)nReferences;
    (void)intervals;
    (void)qualifiers;
    return unsupported(instance);
}


fmi3Status fmi3GetIntervalFraction(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    fmi3UInt64 counters[],
    fmi3UInt64 resolutions[],
    fmi3IntervalQualifier qualifiers[])
{
    (void)references;
    (void)nReferences;
    (void)counters;
    (void)resolutions;
    (void)qualifiers;
    return unsupported(instance);
}


fmi3Status fmi3GetShiftDecimal(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    fmi3Float64 shifts[])
{
    (void)references;
    (void)nReferences;
    (void)shifts;
    return unsupported(instance);
}


fmi3Status fmi3GetShiftFraction(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    fmi3UInt64 counters[],
    fmi3UInt64 resolutions[])
{
    (void)references;
    (void)nReferences;
    (void)counters;
    (void)resolutions;
    return unsupported(instance);
}


fmi3Status fmi3SetIntervalDecimal(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    const fmi3Float64 intervals[])
{
    (void)references;
    (void)nReferences;
    (void)intervals;
    return unsupported(instance);
}


fmi3Status fmi3SetIntervalFraction(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    const fmi3UInt64 counters[],
    const fmi3UInt64 resolutions[])
{
    (void)references;
    (void)nReferences;
    (void)counters;
    (void)resolutions;
    return unsupported(instance);
}


fmi3Status fmi3SetShiftDecimal(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    const fmi3Float64 shifts[])
{
    (void)references;
    (void)nReferences;
    (void)shifts;
    return unsupported(instance);
}


fmi3Status fmi3SetShiftFraction(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    const fmi3UInt64 counters[],
    const fmi3UInt64 resolutions[])
{
    (void)references;
    (void)nReferences;
    (void)counters;
    (void)resolutions;
    return unsupported(instance);
}


fmi3Status fmi3EvaluateDiscreteStates(fmi3Instance instance)
{
    return unsupported(instance);
}


fmi3Status fmi3EnterContinuousTimeMode(fmi3Instance instance)
{
    return unsupported(instance);
}


fmi3Status fmi3CompletedIntegratorStep(
    fmi3Instance instance,
    fmi3Boolean noSetFMUStatePriorToCurrentPoint,
    fmi3Boolean* enterEventMode,
    fmi3Boolean* terminateSimulation)
{
    (void)noSetFMUStatePriorToCurrentPoint;
    (void)enterEventMode;
    (void)terminateSimulation;
    return unsupported(instance);
}


fmi3Status fmi3SetTime(fmi3Instance instance, fmi3Float64 time)
{
    (void)time;
    return unsupported(instance);
}


fmi3Status fmi3SetContinuousStates(
    fmi3Instance instance,
    const fmi3Float64 states[],
    size_t nStates)
{
    (void)states;
    (void)nStates;
    return unsupported(instance);
}


fmi3Status fmi3GetContinuousStateDerivatives(
    fmi3Instance instance,
    fmi3Float64 derivatives[],
    size_t nStates)
{
    (void)derivatives;
    (void)nStates;
    return unsupported(instance);
}


fmi3Status fmi3GetEventIndicators(
    fmi3Instance instance,
    fmi3Float64 eventIndicators[],
    size_t nEventIndicators)
{
    (void)eventIndicators;
    (void)nEventIndicators;
    return unsupported(instance);
}


fmi3Status fmi3GetContinuousStates(
    fmi3Instance instance,
    fmi3Float64 states[],
    size_t nStates)
{
    (void)states;
    (void)nStates;
    return unsupported(instance);
}


fmi3Status fmi3GetNominalsOfContinuousStates(
    fmi3Instance instance,
    fmi3Float64 nominals[],
    size_t nStates)
{
    (void)nominals;
    (void)nStates;
    return unsupported(instance);
}


fmi3Status fmi3GetNumberOfEventIndicators(
    fmi3Instance instance,
    size_t* nEventIndicators)
{
    (void)nEventIndicators;
    return unsupported(instance);
}


fmi3Status fmi3GetNumberOfContinuousStates(
    fmi3Instance instance,
    size_t* nStates)
{
    (void)nStates;
    return unsupported(instance);
}


fmi3Status fmi3GetOutputDerivatives(
    fmi3Instance instance,
    const fmi3ValueReference references[],
    size_t nReferences,
    const fmi3Int32 orders[],
    fmi3Float64 values[],
    size_t nValues)
{
    (void)references;
    (void)nReferences;
    (void)orders;
    (void)values;
    (void)nValues;
    return unsupported(instance);
}


fmi3Status fmi3ActivateModelPartition(
    fmi3Instance instance,
    fmi3ValueReference clockReference,
    fmi3Float64 activationTime)
{
    (void)clockReference;
    (void)activationTime;
    return unsupported(instance);
}
