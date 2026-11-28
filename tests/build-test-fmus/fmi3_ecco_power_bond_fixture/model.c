#include "config.h"
#include "model.h"


Status setStartValues(ModelInstance* comp)
{
    ASSERT_NOT_NULL2(comp);

    M(input) = 0.0;
    M(output) = 0.0;
    M(gain) = 1.0;
    M(bias) = 0.0;
    comp->isDirtyValues = true;

    return OK;
}


Status calculateValues(ModelInstance* comp)
{
    ASSERT_NOT_NULL2(comp);

    M(output) = M(gain) * M(input) + M(bias) * (1.0 + comp->time);
    comp->isDirtyValues = false;

    return OK;
}


Status getFloat64(
    ModelInstance* comp,
    ValueReference vr,
    double values[],
    size_t nValues,
    size_t* index)
{
    ASSERT_NOT_NULL2(comp);
    ASSERT_NOT_NULL2(values);
    ASSERT_NOT_NULL2(index);

    calculateValues(comp);

    switch (vr) {
    case vr_time:
        ASSERT_NVALUES(1);
        values[(*index)++] = comp->time;
        return OK;
    case vr_input:
        ASSERT_NVALUES(1);
        values[(*index)++] = M(input);
        return OK;
    case vr_output:
        ASSERT_NVALUES(1);
        values[(*index)++] = M(output);
        return OK;
    case vr_gain:
        ASSERT_NVALUES(1);
        values[(*index)++] = M(gain);
        return OK;
    case vr_bias:
        ASSERT_NVALUES(1);
        values[(*index)++] = M(bias);
        return OK;
    default:
        logError(comp, "Get Float64 is not allowed for value reference %u.", vr);
        return Error;
    }
}


Status setFloat64(
    ModelInstance* comp,
    ValueReference vr,
    const double values[],
    size_t nValues,
    size_t* index)
{
    ASSERT_NOT_NULL2(comp);
    ASSERT_NOT_NULL2(values);
    ASSERT_NOT_NULL2(index);

    switch (vr) {
    case vr_input:
        ASSERT_NVALUES(1);
        M(input) = values[(*index)++];
        break;
    case vr_gain:
        ASSERT_NVALUES(1);
        M(gain) = values[(*index)++];
        break;
    case vr_bias:
        ASSERT_NVALUES(1);
        M(bias) = values[(*index)++];
        break;
    default:
        logError(comp, "Set Float64 is not allowed for value reference %u.", vr);
        return Error;
    }

    comp->isDirtyValues = true;
    return OK;
}
