#include <FMI2/fmi2Functions.h>

#include <stdlib.h>


enum
{
    vr_real_input = 1,
    vr_real_output = 2,
    vr_integer_input = 3,
    vr_integer_output = 4,
    vr_boolean_input = 5,
    vr_boolean_output = 6,
    vr_enumeration_input = 7,
    vr_enumeration_output = 8,
    vr_signed_enumeration_input = 9,
    vr_signed_enumeration_output = 10,
    vr_status = 11
};


typedef struct
{
    const fmi2CallbackFunctions* functions;
    fmi2Real realInput;
    fmi2Real realOutput;
    fmi2Integer integerInput;
    fmi2Integer integerOutput;
    fmi2Boolean booleanInput;
    fmi2Boolean booleanOutput;
    fmi2Integer enumerationInput;
    fmi2Integer enumerationOutput;
    fmi2Integer signedEnumerationInput;
    fmi2Integer signedEnumerationOutput;
    fmi2Integer status;
    fmi2Real time;
} mixed_types_instance;


static mixed_types_instance* get_instance(fmi2Component component)
{
    return (mixed_types_instance*)component;
}


static fmi2Status validate_component(fmi2Component component)
{
    return component == NULL ? fmi2Fatal : fmi2OK;
}


static void update_outputs(mixed_types_instance* instance)
{
    instance->realOutput = instance->realInput;
    instance->integerOutput = instance->integerInput;
    instance->booleanOutput = instance->booleanInput;
    instance->enumerationOutput = instance->enumerationInput;
    instance->signedEnumerationOutput = instance->signedEnumerationInput;
}


static void* allocate_instance(
    const fmi2CallbackFunctions* functions,
    size_t number,
    size_t size)
{
    if (functions != NULL && functions->allocateMemory != NULL) {
        return functions->allocateMemory(number, size);
    }
    return calloc(number, size);
}


static void free_instance_memory(mixed_types_instance* instance)
{
    if (instance->functions != NULL &&
        instance->functions->freeMemory != NULL) {
        instance->functions->freeMemory(instance);
    } else {
        free(instance);
    }
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
    (void)component;
    (void)loggingOn;
    (void)numberOfCategories;
    (void)categories;
    return fmi2OK;
}


fmi2Component fmi2Instantiate(
    fmi2String instanceName,
    fmi2Type fmuType,
    fmi2String fmuGUID,
    fmi2String resourceLocation,
    const fmi2CallbackFunctions* functions,
    fmi2Boolean visible,
    fmi2Boolean loggingOn)
{
    (void)instanceName;
    (void)fmuGUID;
    (void)resourceLocation;
    (void)visible;
    (void)loggingOn;
    if (fmuType != fmi2CoSimulation) {
        return NULL;
    }

    mixed_types_instance* instance =
        (mixed_types_instance*)allocate_instance(functions, 1, sizeof(*instance));
    if (instance == NULL) {
        return NULL;
    }
    instance->functions = functions;
    instance->realInput = 0.0;
    instance->realOutput = 0.0;
    instance->integerInput = 0;
    instance->integerOutput = 0;
    instance->booleanInput = fmi2False;
    instance->booleanOutput = fmi2False;
    instance->enumerationInput = 1;
    instance->enumerationOutput = 1;
    instance->signedEnumerationInput = -2147483647 - 1;
    instance->signedEnumerationOutput = -2147483647 - 1;
    instance->status = 0;
    instance->time = 0.0;
    return (fmi2Component)instance;
}


void fmi2FreeInstance(fmi2Component component)
{
    if (component != NULL) {
        free_instance_memory(get_instance(component));
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
    if (validate_component(component) != fmi2OK) {
        return fmi2Fatal;
    }
    get_instance(component)->time = startTime;
    return fmi2OK;
}


fmi2Status fmi2EnterInitializationMode(fmi2Component component)
{
    return validate_component(component);
}


fmi2Status fmi2ExitInitializationMode(fmi2Component component)
{
    if (validate_component(component) != fmi2OK) {
        return fmi2Fatal;
    }
    update_outputs(get_instance(component));
    return fmi2OK;
}


fmi2Status fmi2Terminate(fmi2Component component)
{
    return validate_component(component);
}


fmi2Status fmi2Reset(fmi2Component component)
{
    if (validate_component(component) != fmi2OK) {
        return fmi2Fatal;
    }
    mixed_types_instance* instance = get_instance(component);
    instance->realInput = 0.0;
    instance->realOutput = 0.0;
    instance->integerInput = 0;
    instance->integerOutput = 0;
    instance->booleanInput = fmi2False;
    instance->booleanOutput = fmi2False;
    instance->enumerationInput = 1;
    instance->enumerationOutput = 1;
    instance->signedEnumerationInput = -2147483647 - 1;
    instance->signedEnumerationOutput = -2147483647 - 1;
    instance->status = 0;
    instance->time = 0.0;
    return fmi2OK;
}


fmi2Status fmi2GetReal(
    fmi2Component component,
    const fmi2ValueReference valueReferences[],
    size_t numberOfValueReferences,
    fmi2Real values[])
{
    if (validate_component(component) != fmi2OK ||
        (numberOfValueReferences != 0 &&
         (valueReferences == NULL || values == NULL))) {
        return fmi2Fatal;
    }
    mixed_types_instance* instance = get_instance(component);
    update_outputs(instance);
    for (size_t i = 0; i < numberOfValueReferences; ++i) {
        switch (valueReferences[i]) {
            case vr_real_input:
                values[i] = instance->realInput;
                break;
            case vr_real_output:
                values[i] = instance->realOutput;
                break;
            default:
                return fmi2Error;
        }
    }
    return fmi2OK;
}


fmi2Status fmi2GetInteger(
    fmi2Component component,
    const fmi2ValueReference valueReferences[],
    size_t numberOfValueReferences,
    fmi2Integer values[])
{
    if (validate_component(component) != fmi2OK ||
        (numberOfValueReferences != 0 &&
         (valueReferences == NULL || values == NULL))) {
        return fmi2Fatal;
    }
    mixed_types_instance* instance = get_instance(component);
    update_outputs(instance);
    for (size_t i = 0; i < numberOfValueReferences; ++i) {
        switch (valueReferences[i]) {
            case vr_integer_input:
                values[i] = instance->integerInput;
                break;
            case vr_integer_output:
                values[i] = instance->integerOutput;
                break;
            case vr_enumeration_input:
                values[i] = instance->enumerationInput;
                break;
            case vr_enumeration_output:
                values[i] = instance->enumerationOutput;
                break;
            case vr_signed_enumeration_input:
                values[i] = instance->signedEnumerationInput;
                break;
            case vr_signed_enumeration_output:
                values[i] = instance->signedEnumerationOutput;
                break;
            case vr_status:
                values[i] = instance->status;
                break;
            default:
                return fmi2Error;
        }
    }
    switch (instance->status) {
        case 0: return fmi2OK;
        case 1: return fmi2Warning;
        case 2: return fmi2Discard;
        case 3: return fmi2Error;
        case 4: return fmi2Fatal;
        default: return fmi2Error;
    }
}


fmi2Status fmi2GetBoolean(
    fmi2Component component,
    const fmi2ValueReference valueReferences[],
    size_t numberOfValueReferences,
    fmi2Boolean values[])
{
    if (validate_component(component) != fmi2OK ||
        (numberOfValueReferences != 0 &&
         (valueReferences == NULL || values == NULL))) {
        return fmi2Fatal;
    }
    mixed_types_instance* instance = get_instance(component);
    update_outputs(instance);
    for (size_t i = 0; i < numberOfValueReferences; ++i) {
        switch (valueReferences[i]) {
            case vr_boolean_input:
                values[i] = instance->booleanInput;
                break;
            case vr_boolean_output:
                values[i] = instance->booleanOutput;
                break;
            default:
                return fmi2Error;
        }
    }
    return fmi2OK;
}


fmi2Status fmi2GetString(
    fmi2Component component,
    const fmi2ValueReference valueReferences[],
    size_t numberOfValueReferences,
    fmi2String values[])
{
    (void)valueReferences;
    (void)values;
    if (validate_component(component) != fmi2OK) {
        return fmi2Fatal;
    }
    return numberOfValueReferences == 0 ? fmi2OK : fmi2Error;
}


fmi2Status fmi2SetReal(
    fmi2Component component,
    const fmi2ValueReference valueReferences[],
    size_t numberOfValueReferences,
    const fmi2Real values[])
{
    if (validate_component(component) != fmi2OK ||
        (numberOfValueReferences != 0 &&
         (valueReferences == NULL || values == NULL))) {
        return fmi2Fatal;
    }
    mixed_types_instance* instance = get_instance(component);
    for (size_t i = 0; i < numberOfValueReferences; ++i) {
        if (valueReferences[i] != vr_real_input) {
            return fmi2Error;
        }
        instance->realInput = values[i];
    }
    return fmi2OK;
}


fmi2Status fmi2SetInteger(
    fmi2Component component,
    const fmi2ValueReference valueReferences[],
    size_t numberOfValueReferences,
    const fmi2Integer values[])
{
    if (validate_component(component) != fmi2OK ||
        (numberOfValueReferences != 0 &&
         (valueReferences == NULL || values == NULL))) {
        return fmi2Fatal;
    }
    mixed_types_instance* instance = get_instance(component);
    for (size_t i = 0; i < numberOfValueReferences; ++i) {
        switch (valueReferences[i]) {
            case vr_integer_input:
                instance->integerInput = values[i];
                break;
            case vr_enumeration_input:
                instance->enumerationInput = values[i];
                break;
            case vr_signed_enumeration_input:
                instance->signedEnumerationInput = values[i];
                break;
            case vr_status:
                if (values[i] < 0 || values[i] > 4) {
                    return fmi2Error;
                }
                instance->status = values[i];
                break;
            default:
                return fmi2Error;
        }
    }
    return fmi2OK;
}


fmi2Status fmi2SetBoolean(
    fmi2Component component,
    const fmi2ValueReference valueReferences[],
    size_t numberOfValueReferences,
    const fmi2Boolean values[])
{
    if (validate_component(component) != fmi2OK ||
        (numberOfValueReferences != 0 &&
         (valueReferences == NULL || values == NULL))) {
        return fmi2Fatal;
    }
    mixed_types_instance* instance = get_instance(component);
    for (size_t i = 0; i < numberOfValueReferences; ++i) {
        if (valueReferences[i] != vr_boolean_input) {
            return fmi2Error;
        }
        instance->booleanInput = values[i] == fmi2False ? fmi2False : fmi2True;
    }
    return fmi2OK;
}


fmi2Status fmi2SetString(
    fmi2Component component,
    const fmi2ValueReference valueReferences[],
    size_t numberOfValueReferences,
    const fmi2String values[])
{
    (void)valueReferences;
    (void)values;
    if (validate_component(component) != fmi2OK) {
        return fmi2Fatal;
    }
    return numberOfValueReferences == 0 ? fmi2OK : fmi2Error;
}


fmi2Status fmi2GetFMUstate(fmi2Component component, fmi2FMUstate* state)
{
    (void)component;
    if (state != NULL) {
        *state = NULL;
    }
    return fmi2Error;
}


fmi2Status fmi2SetFMUstate(fmi2Component component, fmi2FMUstate state)
{
    (void)component;
    (void)state;
    return fmi2Error;
}


fmi2Status fmi2FreeFMUstate(fmi2Component component, fmi2FMUstate* state)
{
    (void)component;
    if (state != NULL) {
        *state = NULL;
    }
    return fmi2Error;
}


fmi2Status fmi2SerializedFMUstateSize(
    fmi2Component component,
    fmi2FMUstate state,
    size_t* size)
{
    (void)component;
    (void)state;
    if (size != NULL) {
        *size = 0;
    }
    return fmi2Error;
}


fmi2Status fmi2SerializeFMUstate(
    fmi2Component component,
    fmi2FMUstate state,
    fmi2Byte serializedState[],
    size_t size)
{
    (void)component;
    (void)state;
    (void)serializedState;
    (void)size;
    return fmi2Error;
}


fmi2Status fmi2DeSerializeFMUstate(
    fmi2Component component,
    const fmi2Byte serializedState[],
    size_t size,
    fmi2FMUstate* state)
{
    (void)component;
    (void)serializedState;
    (void)size;
    if (state != NULL) {
        *state = NULL;
    }
    return fmi2Error;
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
    (void)component;
    (void)unknowns;
    (void)numberOfUnknowns;
    (void)knowns;
    (void)numberOfKnowns;
    (void)seed;
    (void)sensitivity;
    return fmi2Error;
}


fmi2Status fmi2EnterEventMode(fmi2Component component)
{
    (void)component;
    return fmi2Error;
}


fmi2Status fmi2NewDiscreteStates(
    fmi2Component component,
    fmi2EventInfo* eventInfo)
{
    (void)component;
    if (eventInfo != NULL) {
        eventInfo->newDiscreteStatesNeeded = fmi2False;
        eventInfo->terminateSimulation = fmi2False;
        eventInfo->nominalsOfContinuousStatesChanged = fmi2False;
        eventInfo->valuesOfContinuousStatesChanged = fmi2False;
        eventInfo->nextEventTimeDefined = fmi2False;
        eventInfo->nextEventTime = 0.0;
    }
    return fmi2Error;
}


fmi2Status fmi2EnterContinuousTimeMode(fmi2Component component)
{
    (void)component;
    return fmi2Error;
}


fmi2Status fmi2CompletedIntegratorStep(
    fmi2Component component,
    fmi2Boolean noSetFMUStatePriorToCurrentPoint,
    fmi2Boolean* enterEventMode,
    fmi2Boolean* terminateSimulation)
{
    (void)component;
    (void)noSetFMUStatePriorToCurrentPoint;
    if (enterEventMode != NULL) {
        *enterEventMode = fmi2False;
    }
    if (terminateSimulation != NULL) {
        *terminateSimulation = fmi2False;
    }
    return fmi2Error;
}


fmi2Status fmi2SetTime(fmi2Component component, fmi2Real time)
{
    (void)component;
    (void)time;
    return fmi2Error;
}


fmi2Status fmi2SetContinuousStates(
    fmi2Component component,
    const fmi2Real states[],
    size_t numberOfStates)
{
    (void)component;
    (void)states;
    (void)numberOfStates;
    return fmi2Error;
}


fmi2Status fmi2GetDerivatives(
    fmi2Component component,
    fmi2Real derivatives[],
    size_t numberOfStates)
{
    (void)component;
    (void)derivatives;
    (void)numberOfStates;
    return fmi2Error;
}


fmi2Status fmi2GetEventIndicators(
    fmi2Component component,
    fmi2Real eventIndicators[],
    size_t numberOfEventIndicators)
{
    (void)component;
    (void)eventIndicators;
    (void)numberOfEventIndicators;
    return fmi2Error;
}


fmi2Status fmi2GetContinuousStates(
    fmi2Component component,
    fmi2Real states[],
    size_t numberOfStates)
{
    (void)component;
    (void)states;
    (void)numberOfStates;
    return fmi2Error;
}


fmi2Status fmi2GetNominalsOfContinuousStates(
    fmi2Component component,
    fmi2Real nominals[],
    size_t numberOfStates)
{
    (void)component;
    (void)nominals;
    (void)numberOfStates;
    return fmi2Error;
}


fmi2Status fmi2SetRealInputDerivatives(
    fmi2Component component,
    const fmi2ValueReference valueReferences[],
    size_t numberOfValueReferences,
    const fmi2Integer orders[],
    const fmi2Real values[])
{
    (void)component;
    (void)valueReferences;
    (void)numberOfValueReferences;
    (void)orders;
    (void)values;
    return fmi2Error;
}


fmi2Status fmi2GetRealOutputDerivatives(
    fmi2Component component,
    const fmi2ValueReference valueReferences[],
    size_t numberOfValueReferences,
    const fmi2Integer orders[],
    fmi2Real values[])
{
    (void)component;
    (void)valueReferences;
    (void)numberOfValueReferences;
    (void)orders;
    (void)values;
    return fmi2Error;
}


fmi2Status fmi2DoStep(
    fmi2Component component,
    fmi2Real currentCommunicationPoint,
    fmi2Real communicationStepSize,
    fmi2Boolean noSetFMUStatePriorToCurrentPoint)
{
    (void)noSetFMUStatePriorToCurrentPoint;
    if (validate_component(component) != fmi2OK) {
        return fmi2Fatal;
    }
    mixed_types_instance* instance = get_instance(component);
    instance->time = currentCommunicationPoint + communicationStepSize;
    update_outputs(instance);
    return fmi2OK;
}


fmi2Status fmi2CancelStep(fmi2Component component)
{
    return validate_component(component);
}


fmi2Status fmi2GetStatus(
    fmi2Component component,
    const fmi2StatusKind statusKind,
    fmi2Status* value)
{
    (void)statusKind;
    if (validate_component(component) != fmi2OK || value == NULL) {
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
    if (validate_component(component) != fmi2OK || value == NULL) {
        return fmi2Fatal;
    }
    *value = get_instance(component)->time;
    return fmi2OK;
}


fmi2Status fmi2GetIntegerStatus(
    fmi2Component component,
    const fmi2StatusKind statusKind,
    fmi2Integer* value)
{
    (void)statusKind;
    if (validate_component(component) != fmi2OK || value == NULL) {
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
    if (validate_component(component) != fmi2OK || value == NULL) {
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
    if (validate_component(component) != fmi2OK || value == NULL) {
        return fmi2Fatal;
    }
    *value = "";
    return fmi2OK;
}
