#include "fmi3Functions.h"
#undef fmi3UpdateDiscreteStates
#undef fmi3DoStep
#define fmi3UpdateDiscreteStates referenceUpdateDiscreteStates
#define fmi3DoStep referenceDoStep
#include "src/fmi3Functions.c"
#undef fmi3UpdateDiscreteStates
#undef fmi3DoStep

fmi3Status fmi3UpdateDiscreteStates(
    fmi3Instance instance,
    fmi3Boolean* discreteStatesNeedUpdate,
    fmi3Boolean* terminateSimulation,
    fmi3Boolean* nominalsOfContinuousStatesChanged,
    fmi3Boolean* valuesOfContinuousStatesChanged,
    fmi3Boolean* nextEventTimeDefined,
    fmi3Float64* nextEventTime)
{
    const fmi3Status status = referenceUpdateDiscreteStates(
        instance,
        discreteStatesNeedUpdate,
        terminateSimulation,
        nominalsOfContinuousStatesChanged,
        valuesOfContinuousStatesChanged,
        nextEventTimeDefined,
        nextEventTime);
    if (nextEventTimeDefined) *nextEventTimeDefined = fmi3False;
    return status;
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
    const fmi3Status status = referenceDoStep(
        instance, currentCommunicationPoint, communicationStepSize,
        noSetFMUStatePriorToCurrentPoint, eventHandlingNeeded,
        terminateSimulation, earlyReturn, lastSuccessfulTime);
    if (*earlyReturn && *lastSuccessfulTime >=
            currentCommunicationPoint + communicationStepSize) {
        *earlyReturn = fmi3False;
    }
    return status;
}
