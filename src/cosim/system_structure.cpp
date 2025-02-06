/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/system_structure.hpp"

#include "cosim/exception.hpp"
#include "cosim/function/utility.hpp"
#include "cosim/utility/utility.hpp"

#include <cassert>
#include <cctype>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <unordered_map>


namespace cosim
{

std::string to_text(const full_variable_name& v)
{
    std::ostringstream s;
    s << v;
    return s.str();
}


// =============================================================================
// system_structure
// =============================================================================


namespace
{
std::unordered_map<std::string, variable_description>
make_variable_lookup_table(const model_description& md)
{
    std::unordered_map<std::string, variable_description> table;
    for (const auto& var : md.variables) {
        table.emplace(var.name, var);
    }
    return table;
}

std::unordered_map<
    std::string,
    std::unordered_map<
        std::string,
        system_structure::function_io_description>>
make_variable_lookup_table(const function_description& fd)
{
    std::unordered_map<
        std::string,
        std::unordered_map<
            std::string,
            system_structure::function_io_description>>
        table;
    for (std::size_t g = 0; g < fd.io_groups.size(); ++g) {
        const auto& group = fd.io_groups[g];
        for (std::size_t i = 0; i < group.ios.size(); ++i) {
            const auto& io = group.ios[i];
            table[group.name].emplace(
                io.name,
                system_structure::function_io_description{
                    static_cast<int>(g),
                    static_cast<int>(i),
                    io});
        }
    }
    return table;
}

error make_connection_error(
    const system_structure::connection& c, const std::string& e)
{
    std::ostringstream msg;
    msg << "Cannot establish connection between variables "
        << c.source << " and " << c.target << ": " << e;
    return error(make_error_code(errc::invalid_system_structure), msg.str());
}
} // namespace


void system_structure::add_entity(const entity& e)
{
    if (e.name.empty()) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "Invalid entity name (empty string)");
    }
    if (entities_.count(e.name)) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "Duplicate entity name: " + e.name);
    }
    assert(!functionCache_.count(e.name));

    if (const auto model = entity_type_to<cosim::model>(e.type)) {
        if (e.step_size_hint < duration::zero()) {
            throw error(
                make_error_code(errc::invalid_system_structure),
                "Negative step size hint: " + e.name);
        }
        // Make a model cache entry
        if (modelCache_.count(model->description()->uuid) == 0) {
            modelCache_[model->description()->uuid] =
                {make_variable_lookup_table(*model->description())};
        }
    } else {
        // Make a function cache entry
        const auto functionType = entity_type_to<function_type>(e.type);
        assert(functionType);
        function_info info;
        try {
            info.description = substitute_function_parameters(
                functionType->description(),
                e.parameter_values);
        } catch (const std::exception& ex) {
            throw error(
                make_error_code(errc::invalid_system_structure),
                "Invalid or incomplete function parameter set: " + e.name +
                    " (" + ex.what() + ")");
        }
        info.ios = make_variable_lookup_table(info.description);
        functionCache_.emplace(e.name, std::move(info));
    }
    entities_.emplace(e.name, e);
}

system_structure::power_bond_map system_structure::get_power_bonds() const noexcept
{
    return powerBonds_;
}

void system_structure::add_power_bond(std::string name, system_structure::power_bond pb)
{
    powerBonds_.emplace(name, pb);
}


system_structure::entity_range system_structure::entities() const noexcept
{
    return boost::adaptors::values(entities_);
}


const system_structure::entity* system_structure::find_entity(
    std::string_view name) const
{
    const auto it = entities_.find(std::string(name));
    if (it == entities_.end()) return nullptr;
    return &it->second;
}


void system_structure::connect_variables(const connection& c)
{
    std::string validationError;
    bool valid = false;
    if (c.source.is_simulator_variable()) {
        if (c.target.is_simulator_variable()) {
            valid = is_valid_connection(
                get_variable_description(c.source),
                get_variable_description(c.target),
                &validationError);
        } else {
            valid = is_valid_connection(
                get_variable_description(c.source),
                get_function_io_description(c.target).description,
                &validationError);
        }
    } else {
        valid = is_valid_connection(
            get_function_io_description(c.source).description,
            get_variable_description(c.target),
            &validationError);
    }
    if (!valid) {
        throw make_connection_error(c, validationError);
    }
    const auto cit = connections_.find(c.target);
    if (cit != connections_.end()) {
        throw make_connection_error(
            c,
            "Target variable is already connected to " +
                to_text(cit->second));
    }
    connections_.emplace(c.target, c.source);
}


namespace
{
system_structure::connection to_connection(
    const std::pair<const full_variable_name, full_variable_name>& varPair)
{
    return {varPair.second, varPair.first};
}
} // namespace


system_structure::connection_range system_structure::connections()
    const noexcept
{
    return boost::adaptors::transform(connections_, to_connection);
}


const variable_description& system_structure::get_variable_description(
    const full_variable_name& v) const
{
    const auto sit = entities_.find(v.entity_name);
    if (sit == entities_.end()) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "Unknown simulator name: " + v.entity_name);
    }
    const auto model = entity_type_to<cosim::model>(sit->second.type);
    if (!model) {
        throw std::logic_error("Not a simulator: " + to_text(v));
    }
    const auto& modelInfo = modelCache_.at(model->description()->uuid);
    const auto vit = modelInfo.variables.find(v.variable_name);
    if (vit == modelInfo.variables.end()) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "No such variable: " + to_text(v));
    }
    return vit->second;
}


const system_structure::function_io_description&
system_structure::get_function_io_description(
    const full_variable_name& v) const
{
    const auto fit = entities_.find(v.entity_name);
    if (fit == entities_.end()) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "Unknown function name: " + v.entity_name);
    }
    const auto functionType = entity_type_to<function_type>(fit->second.type);
    if (!functionType) {
        throw std::logic_error("Not a function: " + to_text(v));
    }
    const auto& functionInfo = functionCache_.at(v.entity_name);
    const auto git = functionInfo.ios.find(v.variable_group_name);
    if (git == functionInfo.ios.end()) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "No such variable: " + to_text(v));
    }
    const auto vit = git->second.find(v.variable_name);
    if (vit == git->second.end()) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "No such variable: " + to_text(v));
    }
    return vit->second;
}


// =============================================================================
// Free functions
// =============================================================================


bool is_valid_variable_value(
    const variable_description& variable,
    const scalar_value& value,
    std::string* reason)
{
    const auto valueType = std::visit(
        visitor(
            [=](double) { return variable_type::real; },
            [=](int) { return variable_type::integer; },
            [=](const std::string&) { return variable_type::string; },
            [=](bool) { return variable_type::boolean; }),
        value);
    if (valueType != variable.type) {
        if (reason != nullptr) {
            std::ostringstream msg;
            msg << "Cannot assign a value of type '" << to_text(valueType)
                << "' to a variable of type '" << to_text(variable.type)
                << "'.";
            *reason = msg.str();
        }
        return false;
    }
    // TODO: Check min/max value too.
    return true;
}


bool is_valid_connection(
    const variable_description& source,
    const variable_description& target,
    std::string* reason)
{
    if (source.type != target.type) {
        if (reason != nullptr) *reason = "Variable types differ.";
        return false;
    }
    if (source.causality != variable_causality::calculated_parameter &&
        source.causality != variable_causality::output) {
        if (reason != nullptr) {
            *reason = "Only variables with causality 'output' or 'calculated "
                      "parameter' may be used as source variables in a connection.";
        }
        return false;
    }
    if (target.causality != variable_causality::input) {
        if (reason != nullptr) {
            *reason = "Only variables with causality 'input' may be used as "
                      "target variables in a connection.";
        }
        return false;
    }
    if (target.variability == variable_variability::constant ||
        target.variability == variable_variability::fixed) {
        if (reason != nullptr) {
            *reason = "The target variable is not modifiable.";
        }
        return false;
    }
    return true;
}


bool is_valid_connection(
    const variable_description& source,
    const function_io_description& target,
    std::string* reason)
{
    if (source.type != std::get<variable_type>(target.type)) {
        if (reason != nullptr) *reason = "Variable types differ.";
        return false;
    }
    if (source.causality != variable_causality::calculated_parameter &&
        source.causality != variable_causality::output) {
        if (reason != nullptr) {
            *reason = "Only variables with causality 'output' or 'calculated "
                      "parameter' may be used as source variables in a connection.";
        }
        return false;
    }
    if (target.causality != variable_causality::input) {
        if (reason != nullptr) {
            *reason = "Only variables with causality 'input' may be used as "
                      "target variables in a connection.";
        }
        return false;
    }
    return true;
}


bool is_valid_connection(
    const function_io_description& source,
    const variable_description& target,
    std::string* reason)
{
    if (std::get<variable_type>(source.type) != target.type) {
        if (reason != nullptr) *reason = "Variable types differ.";
        return false;
    }
    if (source.causality != variable_causality::calculated_parameter &&
        source.causality != variable_causality::output) {
        if (reason != nullptr) {
            *reason = "Only variables with causality 'output' or 'calculated "
                      "parameter' may be used as source variables in a connection.";
        }
        return false;
    }
    if (target.causality != variable_causality::input) {
        if (reason != nullptr) {
            *reason = "Only variables with causality 'input' may be used as "
                      "target variables in a connection.";
        }
        return false;
    }
    if (target.variability == variable_variability::constant ||
        target.variability == variable_variability::fixed) {
        if (reason != nullptr) {
            *reason = "The target variable is not modifiable.";
        }
        return false;
    }
    return true;
}


void add_variable_value(
    variable_value_map& variableValues,
    const system_structure& systemStructure,
    const full_variable_name& variable,
    scalar_value value)
{
    const auto& varDescription = systemStructure.get_variable_description(variable);
    if (varDescription.variability == variable_variability::constant) {
        std::ostringstream msg;
        msg << "Cannot modify value of constant variable '" << variable << "'";
        throw error(make_error_code(errc::invalid_system_structure), msg.str());
    }
    if (std::string e; !is_valid_variable_value(varDescription, value, &e)) {
        std::ostringstream msg;
        msg << "Invalid value for variable '" << variable << "': " << e;
        throw error(make_error_code(errc::invalid_system_structure), msg.str());
    }
    variableValues.insert_or_assign(variable, value);
}


} // namespace cosim
