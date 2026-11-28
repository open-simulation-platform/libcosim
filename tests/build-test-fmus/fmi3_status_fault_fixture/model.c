#include "model.h"


Status setStartValues(ModelInstance* comp)
{
    M(fault_status) = OK;
    M(fault_operation) = fault_operation_legacy_step;
    M(value) = 0;
    return OK;
}


Status calculateValues(ModelInstance* comp)
{
    UNUSED(comp);
    return OK;
}


Status eventUpdate(ModelInstance* comp)
{
    UNUSED(comp);
    return OK;
}


Status getFloat64(
    ModelInstance* comp,
    ValueReference vr,
    double values[],
    size_t nValues,
    size_t* index)
{
    UNUSED(vr);
    UNUSED(values);
    UNUSED(nValues);
    UNUSED(index);
    logError(comp, "Getting Float64 is not supported.");
    return Error;
}


Status getInt32(
    ModelInstance* comp,
    ValueReference vr,
    int32_t values[],
    size_t nValues,
    size_t* index)
{
    ASSERT_NOT_NULL2(comp);
    ASSERT_NOT_NULL2(values);
    ASSERT_NOT_NULL2(index);
    ASSERT_NVALUES(1);

    switch (vr) {
        case vr_fault_status:
            values[(*index)++] = M(fault_status);
            return OK;
        case vr_fault_operation:
            values[(*index)++] = M(fault_operation);
            return OK;
        case vr_value:
            values[(*index)++] = M(value);
            return OK;
        default:
            logError(comp, "Invalid Int32 value reference.");
            return Error;
    }
}


Status setInt32(
    ModelInstance* comp,
    ValueReference vr,
    const int32_t values[],
    size_t nValues,
    size_t* index)
{
    ASSERT_NOT_NULL2(comp);
    ASSERT_NOT_NULL2(values);
    ASSERT_NOT_NULL2(index);
    ASSERT_NVALUES(1);

    switch (vr) {
        case vr_fault_status:
            if (values[*index] < OK || values[*index] > Fatal) {
                logError(comp, "Invalid fault status.");
                return Error;
            }
            M(fault_status) = values[(*index)++];
            return OK;
        case vr_fault_operation:
            if (values[*index] < fault_operation_legacy_step ||
                values[*index] > fault_operation_after_endpoint) {
                logError(comp, "Invalid fault operation.");
                return Error;
            }
            M(fault_operation) = values[(*index)++];
            return OK;
        case vr_value:
            M(value) = values[(*index)++];
            return OK;
        default:
            logError(comp, "Invalid Int32 value reference.");
            return Error;
    }
}
