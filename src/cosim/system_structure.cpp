/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/system_structure.hpp"

#include "cosim/exception.hpp"
#include "cosim/function/description.hpp"
#include "cosim/function/function.hpp"
#include "cosim/function/utility.hpp"
#include "cosim/model_description.hpp"
#include "cosim/orchestration.hpp"
#include "cosim/time.hpp"
#include "cosim/utility/enumeration.hpp"
#include "cosim/utility/variable_alias.hpp"

#include <boost/algorithm/cxx11/any_of.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/algorithm/find_if.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>


namespace cosim
{

std::string to_text(const full_variable_name& variable)
{
    std::ostringstream stream;
    stream << variable;
    return stream.str();
}


// =============================================================================
// system_structure
// =============================================================================


namespace
{
std::shared_ptr<const model_description> required_model_description(const model& value)
{
    auto description = value.description();
    if (!description) {
        throw error(make_error_code(errc::invalid_system_structure),
            "Model description is unavailable");
    }
    return description;
}

std::string model_cache_key(const model_description& description)
{
    if (!description.uuid.empty()) {
        return "uuid:" + description.uuid;
    }
    if (!description.instantiation_token.empty()) {
        return "instantiation_token:" + description.instantiation_token;
    }
    return "name:" + description.name;
}


std::unordered_map<std::string, variable_description>
make_variable_lookup_table(const model_description& description)
{
    std::unordered_map<std::string, variable_description> table;
    for (const auto& var : description.variables) {
        table.emplace(var.name, var);
    }
    for (const auto& var : description.variables) {
        for (const auto& alias : var.aliases) {
            if (!table.emplace(alias.name, detail::describe_alias(var, alias)).second) {
                throw error(make_error_code(errc::invalid_system_structure),
                    "Duplicate variable or alias name: " + alias.name);
            }
        }
    }
    return table;
}

std::unordered_map<
    std::string,
    std::unordered_map<
        std::string,
        system_structure::function_io_description>>
make_variable_lookup_table(const function_description& description)
{
    std::unordered_map<
        std::string,
        std::unordered_map<
            std::string,
            system_structure::function_io_description>>
        table;
    for (std::size_t groupIndex = 0; groupIndex < description.io_groups.size(); ++groupIndex) {
        const auto& group = description.io_groups.at(groupIndex);
        for (std::size_t ioIndex = 0; ioIndex < group.ios.size(); ++ioIndex) {
            const auto& inputOutput = group.ios.at(ioIndex);
            table.try_emplace(group.name).first->second.emplace(inputOutput.name, system_structure::function_io_description{
                                                                                      static_cast<int>(groupIndex),
                                                                                      static_cast<int>(ioIndex),
                                                                                      inputOutput,
                                                                                  });
        }
    }
    return table;
}

std::optional<std::vector<std::uint64_t>> function_shape(
    const cosim::function_io_description& variable)
{
    std::vector<std::uint64_t> shape;
    shape.reserve(variable.dimensions.size());
    for (const auto& dimension : variable.dimensions) {
        const auto* const size = std::get_if<std::uint64_t>(&dimension);
        if (size == nullptr) {
            return std::nullopt;
        }
        shape.push_back(*size);
    }
    return shape;
}

using partial_shape = std::vector<std::optional<std::uint64_t>>;

partial_shape known_shape(const variable_description& variable)
{
    partial_shape result;
    result.reserve(variable.dimensions.size());
    for (const auto& dimension : variable.dimensions) {
        if (const auto* const fixed = std::get_if<fixed_dimension>(&dimension)) {
            result.emplace_back(fixed->size);
        } else {
            result.emplace_back(std::nullopt);
        }
    }
    return result;
}

partial_shape known_shape(const std::vector<std::uint64_t>& dimensions)
{
    partial_shape result;
    result.reserve(dimensions.size());
    for (const auto dimension : dimensions) {
        result.emplace_back(dimension);
    }
    return result;
}

bool shapes_match(
    const partial_shape& source,
    const partial_shape& target,
    std::string* reason)
{
    if (source.size() != target.size()) {
        if (reason != nullptr) {
            *reason = "Source and target array ranks do not match";
        }
        return false;
    }
    auto targetDimension = target.begin();
    for (const auto& sourceDimension : source) {
        if (sourceDimension && *targetDimension && sourceDimension != *targetDimension) {
            if (reason != nullptr) {
                *reason = "Source and target dimensions do not match";
            }
            return false;
        }
        ++targetDimension;
    }
    return true;
}


const variable_type_definition* find_declared_type(
    const model_description& model,
    const variable_description& variable)
{
    if (!variable.declared_type) {
        return nullptr;
    }
    const auto definition = boost::range::find_if(
        model.type_definitions,
        [&variable](const auto& candidate) {
            return candidate.name == *variable.declared_type;
        });
    return definition == model.type_definitions.end() ? nullptr : &*definition;
}


bool enumeration_value_is_allowed(
    const variable_description& variable,
    const variable_type_definition& definition,
    std::int64_t value)
{
    const auto* const minimum = variable.min
        ? std::get_if<enumeration_value>(&*variable.min)
        : nullptr;
    const auto* const maximum = variable.max
        ? std::get_if<enumeration_value>(&*variable.max)
        : nullptr;
    if ((variable.min && minimum == nullptr) ||
        (variable.max && maximum == nullptr)) {
        return false;
    }
    if (((minimum != nullptr) && value < minimum->value) ||
        ((maximum != nullptr) && value > maximum->value)) {
        return false;
    }
    return boost::algorithm::any_of(
        definition.enumeration_items,
        [value](const auto& item) { return item.value == value; });
}


bool enumeration_domains_match(
    const model_description& sourceModel,
    const variable_description& source,
    const model_description& targetModel,
    const variable_description& target,
    std::string* reason)
{
    const auto* const sourceType = find_declared_type(sourceModel, source);
    const auto* const targetType = find_declared_type(targetModel, target);
    if (sourceType == nullptr || targetType == nullptr ||
        sourceType->type != variable_type::enumeration ||
        targetType->type != variable_type::enumeration ||
        sourceType->enumeration_items.empty() ||
        targetType->enumeration_items.empty()) {
        if (reason != nullptr) {
            *reason =
                "Enumeration connection metadata is incomplete.";
        }
        return false;
    }

    bool sourceHasAllowedValue = false;
    for (const auto& item : sourceType->enumeration_items) {
        if (!enumeration_value_is_allowed(source, *sourceType, item.value)) {
            continue;
        }
        sourceHasAllowedValue = true;
        if (!enumeration_value_is_allowed(target, *targetType, item.value)) {
            if (reason != nullptr) {
                *reason =
                    "Enumeration source domain contains value " +
                    std::to_string(item.value) +
                    ", which the target does not accept.";
            }
            return false;
        }
    }
    if (!sourceHasAllowedValue) {
        if (reason != nullptr) {
            *reason = "Enumeration source domain has no allowed values.";
        }
        return false;
    }
    return true;
}


error make_connection_error(
    const system_structure::connection& connectionInfo, const std::string& reason)
{
    std::ostringstream msg;
    msg << "Cannot establish connection between variables "
        << connectionInfo.source << " and " << connectionInfo.target << ": " << reason;
    return {make_error_code(errc::invalid_system_structure), msg.str()};
}
} // namespace


void system_structure::add_entity(const entity& entityInfo)
{
    if (entityInfo.name.empty()) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "Invalid entity name (empty string)");
    }
    if (entities_.count(entityInfo.name) != 0U) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "Duplicate entity name: " + entityInfo.name);
    }
    assert(!functionCache_.count(entityInfo.name));

    if (const auto model = entity_type_to<cosim::model>(entityInfo.type)) {
        if (entityInfo.step_size_hint < duration::zero()) {
            throw error(
                make_error_code(errc::invalid_system_structure),
                "Negative step size hint: " + entityInfo.name);
        }
        // Make a model cache entry
        const auto description = required_model_description(*model);
        const auto modelCacheKey = model_cache_key(*description);
        if (modelCache_.count(modelCacheKey) == 0) {
            modelCache_[modelCacheKey] =
                {make_variable_lookup_table(*description)};
        }
    } else {
        // Make a function cache entry
        const auto functionType = entity_type_to<function_type>(entityInfo.type);
        assert(functionType);
        function_info info;
        try {
            info.description = substitute_function_parameters(
                functionType->description(),
                entityInfo.parameter_values);
        } catch (const std::exception& ex) {
            throw error(
                make_error_code(errc::invalid_system_structure),
                "Invalid or incomplete function parameter set: " + entityInfo.name +
                    " (" + ex.what() + ")");
        }
        info.ios = make_variable_lookup_table(info.description);
        functionCache_.emplace(entityInfo.name, std::move(info));
    }
    entities_.emplace(entityInfo.name, entityInfo);
}

system_structure::power_bond_map system_structure::get_power_bonds() const noexcept // NOLINT(bugprone-exception-escape): Preserve the public noexcept contract.
{
    return powerBonds_;
}

void system_structure::add_power_bond(std::string name, system_structure::power_bond bond)
{
    if (name.empty() || powerBonds_.count(name) != 0) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "Power bond names must be non-empty and unique: " + name);
    }

    const auto validateEndpoint =
        [this, &name](
            const full_variable_name& endpoint,
            variable_causality expectedCausality) {
            const auto& variable = get_variable_description(endpoint);
            if (variable.type != variable_type::real ||
                !variable.dimensions.empty() ||
                variable.causality != expectedCausality) {
                throw error(
                    make_error_code(errc::invalid_system_structure),
                    "ECCO power bond " + name +
                        " requires scalar Float64 input/output endpoints; invalid endpoint: " +
                        to_text(endpoint));
            }
        };

    validateEndpoint(bond.input_a, variable_causality::input);
    validateEndpoint(bond.input_b, variable_causality::input);
    validateEndpoint(bond.output_a, variable_causality::output);
    validateEndpoint(bond.output_b, variable_causality::output);
    powerBonds_.emplace(std::move(name), std::move(bond));
}

system_structure::entity_range system_structure::entities() const noexcept
{
    return boost::adaptors::values(entities_);
}


const system_structure::entity* system_structure::find_entity(
    std::string_view name) const
{
    const auto entry = entities_.find(std::string(name));
    if (entry == entities_.end()) {
        return nullptr;
    }
    return &entry->second;
}


void system_structure::connect_variables(const connection& connectionInfo)
{
    std::string validationError;
    bool valid = false;
    if (connectionInfo.source.is_simulator_variable()) {
        if (connectionInfo.target.is_simulator_variable()) {
            const auto& source = get_variable_description(connectionInfo.source);
            const auto& target = get_variable_description(connectionInfo.target);
            const auto sourceModel = required_model_description(
                *entity_type_to<cosim::model>(entities_.at(connectionInfo.source.entity_name).type));
            const auto targetModel = required_model_description(
                *entity_type_to<cosim::model>(entities_.at(connectionInfo.target.entity_name).type));
            valid = is_valid_connection(
                *sourceModel,
                source,
                *targetModel,
                target,
                &validationError);
        } else {
            valid = is_valid_connection(
                get_variable_description(connectionInfo.source),
                get_function_io_description(connectionInfo.target).description,
                &validationError);
        }
    } else {
        if (connectionInfo.target.is_simulator_variable()) {
            valid = is_valid_connection(
                get_function_io_description(connectionInfo.source).description,
                get_variable_description(connectionInfo.target),
                &validationError);
        } else {
            valid = is_valid_connection(
                get_function_io_description(connectionInfo.source).description,
                get_function_io_description(connectionInfo.target).description,
                &validationError);
        }
    }
    if (!valid) {
        throw make_connection_error(connectionInfo, validationError);
    }
    const auto cit = connections_.find(connectionInfo.target);
    if (cit != connections_.end()) {
        throw make_connection_error(
            connectionInfo,
            "Target variable is already connected to " +
                to_text(cit->second));
    }
    connections_.emplace(connectionInfo.target, connectionInfo.source);
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
    const full_variable_name& variable) const
{
    const auto sit = entities_.find(variable.entity_name);
    if (sit == entities_.end()) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "Unknown simulator name: " + variable.entity_name);
    }
    const auto model = entity_type_to<cosim::model>(sit->second.type);
    if (!model) {
        throw std::logic_error("Not a simulator: " + to_text(variable));
    }
    const auto description = required_model_description(*model);
    const auto& modelInfo = modelCache_.at(model_cache_key(*description));
    const auto vit = modelInfo.variables.find(variable.variable_name);
    if (vit == modelInfo.variables.end()) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "No such variable: " + to_text(variable));
    }
    return vit->second;
}


const system_structure::function_io_description&
system_structure::get_function_io_description(
    const full_variable_name& variable) const
{
    const auto fit = entities_.find(variable.entity_name);
    if (fit == entities_.end()) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "Unknown function name: " + variable.entity_name);
    }
    const auto functionType = entity_type_to<function_type>(fit->second.type);
    if (!functionType) {
        throw std::logic_error("Not a function: " + to_text(variable));
    }
    const auto& functionInfo = functionCache_.at(variable.entity_name);
    const auto git = functionInfo.ios.find(variable.variable_group_name);
    if (git == functionInfo.ios.end()) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "No such variable: " + to_text(variable));
    }
    const auto vit = git->second.find(variable.variable_name);
    if (vit == git->second.end()) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "No such variable: " + to_text(variable));
    }
    const auto& result = vit->second;
    const auto& group =
        functionInfo.description.io_groups.at(result.group_index);
    const auto* const groupCount = std::get_if<int>(&group.count);
    if ((groupCount == nullptr) || variable.variable_group_instance < 0 ||
        variable.variable_group_instance >= *groupCount) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "Function variable group instance out of range: " + to_text(variable));
    }
    const auto* const ioCount = std::get_if<int>(&result.description.count);
    if ((ioCount == nullptr) || variable.variable_instance < 0 ||
        variable.variable_instance >= *ioCount) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "Function variable instance out of range: " + to_text(variable));
    }
    return result;
}


// =============================================================================
// Free functions
// =============================================================================


bool is_valid_variable_value(
    const variable_description& variable,
    const scalar_value& value,
    std::string* reason)
{
    return is_valid_variable_value(variable, variable_value(value), reason);
}


bool is_valid_variable_value(
    const variable_description& variable,
    const variable_value& value,
    std::string* reason)
{
    if (value.type() != variable.type &&
        !(variable.dimensions.empty() &&
            normalize_enumeration_value(variable.type, value))) {
        if (reason != nullptr) {
            std::ostringstream msg;
            msg << "Cannot assign a value of type '" << to_text(value.type())
                << "' to a variable of type '" << to_text(variable.type)
                << "'.";
            *reason = msg.str();
        }
        return false;
    }
    const auto dimensions = fixed_shape(variable);
    if (variable.dimensions.size() != value.dimensions().size() ||
        (dimensions && *dimensions != value.dimensions())) {
        if (reason != nullptr) {
            *reason = "Value dimensions differ from the variable dimensions.";
        }
        return false;
    }
    return true;
}

namespace
{
bool value_meets_metadata_bounds(
    const variable_description& variable,
    const variable_value& value)
{
    return std::visit([&](const auto& storage) {
        using T = typename std::decay_t<decltype(storage)>::value_type;
        if constexpr (std::is_same_v<T, binary_value>) {
            return !variable.max_size ||
                std::all_of(storage.values.begin(), storage.values.end(),
                    [&](const auto& item) { return item.data.size() <= *variable.max_size; });
        } else if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            const auto min = variable.min ? std::get_if<T>(&*variable.min) : nullptr;
            const auto max = variable.max ? std::get_if<T>(&*variable.max) : nullptr;
            return std::none_of(storage.values.begin(), storage.values.end(),
                [&](T item) { return (min && item < *min) || (max && item > *max); });
        } else if constexpr (std::is_same_v<T, enumeration_value>) {
            const auto min = variable.min ? std::get_if<T>(&*variable.min) : nullptr;
            const auto max = variable.max ? std::get_if<T>(&*variable.max) : nullptr;
            return std::none_of(storage.values.begin(), storage.values.end(),
                [&](T item) {
                    return (min && item.value < min->value) ||
                        (max && item.value > max->value);
                });
        } else {
            return true;
        }
    },
        value.values());
}

bool valid_clock_connection(
    const variable_description& source,
    const variable_description& target,
    std::string* reason)
{
    if (source.causality != variable_causality::output) {
        if (reason != nullptr) {
            *reason = "Only output Clocks may be connection sources.";
        }
        return false;
    }
    if (target.causality != variable_causality::input) {
        if (reason != nullptr) {
            *reason = "Only input Clocks may be connection targets.";
        }
        return false;
    }
    if (!source.clock || !target.clock) {
        if (reason != nullptr) {
            *reason = "Clock connection metadata is incomplete.";
        }
        return false;
    }
    if (source.clock->interval_variability !=
            clock_interval_variability::triggered ||
        target.clock->interval_variability !=
            clock_interval_variability::triggered) {
        if (reason != nullptr) {
            *reason =
                "Clock connections require triggered source and target Clocks.";
        }
        return false;
    }
    return true;
}
} // namespace


bool is_valid_connection(
    const variable_description& source,
    const variable_description& target,
    std::string* reason)
{
    if (source.type != target.type) {
        if (reason != nullptr) {
            *reason = "Variable types differ.";
        }
        return false;
    }
    if (source.type == variable_type::clock) {
        return valid_clock_connection(source, target, reason);
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
    return shapes_match(known_shape(source), known_shape(target), reason);
}


bool is_valid_connection(
    const model_description& sourceModel,
    const variable_description& source,
    const model_description& targetModel,
    const variable_description& target,
    std::string* reason)
{
    if (!is_valid_connection(source, target, reason)) {
        return false;
    }
    return source.type != variable_type::enumeration ||
        enumeration_domains_match(
            sourceModel, source, targetModel, target, reason);
}


bool is_valid_connection(
    const variable_description& source,
    const function_io_description& target,
    std::string* reason)
{
    const auto* const targetType = std::get_if<variable_type>(&target.type);
    if ((targetType == nullptr) || source.type != *targetType) {
        if (reason != nullptr) {
            *reason = "Variable types differ.";
        }
        return false;
    }
    if (source.type == variable_type::clock) {
        if (reason != nullptr) {
            *reason = "Clock variables cannot be data connections.";
        }
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
    const auto targetShape = function_shape(target);
    if (!targetShape) {
        if (reason != nullptr) {
            *reason = "Function variable dimensions are unresolved.";
        }
        return false;
    }
    return shapes_match(
        known_shape(source), known_shape(*targetShape), reason);
}


bool is_valid_connection(
    const function_io_description& source,
    const variable_description& target,
    std::string* reason)
{
    const auto* const sourceType = std::get_if<variable_type>(&source.type);
    if ((sourceType == nullptr) || *sourceType != target.type) {
        if (reason != nullptr) {
            *reason = "Variable types differ.";
        }
        return false;
    }
    if (*sourceType == variable_type::clock) {
        if (reason != nullptr) {
            *reason = "Clock variables cannot be data connections.";
        }
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
    const auto sourceShape = function_shape(source);
    if (!sourceShape) {
        if (reason != nullptr) {
            *reason = "Function variable dimensions are unresolved.";
        }
        return false;
    }
    return shapes_match(
        known_shape(*sourceShape), known_shape(target), reason);
}


bool is_valid_connection(
    const function_io_description& source,
    const function_io_description& target,
    std::string* reason)
{
    const auto* const sourceType = std::get_if<variable_type>(&source.type);
    const auto* const targetType = std::get_if<variable_type>(&target.type);
    if ((sourceType == nullptr) || (targetType == nullptr) || *sourceType != *targetType) {
        if (reason != nullptr) {
            *reason = "Variable types differ.";
        }
        return false;
    }
    if (*sourceType == variable_type::clock) {
        if (reason != nullptr) {
            *reason = "Clock variables cannot be data connections.";
        }
        return false;
    }
    if (source.causality != variable_causality::output) {
        if (reason != nullptr) {
            *reason = "Only variables with causality 'output' may be used as "
                      "source variables in a connection.";
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
    const auto sourceShape = function_shape(source);
    const auto targetShape = function_shape(target);
    if (!sourceShape || !targetShape) {
        if (reason != nullptr) {
            *reason = "Function variable dimensions are unresolved.";
        }
        return false;
    }
    return shapes_match(
        known_shape(*sourceShape), known_shape(*targetShape), reason);
}


void add_variable_value(
    variable_value_map& variableValues,
    const system_structure& systemStructure,
    const full_variable_name& variable,
    variable_value value)
{
    const auto& varDescription = systemStructure.get_variable_description(variable);
    if (varDescription.variability == variable_variability::constant) {
        std::ostringstream msg;
        msg << "Cannot modify value of constant variable '" << variable << "'";
        throw error(make_error_code(errc::invalid_system_structure), msg.str());
    }
    if (std::string reason; !is_valid_variable_value(varDescription, value, &reason)) {
        std::ostringstream msg;
        msg << "Invalid value for variable '" << variable << "': " << reason;
        throw error(make_error_code(errc::invalid_system_structure), msg.str());
    }
    if (auto normalized = normalize_enumeration_value(varDescription.type, value)) {
        value = std::move(*normalized);
    }
    if (!value_meets_metadata_bounds(varDescription, value)) {
        throw error(make_error_code(errc::invalid_system_structure),
            "Initial value for variable '" + to_text(variable) + "' exceeds its declared bounds");
    }
    variableValues.insert_or_assign(variable, std::move(value));
}

void add_variable_value(
    variable_value_map& variableValues,
    const system_structure& systemStructure,
    const full_variable_name& variable,
    scalar_value value)
{
    add_variable_value(variableValues, systemStructure, variable, variable_value(std::move(value)));
}


} // namespace cosim
