#ifndef CSE_SYSTEM_STRUCTURE_HPP
#define CSE_SYSTEM_STRUCTURE_HPP

#include <cse/function/function.hpp>
#include <cse/model.hpp>
#include <cse/orchestration.hpp>

#include <boost/functional/hash.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/range/adaptor/transformed.hpp>

#include <memory>
#include <ostream>
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
    /// Constructor for simulator variables.
    full_variable_name(std::string simulatorName, std::string variableName)
        : entity_name(std::move(simulatorName))
        , variable_name(std::move(variableName))
    {}

    /// Constructor for function variables.
    full_variable_name(
        std::string functionName,
        std::string ioGroupName,
        int ioGroupInstance,
        std::string ioName,
        int ioInstance)
        : entity_name(std::move(functionName))
        , variable_group_name(std::move(ioGroupName))
        , variable_group_instance(ioGroupInstance)
        , variable_name(std::move(ioName))
        , variable_instance(ioInstance)
    {}

    /// The name of an entity.
    std::string entity_name;

    /**
     *  The name of a variable group (ignored for simulators).
     *
     *  \note
     *      This member is sometimes used to determine whether this
     *      `full_variable_name` refers to a simulator or function variable.
     *      It is considered to refer to a simulator if and only if this
     *      string is empty, and a function otherwise.
     */
    std::string variable_group_name;

    /// The index of a variable group instance (ignored for simulators).
    int variable_group_instance = 0;

    /// The name of a variable.
    std::string variable_name;

    /// The index of a variable instance (ignored for simulators).
    int variable_instance = 0;

    /// Convenience function to check `variable_group_name` for emptiness.
    bool is_simulator_variable() const noexcept
    {
        return variable_group_name.empty();
    }
};


/// Equality operator for `full_variable_name`.
inline bool operator==(
    const full_variable_name& a, const full_variable_name& b) noexcept
{
    return a.entity_name == b.entity_name &&
        a.variable_group_name == b.variable_group_name &&
        a.variable_group_instance == b.variable_group_instance &&
        a.variable_name == b.variable_name &&
        a.variable_instance == b.variable_instance;
}

/// Inequality operator for `full_variable_name`.
inline bool operator!=(
    const full_variable_name& a, const full_variable_name& b) noexcept
{
    return !operator==(a, b);
}

/// Writes a `full_variable_name` to an output stream.
inline std::ostream& operator<<(std::ostream& s, const full_variable_name& v)
{
    s << v.entity_name << ':';
    if (v.is_simulator_variable()) {
        s << v.variable_name;
    } else {
        s << v.variable_group_name << '[' << v.variable_group_instance << "]:"
          << v.variable_name << '[' << v.variable_instance << ']';
    }
    return s;
}

/// Returns a string representation of a `full_variable_name`.
std::string to_text(const full_variable_name& v);

} // namespace cse

namespace std
{
template<>
struct hash<cse::full_variable_name>
{
    std::size_t operator()(const cse::full_variable_name& v) const noexcept
    {
        std::size_t h = 0;
        boost::hash_combine(h, v.entity_name);
        boost::hash_combine(h, v.variable_group_name);
        boost::hash_combine(h, v.variable_group_instance);
        boost::hash_combine(h, v.variable_name);
        boost::hash_combine(h, v.variable_instance);
        return h;
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
     *  An entity may be either a simulator or a function instance;
     *  this is determined by the `type` field.
     */
    struct entity
    {
        /// The entity name.
        std::string name;

        /// The entity type.
        entity_type type;

        /// Recommended step size (for simulators; ignored for functions).
        duration step_size_hint;

        /// Parameter values (for functions; ignored for simulators).
        function_parameter_value_map parameter_values;
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
    void add_entity(
        std::string_view name,
        std::shared_ptr<cse::model> type,
        duration stepSizeHint = duration::zero())
    {
        add_entity({std::string(name), type, stepSizeHint, {}});
    }

    /// \overload
    void add_entity(
        std::string_view name,
        std::shared_ptr<function_type> type,
        function_parameter_value_map parameters)
    {
        add_entity({std::string(name), type, {}, std::move(parameters)});
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
     *  Retrieves the description of a simulator variable, given its
     *  qualified name.
     *
     *  This is a convenience function that provides fast variable lookup
     *  (O(1) on average).
     */
    const variable_description& get_variable_description(
        const full_variable_name& v) const;

    /**
     *  A description of a function variable, including group and variable
     *  indices.
     *
     *  This is just a `cse::function_io_description` bundled together with
     *  the indices of the variable and its group.  (These indices are
     *  implicitly defined by the order of groups and variables in a
     *  `cse::function_description`, and thus not an explicit part of each
     *  `cse::function_io_description`.)
     */
    struct function_io_description
    {
        int group_index = 0;
        int io_index = 0;
        cse::function_io_description description;
    };

    /**
     *  Retrieves the description of a function variable, given its
     *  qualified name.
     *
     *  This is a convenience function that provides fast variable lookup
     *  (O(1) on average).
     */
    const function_io_description& get_function_io_description(
        const full_variable_name& v) const;

private:
    // Entities, indexed by name.
    entity_map entities_;

    // Connections. Target is key, source is value.
    connection_map connections_;

    // Cache for fast lookup of model info, indexed by model UUID.
    struct model_info
    {
        std::unordered_map<std::string, variable_description> variables;
    };
    std::unordered_map<std::string, model_info> modelCache_;

    // Cache for fast lookup of function info.
    struct function_info
    {
        function_description description;
        std::unordered_map<
            std::string,
            std::unordered_map<
                std::string,
                function_io_description>>
            ios;
    };
    std::unordered_map<std::string, function_info> functionCache_;
};


/**
 *  Converts an entity type to a model or function type.
 *
 *  This is a convenience function that simply checks whether `et` contains
 *  a `std::shared_ptr<T>`, and if so, returns it.
 *  Otherwise, it returns an empty (null) `std::shared_ptr<T>`.
 */
template<typename T>
std::shared_ptr<T> entity_type_to(system_structure::entity_type et) noexcept
{
    const auto t = std::get_if<std::shared_ptr<T>>(&et);
    return t ? *t : nullptr;
}


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

/// Overload of `is_valid_connection()` for simulator-function connections.
bool is_valid_connection(
    const variable_description& source,
    const function_io_description& target,
    std::string* reason);

/// Overload of `is_valid_connection()` for function-simulator connections.
bool is_valid_connection(
    const function_io_description& source,
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
 *  Instead, the `add_variable_value()` function may be used as a
 *  convenient method for populating a `variable_value_map` with verified
 *  values.
 */
using variable_value_map = std::unordered_map<full_variable_name, scalar_value>;


/**
 *  Validates a variable value and adds it to a `variable_value_map`.
 *
 *  This is a convenience function which verifies that `variable` refers
 *  to a variable in `systemStructure` and that `value` is a valid value
 *  for that variable, and then adds the variable-value pair to
 *  `variableValues`.
 *
 *  \throws `cse::error` with code `cse::errc::invalid_system_structure`
 *      if any of the above-mentioned checks fail.
 */
void add_variable_value(
    variable_value_map& variableValues,
    const system_structure& systemStructure,
    const full_variable_name& variable,
    scalar_value value);


} // namespace cse
#endif // header guard
