#ifndef FMI2_DERIVATIVE_FIXTURE_H
#define FMI2_DERIVATIVE_FIXTURE_H

#define FMI2_DERIVATIVE_FIXTURE_GUID \
    "{2D4E616C-74A9-4B69-932A-3C6DDBF24E20}"

enum Fmi2DerivativeValueReference
{
    fmi2_vr_known_0 = 1,
    fmi2_vr_known_1 = 2,
    fmi2_vr_known_2 = 3,
    fmi2_vr_unknown_0 = 10,
    fmi2_vr_unknown_1 = 11,
    fmi2_vr_derivative_status = 100,
    fmi2_vr_directional_call_count = 101,
    fmi2_vr_non_real = 102
};

#endif
