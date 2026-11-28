#include "model.h"


Status setStartValues(ModelInstance* comp)
{
    M(input) = 0;
    M(snapshot) = 0;
    M(updates) = 0;
    M(periodic_input) = 0;
    M(periodic_snapshot) = 0;
    for (size_t i = 0; i < 2; ++i) {
        M(periodic_array)[i] = 0;
        M(periodic_array_snapshot)[i] = 0;
    }
    M(periodic_active) = 0;
    M(triggered_active) = 0;
    M(output_observed) = 0;
    return OK;
}


Status calculateValues(ModelInstance* comp)
{
    UNUSED(comp);
    return OK;
}


Status activateClock(ModelInstance* comp, ValueReference vr)
{
    if (vr == vr_periodic_clock) {
        M(periodic_active) = 1;
    } else if (vr == vr_triggered_clock) {
        M(triggered_active) = 1;
    } else {
        logError(comp, "Cannot activate Clock %u.", vr);
        return Error;
    }
    return OK;
}


Status getClock(ModelInstance* comp, ValueReference vr, _Bool* value)
{
    if (vr != vr_output_clock) {
        logError(comp, "Cannot get Clock %u.", vr);
        return Error;
    }
    *value = M(periodic_active) || M(triggered_active);
    M(output_observed) = *value;
    return OK;
}


static int32_t outputValue(ModelInstance* comp)
{
    return M(triggered_active) ? M(input) + 1 : M(updates) + 1;
}


Status eventUpdate(ModelInstance* comp)
{
    if (M(periodic_active)) {
        M(periodic_snapshot) = M(periodic_input);
        for (size_t i = 0; i < 2; ++i) {
            M(periodic_array_snapshot)[i] = M(periodic_array)[i];
        }
    }
    if (M(periodic_active) || M(triggered_active)) {
        M(snapshot) = outputValue(comp);
        ++M(updates);
    }
    M(periodic_active) = 0;
    M(triggered_active) = 0;
    M(output_observed) = 0;
    return OK;
}


Status getFloat64(ModelInstance* comp, ValueReference vr, double values[],
    size_t nValues, size_t* index)
{
    UNUSED(vr);
    UNUSED(values);
    UNUSED(nValues);
    UNUSED(index);
    logError(comp, "Getting Float64 is not supported.");
    return Error;
}


Status getInt32(ModelInstance* comp, ValueReference vr, int32_t values[],
    size_t nValues, size_t* index)
{
    ASSERT_NVALUES(1);
    switch (vr) {
    case vr_input:
        if (comp->state != InitializationMode && !M(triggered_active)) break;
        values[(*index)++] = M(input);
        return OK;
    case vr_output:
        if (comp->state != InitializationMode && !M(output_observed)) break;
        values[(*index)++] = outputValue(comp);
        return OK;
    case vr_snapshot:
        values[(*index)++] = M(snapshot);
        return OK;
    case vr_updates:
        values[(*index)++] = M(updates);
        return OK;
    case vr_periodic_input:
        if (comp->state != InitializationMode && !M(periodic_active)) break;
        values[(*index)++] = M(periodic_input);
        return OK;
    case vr_periodic_snapshot:
        values[(*index)++] = M(periodic_snapshot);
        return OK;
    default:
        break;
    }
    logError(comp, "Cannot get Int32 %u without its active Clock.", vr);
    return Error;
}


Status setInt32(ModelInstance* comp, ValueReference vr, const int32_t values[],
    size_t nValues, size_t* index)
{
    ASSERT_NVALUES(1);
    if (vr == vr_input &&
        (comp->state == InitializationMode || M(triggered_active))) {
        M(input) = values[(*index)++];
        return OK;
    }
    if (vr == vr_periodic_input &&
        (comp->state == InitializationMode || M(periodic_active))) {
        M(periodic_input) = values[(*index)++];
        return OK;
    }
    logError(comp, "Cannot set Int32 %u without its active Clock.", vr);
    return Error;
}


Status getUInt64(ModelInstance* comp, ValueReference vr, uint64_t values[],
    size_t nValues, size_t* index)
{
    ASSERT_NVALUES(2);
    if (vr == vr_periodic_array &&
        (comp->state == InitializationMode || M(periodic_active))) {
        for (size_t i = 0; i < 2; ++i) values[(*index)++] = M(periodic_array)[i];
        return OK;
    }
    if (vr == vr_periodic_array_snapshot) {
        for (size_t i = 0; i < 2; ++i) values[(*index)++] = M(periodic_array_snapshot)[i];
        return OK;
    }
    logError(comp, "Cannot get UInt64 %u without its active Clock.", vr);
    return Error;
}


Status setUInt64(ModelInstance* comp, ValueReference vr, const uint64_t values[],
    size_t nValues, size_t* index)
{
    ASSERT_NVALUES(2);
    if (vr == vr_periodic_array &&
        (comp->state == InitializationMode || M(periodic_active))) {
        for (size_t i = 0; i < 2; ++i) M(periodic_array)[i] = values[(*index)++];
        return OK;
    }
    logError(comp, "Cannot set UInt64 %u without its active Clock.", vr);
    return Error;
}
