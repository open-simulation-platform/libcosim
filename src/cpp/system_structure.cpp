#include "cse/system_structure.hpp"

#include "cse/exception.hpp"
#include "cse/model.hpp"

#include <cctype>
#include <cstddef>
#include <unordered_map>
#include <vector>


namespace cse
{
namespace
{
std::unordered_map<std::string, variable_description*>
make_variable_lookup_table(const model_description& md)
{
    std::unordered_map<std::string, variable_description> table;
    for (const auto& var : md.variables) {
        table.emplace(var.name, var);
    }
    return table;
}
} // namespace

class system_structure::impl
{
public:
    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;
    impl(impl&&) noexcept = delete;
    impl& operator=(impl&&) = delete;

    void add_simulator(const simulator& s)
    {
        if (!is_valid_simulator_name(s.name)) {
            throw error(
                make_error_code(errc::invalid_system_structure),
                "Illegal simulator name: " + s.name);
        }
        if (simulatorLookup_.count(s.name)) {
            throw error(
                make_error_code(errc::invalid_system_structure),
                "Duplicate simulator name: " + s.name);
        }
        simulators.emplace_back(s);
        simulatorLookup.emplace(
            s.name,
            {simulators.size() - 1, make_variable_lookup_table(*s.model->model_description())});
    }

    struct variable_qname
    {
        std::string simulator_name;
        std::string variable_name;
    };

    void add_scalar_connection(const scalar_connection& c)
    {
        validate_connection(
            lookup_variable(c.source),
            lookup_variable(c.target));
    }

private:
    // Containers that hold the system structure
    std::vector<simulator> simulators_;
    std::vector<scalar_connection> scalarConnections_;

    // Simulator & variable lookup tables
    struct simulator_info
    {
        std::size_t index;
        std::unordered_map<std::string, variable_description> variables;
    };
    std::unordered_map<std::string, simulator_info> simulatorLookup_;

    const variable_description& lookup_variable(const variable_qname& v) const
    {
        const auto sit = simulatorLookup_.find(v.simulator_name);
        if (sit == simulatorLookup_.end()) {
            throw error(
                make_error_code(errc::invalid_system_structure),
                "Unknown simulator name: " + v.simulator_name);
        }
        const auto& sim = simulators_.at(sit->second.index);
        const auto vit = sit->second.variables.find(v.variable_name);
        if (vit == sit->second.variables.end()) {
            throw error(
                make_error_code(errc::invalid_system_structure),
                "Simulator '" + v.simulator_name +
                    "' of type '" + sim.model->model_description()->name +
                    "' does not have a variable named '" + v.variable_name + "'");
        }

    }

    void validate_connection
};


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


namespace
{
    bool can_connect_causalities(
        const variable_description& sourceVar,
        const variable_description& targetVar)
    {
        return sourceVar.causality == variable_causality::output &&
            targetVar.causality == variable_causality:: input;
    }

    bool can_connect_variabilities(
        const variable_description& sourceVar,
        const variable_description& targetVar)
    {

    }

    bool make_connection_validation_error(
        const variable_description& sourceVar,
        const variable_description& targetVar,
        std::string* reason)
    {
        if (reason == nullptr) return;
        if
}


bool is_valid_connection(
    const variable_description& sourceVar,
    const variable_description& targetVar,
    std::string* reason)
{
    if (sourceVar.type != targetVar.type) {
        if (reason != nullptr) *reason = "Incompatible types";
        return false;
    }
    if (!can_connect_causalities(

}

} // namespace cse
