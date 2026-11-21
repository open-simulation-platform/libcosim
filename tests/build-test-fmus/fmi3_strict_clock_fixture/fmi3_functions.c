#include "fmi3Functions.h"
#undef fmi3GetIntervalDecimal
#undef fmi3GetIntervalFraction
#undef fmi3GetShiftDecimal
#undef fmi3GetShiftFraction
#undef fmi3SetIntervalDecimal
#undef fmi3SetIntervalFraction
#undef fmi3SetShiftDecimal
#undef fmi3SetShiftFraction
#define fmi3GetIntervalDecimal referenceGetIntervalDecimal
#define fmi3GetIntervalFraction referenceGetIntervalFraction
#define fmi3GetShiftDecimal referenceGetShiftDecimal
#define fmi3GetShiftFraction referenceGetShiftFraction
#define fmi3SetIntervalDecimal referenceSetIntervalDecimal
#define fmi3SetIntervalFraction referenceSetIntervalFraction
#define fmi3SetShiftDecimal referenceSetShiftDecimal
#define fmi3SetShiftFraction referenceSetShiftFraction
#include "src/fmi3Functions.c"
#undef fmi3GetIntervalDecimal
#undef fmi3GetIntervalFraction
#undef fmi3GetShiftDecimal
#undef fmi3GetShiftFraction
#undef fmi3SetIntervalDecimal
#undef fmi3SetIntervalFraction
#undef fmi3SetShiftDecimal
#undef fmi3SetShiftFraction

fmi3Status fmi3GetIntervalDecimal(fmi3Instance instance,
    const fmi3ValueReference references[], size_t count,
    fmi3Float64 intervals[], fmi3IntervalQualifier qualifiers[])
{
    ModelInstance* comp = (ModelInstance*)instance;
    if (M(input) == -3) return fmi3Error;
    for (size_t i = 0; i < count; ++i) {
        if (references[i] != vr_periodic_clock) return fmi3Error;
        if (M(input) == -24 && M(updates) > 0) return fmi3Error;
        intervals[i] = (M(input) == -35 || (M(input) == -36 && M(updates) == 0)) ? 0.75
            : M(input) == -21 ? 0.0
            : M(input) == -23 && M(updates) > 0 ? -1.0
            : (M(input) == -20 && M(updates) > 0) ||
                    (M(input) == -30 && M(updates) == 2) ||
                    (M(input) == -31 && M(updates) == 1) ||
                    (M(input) == -36 && M(updates) == 1) ? 0.5 : 0.25;
        qualifiers[i] = M(input) == -1 || (M(input) == -22 && M(updates) > 0)
            || (M(input) == -30 && (M(updates) == 1 || M(updates) >= 3))
            || (M(input) == -31 && M(updates) != 1)
            || (M(input) == -36 && M(updates) >= 2)
            || (M(input) == -33 && comp->state != InitializationMode)
            ? fmi3IntervalNotYetKnown
            : M(input) == -25 || (M(input) == -20 && M(updates) > 1)
                || M(input) == -32 || (M(input) == -34 && M(updates) > 0)
                || (M(input) == -35 && M(updates) == 1)
            ? fmi3IntervalUnchanged : fmi3IntervalChanged;
    }
    return M(input) == -7 ? fmi3Warning : fmi3OK;
}

fmi3Status fmi3GetIntervalFraction(fmi3Instance instance,
    const fmi3ValueReference references[], size_t count,
    fmi3UInt64 counters[], fmi3UInt64 resolutions[],
    fmi3IntervalQualifier qualifiers[])
{
    ModelInstance* comp = (ModelInstance*)instance;
    if (M(input) == -3) return fmi3Error;
    for (size_t i = 0; i < count; ++i) {
        if (references[i] != vr_periodic_clock) return fmi3Error;
        if (M(input) == -24 && M(updates) > 0) return fmi3Error;
        counters[i] = (M(input) == -35 || (M(input) == -36 && M(updates) == 0)) ? 3
            : M(input) == -6 ? UINT64_MAX
            : M(input) == -21 ? 0 : M(input) == -20 && M(updates) > 0 ? 2 : 1;
        resolutions[i] = M(input) == -23 && M(updates) > 0 ? 0
            : M(input) == -5 || M(input) == -6 ? UINT64_MAX
            : M(input) <= -30 && M(input) >= -36 ? 4 : 3;
        if ((M(input) == -30 && M(updates) == 2) ||
            (M(input) == -31 && M(updates) == 1) ||
            (M(input) == -36 && M(updates) == 1)) counters[i] = 2;
        qualifiers[i] = M(input) == -1 || (M(input) == -22 && M(updates) > 0)
            || (M(input) == -30 && (M(updates) == 1 || M(updates) >= 3))
            || (M(input) == -31 && M(updates) != 1)
            || (M(input) == -36 && M(updates) >= 2)
            || (M(input) == -33 && comp->state != InitializationMode)
            ? fmi3IntervalNotYetKnown
            : M(input) == -25 || (M(input) == -20 && M(updates) > 1)
                || M(input) == -32 || (M(input) == -34 && M(updates) > 0)
                || (M(input) == -35 && M(updates) == 1)
            ? fmi3IntervalUnchanged : fmi3IntervalChanged;
    }
    return M(input) == -7 ? fmi3Warning : fmi3OK;
}

fmi3Status fmi3GetShiftDecimal(fmi3Instance instance,
    const fmi3ValueReference references[], size_t count, fmi3Float64 shifts[])
{
    ModelInstance* comp = (ModelInstance*)instance;
    if (M(input) == -4) return fmi3Error;
    for (size_t i = 0; i < count; ++i) {
        if (references[i] != vr_periodic_clock) return fmi3Error;
        shifts[i] = M(input) == -2 ? -1.0 : 0.125;
    }
    return M(input) == -7 ? fmi3Warning : fmi3OK;
}

fmi3Status fmi3GetShiftFraction(fmi3Instance instance,
    const fmi3ValueReference references[], size_t count,
    fmi3UInt64 counters[], fmi3UInt64 resolutions[])
{
    ModelInstance* comp = (ModelInstance*)instance;
    if (M(input) == -4) return fmi3Error;
    for (size_t i = 0; i < count; ++i) {
        if (references[i] != vr_periodic_clock) return fmi3Error;
        counters[i] = M(input) == -6 ? UINT64_MAX - 1 : 1;
        resolutions[i] = M(input) == -2 ? 0
            : M(input) == -5 || M(input) == -6 ? UINT64_MAX - 1
            : M(input) == -11 ? UINT64_C(9007199254739995) : 4;
    }
    return M(input) == -7 ? fmi3Warning : fmi3OK;
}

fmi3Status fmi3SetIntervalDecimal(fmi3Instance instance,
    const fmi3ValueReference references[], size_t count,
    const fmi3Float64 intervals[])
{
    ModelInstance* comp = (ModelInstance*)instance;
    if (M(input) == -8) return fmi3Error;
    for (size_t i = 0; i < count; ++i) {
        if (references[i] != vr_periodic_clock || intervals[i] <= 0.0 ||
            (M(input) == -9 && intervals[i] != 0.5)) return fmi3Error;
    }
    return M(input) == -7 ? fmi3Warning : fmi3OK;
}

fmi3Status fmi3SetIntervalFraction(fmi3Instance instance,
    const fmi3ValueReference references[], size_t count,
    const fmi3UInt64 counters[], const fmi3UInt64 resolutions[])
{
    ModelInstance* comp = (ModelInstance*)instance;
    if (M(input) == -8) return fmi3Error;
    for (size_t i = 0; i < count; ++i) {
        if (references[i] != vr_periodic_clock ||
            counters[i] == 0 || resolutions[i] == 0 ||
            (M(input) == -9 && (counters[i] != 2 || resolutions[i] != 4))) return fmi3Error;
    }
    return M(input) == -7 ? fmi3Warning : fmi3OK;
}

fmi3Status fmi3SetShiftDecimal(fmi3Instance instance,
    const fmi3ValueReference references[], size_t count,
    const fmi3Float64 shifts[])
{
    ModelInstance* comp = (ModelInstance*)instance;
    if (M(input) == -10) return fmi3Error;
    for (size_t i = 0; i < count; ++i) {
        if (references[i] != vr_periodic_clock || shifts[i] < 0.0 ||
            (M(input) == -9 && shifts[i] != 0.25)) return fmi3Error;
    }
    return M(input) == -7 ? fmi3Warning : fmi3OK;
}

fmi3Status fmi3SetShiftFraction(fmi3Instance instance,
    const fmi3ValueReference references[], size_t count,
    const fmi3UInt64 counters[], const fmi3UInt64 resolutions[])
{
    ModelInstance* comp = (ModelInstance*)instance;
    if (M(input) == -10) return fmi3Error;
    for (size_t i = 0; i < count; ++i) {
        if (references[i] != vr_periodic_clock || resolutions[i] == 0 ||
            (M(input) == -9 && (counters[i] != 1 || resolutions[i] != 4))) return fmi3Error;
    }
    return M(input) == -7 ? fmi3Warning : fmi3OK;
}
