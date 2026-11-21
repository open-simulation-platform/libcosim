/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/error.hpp"
#include "cosim/manipulator/manipulator.hpp"
#include "cosim/model_description.hpp"
#include "cosim/observer/observer.hpp"
#include "cosim/time.hpp"
#include <cosim/algorithm/simulator.hpp>
#include <cosim/exception.hpp>

#include <string>


namespace cosim
{
namespace
{

[[noreturn]] void throw_unsupported(variable_type type)
{
    throw error(
        make_error_code(errc::unsupported_feature),
        std::string("Whole-value access is not supported for ") + to_text(type));
}


void validate_scalar(variable_type type, const variable_value& value)
{
    COSIM_INPUT_CHECK(value.type() == type);
    COSIM_INPUT_CHECK(value.is_scalar());
}

} // namespace


variable_value observable::get_variable_value(
    variable_type type,
    value_reference reference) const
{
    switch (type) {
        case variable_type::real:
            return variable_value(real_values{{get_real(reference)}});
        case variable_type::integer:
            return variable_value(integer_values{{get_integer(reference)}});
        case variable_type::boolean:
            return variable_value(boolean_values{{get_boolean(reference)}});
        case variable_type::string:
            return variable_value(
                string_values{{std::string(get_string(reference))}});
        case variable_type::enumeration:
        case variable_type::float32:
        case variable_type::int8:
        case variable_type::uint8:
        case variable_type::int16:
        case variable_type::uint16:
        case variable_type::uint32:
        case variable_type::int64:
        case variable_type::uint64:
        case variable_type::binary:
        case variable_type::clock:
            break;
    }
    throw_unsupported(type);
}


void simulator::set_variable_value(
    variable_type type,
    value_reference reference,
    const variable_value& value)
{
    validate_scalar(type, value);
    switch (type) {
        case variable_type::real:
            set_real(reference, std::get<real_values>(value.values()).values.at(0));
            return;
        case variable_type::integer:
            set_integer(
                reference, std::get<integer_values>(value.values()).values.at(0));
            return;
        case variable_type::boolean:
            set_boolean(
                reference, std::get<boolean_values>(value.values()).values.at(0));
            return;
        case variable_type::string:
            set_string(
                reference, std::get<string_values>(value.values()).values.at(0));
            return;
        case variable_type::enumeration:
        case variable_type::float32:
        case variable_type::int8:
        case variable_type::uint8:
        case variable_type::int16:
        case variable_type::uint16:
        case variable_type::uint32:
        case variable_type::int64:
        case variable_type::uint64:
        case variable_type::binary:
        case variable_type::clock:
            break;
    }
    throw_unsupported(type);
}


void manipulable::set_variable_input_modifier(
    variable_type /*unused*/,
    value_reference /*unused*/,
    variable_modifier /*unused*/) // NOLINT(performance-unnecessary-value-param): Preserve the virtual by-value interface.
{
    throw error(
        make_error_code(errc::unsupported_feature),
        "Whole-value input modifiers are not supported by this simulator");
}

void manipulable::signal_input_discontinuity(variable_type /*unused*/, value_reference /*unused*/)
{
    throw error(
        make_error_code(errc::unsupported_feature),
        "Input discontinuity signaling is not supported by this simulator");
}

void manipulable::set_input_sample_discontinuity_policy(
    variable_type /*unused*/, value_reference /*unused*/, bool /*unused*/)
{
    throw error(
        make_error_code(errc::unsupported_feature),
        "Input sample discontinuity policies are not supported by this simulator");
}

void manipulable::set_input_modifier_replay_safe(
    variable_type /*unused*/, value_reference /*unused*/, bool /*unused*/)
{
    throw error(
        make_error_code(errc::unsupported_feature),
        "Replay-safe input modifiers are not supported by this simulator");
}

void simulator::apply_event_inputs()
{
    throw error(
        make_error_code(errc::unsupported_feature),
        "Staged Event Mode inputs are not supported by this simulator");
}

void simulator::stage_clocked_input(
    variable_type /*unused*/, value_reference /*unused*/, const variable_value& /*unused*/)
{
    throw error(
        make_error_code(errc::unsupported_feature),
        "Staging Clock-associated inputs is not supported by this simulator");
}

void simulator::defer_input_events(time_point /*unused*/)
{
    throw error(
        make_error_code(errc::unsupported_feature),
        "Deferring input events is not supported by this simulator");
}

void simulator::finish_input_event_coordination()
{
    throw error(
        make_error_code(errc::unsupported_feature),
        "Finishing input event staging is not supported by this simulator");
}

bool simulator::prepare_input_events(duration /*unused*/, bool /*unused*/)
{
    throw error(
        make_error_code(errc::unsupported_feature),
        "Input event staging is not supported by this simulator");
}

bool simulator::reevaluate_input_modifiers(duration /*unused*/)
{
    throw error(
        make_error_code(errc::unsupported_feature),
        "Input event changed the step size after modifiers were evaluated");
}

void simulator::refresh_event_values()
{
    throw error(
        make_error_code(errc::unsupported_feature),
        "Refreshing Event Mode values is not supported by this simulator");
}


void manipulable::set_variable_output_modifier(
    variable_type /*unused*/,
    value_reference /*unused*/,
    variable_modifier /*unused*/) // NOLINT(performance-unnecessary-value-param): Preserve the virtual by-value interface.
{
    throw error(
        make_error_code(errc::unsupported_feature),
        "Whole-value output modifiers are not supported by this simulator");
}


} // namespace cosim
