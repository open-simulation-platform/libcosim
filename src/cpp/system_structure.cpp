#include "cse/system_structure.hpp"

#include "cse/exception.hpp"
#include "cse/model.hpp"
#include "cse/utility/utility.hpp"

#include <cctype>
#include <cstddef>
#include <ostream>
#include <sstream>
#include <unordered_map>


namespace cse
{


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

std::ostream& operator<<(std::ostream& s, const variable_qname& v)
{
    return s << v.simulator_name << '.' << v.variable_name;
}

std::string to_text(const variable_qname& v)
{
    std::ostringstream s;
    s << v;
    return s.str();
}

error make_connection_error(
    const system_structure::scalar_connection& c, const std::string& e)
{
    std::ostringstream msg;
    msg << "Cannot establish connection between variables "
        << c.source << " and " << c.target << ": " << e;
    return error(make_error_code(errc::invalid_system_structure), msg.str());
}
} // namespace


void system_structure::add_simulator(const simulator& s)
{
    if (!is_valid_simulator_name(s.name)) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "Illegal simulator name: " + s.name);
    }
    if (simulators_.count(s.name)) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "Duplicate simulator name: " + s.name);
    }
    simulators_.emplace(s.name, s);
    if (modelCache_.count(s.model->description()->uuid) == 0) {
        modelCache_[s.model->description()->uuid] =
            {make_variable_lookup_table(*s.model->description())};
    }
}


system_structure::simulator_range system_structure::simulators() const
{
    return boost::adaptors::values(simulators_);
}


void system_structure::add_scalar_connection(const scalar_connection& c)
{
    std::string validationError;
    const auto valid = is_valid_connection(
        get_variable_description(c.source),
        get_variable_description(c.target),
        &validationError);
    if (!valid) {
        throw make_connection_error(c, validationError);
    }
    const auto cit = scalarConnections_.find(c.target);
    if (cit != scalarConnections_.end()) {
        throw make_connection_error(
            c,
            "Target variable is already connected to " +
                to_text(cit->second));
    }
    scalarConnections_.emplace(c.target, c.source);
}


namespace
{
    system_structure::scalar_connection to_scalar_connection(
        const std::pair<const variable_qname, variable_qname>& varPair)
    {
        return {varPair.second, varPair.first};
    }
}


system_structure::scalar_connection_range system_structure::scalar_connections()
    const
{
    return boost::adaptors::transform(scalarConnections_, to_scalar_connection);
}


const variable_description& system_structure::get_variable_description(
    const variable_qname& v) const
{
    const auto sit = simulators_.find(v.simulator_name);
    if (sit == simulators_.end()) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "Unknown simulator name: " + v.simulator_name);
    }
    const auto& modelInfo =
        modelCache_.at(sit->second.model->description()->uuid);

    const auto vit = modelInfo.variables.find(v.variable_name);
    if (vit == modelInfo.variables.end()) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "Simulator '" + v.simulator_name +
                "' of type '" + sit->second.model->description()->name +
                "' does not have a variable named '" + v.variable_name + "'");
    }
    return vit->second;
}


// =============================================================================
// Free functions
// =============================================================================


bool is_valid_simulator_name(std::string_view name) noexcept
{
    if (name.empty()) return false;
    if (!std::isalpha(static_cast<unsigned char>(name[0])) && name[0] != '_') {
        return false;
    }
    for (std::size_t i = 1; i < name.size(); ++i) {
        if (!std::isalnum(static_cast<unsigned char>(name[i])) && name[i] != '_') {
            return false;
        }
    }
    return true;
}


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
            *reason = "Cannot assign a value of type '" +
                to_text(valueType) + "' to a variable of type '" +
                to_text(variable.type) + "'.";
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


void add_parameter_value(
    parameter_set& parameterSet,
    const system_structure& systemStructure,
    variable_qname variable,
    scalar_value value)
{
    const auto& varDescription = systemStructure.get_variable_description(variable);
    std::string failReason;
    if (!is_valid_variable_value(varDescription, value, &failReason)) {
        std::ostringstream msg;
        msg << "Invalid value for variable '" << variable << "': " << failReason;
        throw error(make_error_code(errc::invalid_system_structure), msg.str());
    }
    parameterSet.insert_or_assign(std::string(parameterName), value);
}


} // namespace cse
