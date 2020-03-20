#ifndef CSE_SYSTEM_STRUCTURE_HPP
#define CSE_SYSTEM_STRUCTURE_HPP

#include <cse/function/function.hpp>
#include <cse/model.hpp>
#include <cse/orchestration.hpp>

#include <boost/functional/hash.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/range/adaptor/transformed.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>


namespace cse
{

/**
 *  The qualified name of a variable, consisting of the entity name and
 *  the variable name.
 *
 *  The validity of the qualified name can only be determined in the context
 *  of a specific system structure (see `system_structure`).
 */
struct full_variable_name
{
    /// The name of a entity.
    std::string entity_name;

    /// The name of a variable.
    std::string variable_name;
};


inline bool operator==(
    const full_variable_name& a, const full_variable_name& b) noexcept
{
    return a.entity_name == b.entity_name &&
        a.variable_name == b.variable_name;
}

inline bool operator!=(
    const full_variable_name& a, const full_variable_name& b) noexcept
{
    return !operator==(a, b);
}

} // namespace cse

namespace std
{
template<>
struct hash<cse::full_variable_name>
{
    std::size_t operator()(const cse::full_variable_name& v) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, v.entity_name);
        boost::hash_combine(seed, v.variable_name);
        return seed;
    }
};
} // namespace std


namespace cse
{

/**
 *  A description of the structure of a modelled system.
 *
 *  The system structure description contains the list of entities in the
 *  system and the connections between them.  Validation is performed on
 *  the fly by the class' mutators, and any attempt to make an invalid change
 *  will result in an exception of type `cse::error` with error code
 *  `cse::errc::invalid_system_structure`.
 */
class system_structure
{
public:
    /**
     *  The type of an entity.
     *
     *  This is a shared pointer to a `cse::model` if the entity is a simulator,
     *  and to a `cse::function_type` if the entity is a function instance.
     */
    using entity_type = std::variant<
            std::shared_ptr<model>,
            std::shared_ptr<function_type>>;
    /**
     *  Information about a simulation entity
     *
     *  An entity may be either a entity or a function instance;
     *  this is determined by the `type` field.
     */
    struct entity
    {
        /// The entity name.
        std::string name;

        /// The entity type.
        entity_type type;
    };

    /// Information about a connection.
    struct connection
    {
        /// The source variable.
        full_variable_name source;

        /// The target variable.
        full_variable_name target;
    };

private:
    using entity_map = std::unordered_map<std::string, entity>;
    using connection_map =
        std::unordered_map<full_variable_name, full_variable_name>;
    using connection_transform =
        connection (*)(const connection_map::value_type&);

public:
    using entity_range = boost::select_second_const_range<entity_map>;
    using connection_range =
        boost::transformed_range<
            connection_transform, const connection_map>;

    /**
     *  Adds an entity to the system.
     *
     *  `e.name` must be unique in the context of the present system.
     */
    void add_entity(const entity& e);

    /// \overload
    void add_entity(std::string_view name, std::shared_ptr<cse::model> type)
    {
        add_entity({std::string(name), type});
    }

    /// \overload
    void add_entity(std::string_view name, std::shared_ptr<cse::function_type> type)
    {
        add_entity({std::string(name), type});
    }

    /**
     *  Returns a list of the entities in the system.
     *
     *  \returns
     *      A range of `entity` objects.
     */
    entity_range entities() const noexcept;

    /**
     *  Establishes a connection between two variables.
     *
     *  The same target variable may not be connected several times.
     */
    void connect_variables(const connection& c);

    /// \overload
    void connect_variables(
        const full_variable_name& source, const full_variable_name& target)
    {
        connect_variables({source, target});
    }

    /**
     *  Returns a list of the scalar connections in the system.
     *
     *  \returns
     *      A range of `connection` objects.
     */
    connection_range connections() const noexcept;

    /**
     *  Retrieves the description of a variable, given its qualified name.
     *
     *  This is a convenience function that provides fast variable lookup
     *  (O(1) on average).
     */
    const variable_description& get_variable_description(
        const full_variable_name& v) const;

private:
    // Entities, indexed by name.
    entity_map entities_;

    // Connections. Target is key, source is value.
    connection_map connections_;

    // Cache for fast lookup of model info.
    struct model_info
    {
        std::unordered_map<std::string, variable_description> variables;
    };
    std::unordered_map<std::shared_ptr<model>, model_info> modelCache_;
};


/**
 *  Converts a `cse::system_structure::entity_type` to a `cse::model`.
 *
 *  This is a convenience function that simply checks whether `et` contains
 *  a pointer to a `model`, and if so, returns it.  Otherwise, it returns null.
 */
std::shared_ptr<model> entity_type_to_model(system_structure::entity_type et) noexcept;


/**
 *  Checks whether `name` is a valid entity name.
 *
 *  The rules are the same as for C(++) identifiers:  The name may only
 *  consist of ASCII letters, numbers and underscores, and the first
 *  character must be a letter or an underscore.
 *
 *  If `reason` is non-null and the function returns `false`, a
 *  human-readable reason will be stored in the string pointed to by
 *  `reason`.
 */
bool is_valid_entity_name(std::string_view name, std::string* reason) noexcept;


/**
 *  Checks whether `value` is a valid value for a variable described by
 *  `variable`.
 *
 *  If it is not, the function will store a human-readable reason for the
 *  rejection in the string pointed to by `reason`.  If the function
 *  returns `true`, or if `reason` is null, this parameter is ignored.
 */
bool is_valid_variable_value(
    const variable_description& variable,
    const scalar_value& value,
    std::string* reason);


/**
 *  Checks whether a connection between two variables described by
 *  `source` and `target`, respectively, would be valid.
 *
 *  If it is not, the function will store a human-readable reason for the
 *  rejection in the string pointed to by `reason`.  If the function
 *  returns `true`, or if `reason` is null, this parameter is ignored.
 */
bool is_valid_connection(
    const variable_description& source,
    const variable_description& target,
    std::string* reason);


/**
 *  A container that holds a set of variable values.
 *
 *  This is a simple container that associates qualified variable names,
 *  of type `full_variable_name`, to scalar values of type `scalar_value`.
 *  Being a plain map (`std::unordered_map`, to be precise) it is not
 *  linked to a particular `system_structure` instance, and does not
 *  perform any validation of variable names or values.
 *
 *  Instead, the `add_parameter_value()` function may be used as a
 *  convenient method for populating a `parameter_set` with verified
 *  values.
 */
using parameter_set = std::unordered_map<full_variable_name, scalar_value>;


/**
 *  Validates a parameter value and adds it to a parameter set.
 *
 *  This is a convenience function which verifies that `variable` refers
 *  to a variable in `systemStructure` and that `value` is a valid value
 *  for that variable, and then adds the variable-value pair to
 *  `parameterSet`.
 *
 *  \throws `cse::error` with code `cse::errc::invalid_system_structure`
 *      if any of the above-mentioned checks fail.
 */
void add_parameter_value(
    parameter_set& parameterSet,
    const system_structure& systemStructure,
    const full_variable_name& variable,
    scalar_value value);


} // namespace cse
#endif // header guard
