#include "model.h"


static Status selectedStatus(ModelInstance* comp)
{
    switch (M(status)) {
        case 0: return OK;
        case 1: return Warning;
        case 2: return Discard;
        case 3: return Error;
        case 4: return Fatal;
        default: return Error;
    }
}


static void copyInputsToOutputs(ModelInstance* comp)
{
    M(scalar_output) = M(scalar_input);
    M(array_output)[0] = M(array_input)[0];
    M(array_output)[1] = M(array_input)[1];
}


Status setStartValues(ModelInstance* comp)
{
    M(status) = 0;
    M(scalar_input) = 1;
    M(array_input)[0] = 2;
    M(array_input)[1] = 3;
    M(scalar_output) = 1;
    M(array_output)[0] = 2;
    M(array_output)[1] = 3;
    M(input_clock) = false;
    M(output_clock) = false;
    return OK;
}


Status calculateValues(ModelInstance* comp)
{
    copyInputsToOutputs(comp);
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

    if (vr != vr_status) {
        logError(comp, "Invalid Int32 value reference.");
        return Error;
    }
    values[(*index)++] = M(status);
    return OK;
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

    if (vr != vr_status || values[*index] < 0 || values[*index] > 4) {
        logError(comp, "Invalid status value reference or status.");
        return Error;
    }
    M(status) = values[(*index)++];
    return OK;
}


Status getInt64(
    ModelInstance* comp,
    ValueReference vr,
    int64_t values[],
    size_t nValues,
    size_t* index)
{
    ASSERT_NOT_NULL2(comp);
    ASSERT_NOT_NULL2(index);

    switch (vr) {
        case vr_scalar_input:
            ASSERT_NVALUES(1);
            if (nValues > 0) ASSERT_NOT_NULL2(values);
            values[(*index)++] = M(scalar_input);
            break;
        case vr_array_input:
            ASSERT_NVALUES(2);
            if (nValues > 0) ASSERT_NOT_NULL2(values);
            values[(*index)++] = M(array_input)[0];
            values[(*index)++] = M(array_input)[1];
            break;
        case vr_empty_input:
            ASSERT_NVALUES(0);
            break;
        case vr_scalar_output:
            ASSERT_NVALUES(1);
            if (nValues > 0) ASSERT_NOT_NULL2(values);
            values[(*index)++] = M(scalar_output);
            break;
        case vr_array_output:
            ASSERT_NVALUES(2);
            if (nValues > 0) ASSERT_NOT_NULL2(values);
            values[(*index)++] = M(array_output)[0];
            values[(*index)++] = M(array_output)[1];
            break;
        case vr_empty_output:
            ASSERT_NVALUES(0);
            break;
        default:
            logError(comp, "Invalid Int64 value reference.");
            return Error;
    }

    return selectedStatus(comp);
}


Status setInt64(
    ModelInstance* comp,
    ValueReference vr,
    const int64_t values[],
    size_t nValues,
    size_t* index)
{
    ASSERT_NOT_NULL2(comp);
    ASSERT_NOT_NULL2(index);

    switch (vr) {
        case vr_scalar_input:
            ASSERT_NVALUES(1);
            if (nValues > 0) ASSERT_NOT_NULL2(values);
            M(scalar_input) = values[(*index)++];
            break;
        case vr_array_input:
            ASSERT_NVALUES(2);
            if (nValues > 0) ASSERT_NOT_NULL2(values);
            M(array_input)[0] = values[(*index)++];
            M(array_input)[1] = values[(*index)++];
            break;
        case vr_empty_input:
            ASSERT_NVALUES(0);
            break;
        default:
            logError(comp, "Invalid Int64 value reference.");
            return Error;
    }

    return selectedStatus(comp);
}


Status getClock(ModelInstance* comp, ValueReference vr, bool* value)
{
    ASSERT_NOT_NULL2(comp);
    ASSERT_NOT_NULL2(value);
    switch (vr) {
        case vr_input_clock:
            *value = M(input_clock);
            return OK;
        case vr_output_clock:
            *value = M(output_clock);
            return OK;
        default:
            logError(comp, "Invalid Clock value reference.");
            return Error;
    }
}
