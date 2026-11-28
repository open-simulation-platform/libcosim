// Wrap the Reference-FMU instance allocator without editing the shipped source.
#define createModelInstance reference_createModelInstance
#define freeModelInstance reference_freeModelInstance
#include "common.c"
#undef freeModelInstance
#undef createModelInstance

#include "fixture_instances.h"

static void destroy_instance(void* instance)
{
    reference_freeModelInstance(instance);
}

ModelInstance* createModelInstance(
    loggerType logger,
    intermediateUpdateType intermediateUpdate,
    void* componentEnvironment,
    const char* instanceName,
    const char* instantiationToken,
    const char* resourceLocation,
    bool loggingOn,
    InterfaceType interfaceType)
{
    ModelInstance* instance = reference_createModelInstance(
        logger, intermediateUpdate, componentEnvironment, instanceName,
        instantiationToken, resourceLocation, loggingOn, interfaceType);
    if (!fixture_track_instance(instance, destroy_instance)) {
        destroy_instance(instance);
        return NULL;
    }
    return instance;
}

void freeModelInstance(ModelInstance* instance)
{
    fixture_free_instance(instance);
}
