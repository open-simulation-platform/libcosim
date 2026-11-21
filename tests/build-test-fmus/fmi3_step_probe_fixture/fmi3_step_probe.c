#include "fmi3Functions.h"
#include "model.h"

fmi3DoStepTYPE reference_fmi3DoStep;

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
    ModelInstance* model = (ModelInstance*)instance;
    const fmi3IntermediateUpdateCallback callback = model->intermediateUpdate;
    fmi3Boolean requested = fmi3False;
    fmi3Float64 end = currentCommunicationPoint + communicationStepSize;
    if (callback) {
        const fmi3Float64 callbackTime =
            currentCommunicationPoint + 0.75 * communicationStepSize;
        /* A negative start exercises a callback that withholds permission. */
        const fmi3Boolean canReturnEarly = model->earlyReturnAllowed &&
            currentCommunicationPoint >= 0.0;
        callback(model->componentEnvironment, callbackTime, fmi3False,
            fmi3False, fmi3False, canReturnEarly, &requested, &end);
    } else {
        /* Simulate an internal event without an early communication return. */
        end = currentCommunicationPoint + 0.5 * communicationStepSize;
    }

    /* Stop at the requested time if accepted; the callback is only a probe. */
    model->intermediateUpdate = NULL;
    const fmi3Boolean shortened = !callback || requested;
    const fmi3Status status = reference_fmi3DoStep(instance,
        currentCommunicationPoint,
        shortened ? end - currentCommunicationPoint : communicationStepSize,
        noSetFMUStatePriorToCurrentPoint, eventHandlingNeeded, terminateSimulation,
        earlyReturn, lastSuccessfulTime);
    model->intermediateUpdate = callback;
    if (status == fmi3OK || status == fmi3Warning) {
        if (callback && requested) {
            *earlyReturn = fmi3True;
        } else if (!callback) {
            *earlyReturn = fmi3False;
            *eventHandlingNeeded = fmi3True;
        }
    }
    return status;
}
