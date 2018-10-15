#include "cse/fmi/glue.hpp"

#include <cassert>

#include "cse/error.hpp"
#include <cse/exception.hpp>


namespace cse
{
namespace fmi
{


variable_type to_variable_type(fmi1_base_type_enu_t t)
{
    switch (t) {
        case fmi1_base_type_real: return variable_type::real;
        case fmi1_base_type_int: return variable_type::integer;
        case fmi1_base_type_bool: return variable_type::boolean;
        case fmi1_base_type_str: return variable_type::string;
        case fmi1_base_type_enum:
            throw cse::error(
                make_error_code(errc::unsupported_feature),
                "FMI 1.0 enumeration variable types not supported yet");
        default:
            CSE_PANIC();
    }
}


variable_type to_variable_type(fmi2_base_type_enu_t t)
{
    switch (t) {
        case fmi2_base_type_real: return variable_type::real;
        case fmi2_base_type_int: return variable_type::integer;
        case fmi2_base_type_bool: return variable_type::boolean;
        case fmi2_base_type_str: return variable_type::string;
        case fmi2_base_type_enum:
            throw cse::error(
                make_error_code(errc::unsupported_feature),
                "FMI 2.0 enumeration variable types not supported yet");
        default:
            CSE_PANIC();
    }
}


variable_causality to_variable_causality(
    fmi1_causality_enu_t c,
    fmi1_variability_enu_t v)
{
    switch (c) {
        case fmi1_causality_enu_input:
            return (v == fmi1_variability_enu_parameter)
                ? variable_causality::parameter
                : variable_causality::input;
        case fmi1_causality_enu_output:
            return variable_causality::output;
        case fmi1_causality_enu_internal:
        case fmi1_causality_enu_none:
            return variable_causality::local;
        default:
            CSE_PANIC();
    }
}


variable_causality to_variable_causality(fmi2_causality_enu_t c)
{
    switch (c) {
        case fmi2_causality_enu_parameter: return variable_causality::parameter;
        case fmi2_causality_enu_calculated_parameter: return variable_causality::calculated_parameter;
        case fmi2_causality_enu_input: return variable_causality::input;
        case fmi2_causality_enu_output: return variable_causality::output;
        case fmi2_causality_enu_local:
        case fmi2_causality_enu_independent: return variable_causality::local;
        default:
            CSE_PANIC();
    }
}


variable_variability to_variable_variability(fmi1_variability_enu_t v)
{
    switch (v) {
        case fmi1_variability_enu_constant: return variable_variability::constant;
        case fmi1_variability_enu_parameter: return variable_variability::fixed;
        case fmi1_variability_enu_discrete: return variable_variability::discrete;
        case fmi1_variability_enu_continuous: return variable_variability::continuous;
        default:
            CSE_PANIC();
    }
}


variable_variability to_variable_variability(fmi2_variability_enu_t v)
{
    switch (v) {
        case fmi2_variability_enu_constant: return variable_variability::constant;
        case fmi2_variability_enu_fixed: return variable_variability::fixed;
        case fmi2_variability_enu_tunable: return variable_variability::tunable;
        case fmi2_variability_enu_discrete: return variable_variability::discrete;
        case fmi2_variability_enu_continuous: return variable_variability::continuous;
        default:
            CSE_PANIC();
    }
}


variable_description to_variable_description(fmi1_import_variable_t* fmiVariable)
{
    assert(fmiVariable != nullptr);
    const auto fmiVariability = fmi1_import_get_variability(fmiVariable);
    return {
        fmi1_import_get_variable_name(fmiVariable),
        fmi1_import_get_variable_vr(fmiVariable),
        to_variable_type(fmi1_import_get_variable_base_type(fmiVariable)),
        to_variable_causality(fmi1_import_get_causality(fmiVariable), fmiVariability),
        to_variable_variability(fmiVariability)};
}


variable_description to_variable_description(fmi2_import_variable_t* fmiVariable)
{
    assert(fmiVariable != nullptr);
    return {
        fmi2_import_get_variable_name(fmiVariable),
        fmi2_import_get_variable_vr(fmiVariable),
        to_variable_type(fmi2_import_get_variable_base_type(fmiVariable)),
        to_variable_causality(fmi2_import_get_causality(fmiVariable)),
        to_variable_variability(fmi2_import_get_variability(fmiVariable))};
}


} // namespace fmi
} // namespace cse
