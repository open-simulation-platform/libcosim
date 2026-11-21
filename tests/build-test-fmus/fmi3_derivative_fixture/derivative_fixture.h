#ifndef FMI3_DERIVATIVE_FIXTURE_H
#define FMI3_DERIVATIVE_FIXTURE_H

#define FMI3_DERIVATIVE_FIXTURE_TOKEN \
    "{A57664C8-8AD5-4E8F-BD1D-E27B45E92613}"

enum Fmi3DerivativeValueReference
{
    fmi3_vr_known_float64 = 1,
    fmi3_vr_known_float32 = 2,
    fmi3_vr_known_fixed = 3,
    fmi3_vr_structural_size = 4,
    fmi3_vr_known_structural = 5,
    fmi3_vr_output_matrix = 10,
    fmi3_vr_output_constant = 11,
    fmi3_vr_output_float32 = 12,
    fmi3_vr_output_structural = 13,
    fmi3_vr_state = 20,
    fmi3_vr_state_derivative = 21,
    fmi3_vr_derivative_status = 100,
    fmi3_vr_derivative_call_count = 101,
    fmi3_vr_non_float = 102
};

#endif
