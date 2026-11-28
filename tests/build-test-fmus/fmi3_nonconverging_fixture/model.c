#define eventUpdate referenceEventUpdate
#include "Stair/model.c"
#undef eventUpdate

Status eventUpdate(ModelInstance* comp)
{
    ASSERT_NOT_NULL2(comp);
    M(counter)++;
    comp->newDiscreteStatesNeeded = true;
    return OK;
}
