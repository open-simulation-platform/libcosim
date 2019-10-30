#ifndef CSE_SYSTEM_STRUCTURE_HPP
#define CSE_SYSTEM_STRUCTURE_HPP

#include <cse/model.hpp>
#include <cse/orchestration.hpp>

#include <boost/functional/hash.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/range/adaptor/transformed.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>


namespace cse
{

/**
 *  The qualified name of a variable, consisting of the simulator name and
 *  the variable name.
 *
 *  The validity of the qualified name can only be determined in the context
 *  of a specific system structure (see `system_structure`).
 */
struct variable_qname
{
    /// The name of a simulator.
    std::string simulator_name;

    /// The name of a variable.
    std::string variable_name;
};


inline bool operator==(const variable_qname& a, const variable_qname& b) noexcept
{
    return a.simulator_name == b.simulator_name &&
        a.variable_name == b.variable_name;
}

inline bool operator!=(const variable_qname& a, const variable_qname& b) noexcept
{
    return !operator==(a, b);
}

} // namespace cse

namespace std
{
template<>
struct hash<cse::variable_qname>
{
    std::size_t operator()(const cse::variable_qname& vqn) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, vqn.simulator_name);
        boost::hash_combine(seed, vqn.variable_name);
        return seed;
    }
};
} // namespace std


namespace cse
{

/**
 *  A description of the structure of a modelled system.
 *
 *  The system structure description contains the list of simulators in the
 *  system and the connections between them.  Validation is performed on
 *  the fly by the class' mutators, and any attempt to make an invalid change
 *  will result in an exception of type `cse::error` with error code
 *  `cse::errc::invalid_system_structure`.
 */
class system_structure
{
public:
    /// Information about a simulator.
    struct simulator
    {
        /// The simulator name.
        std::string name;

        /// The model on which the simulator is based.
        std::shared_ptr<cse::model> model;
    };

    /// Information about a scalar connection.
    struct scalar_connection
    {
        /// The source variable.
        variable_qname source;

        /// The target variable.
        variable_qname target;
    };

private:
    using simulator_map = std::unordered_map<std::string, simulator>;
    using scalar_connection_map =
        std::unordered_map<variable_qname, variable_qname>;
    using scalar_connection_transform =
        scalar_connection (*)(const scalar_connection_map::value_type&);

public:
    using simulator_range = boost::select_second_const_range<simulator_map>;
    using scalar_connection_range =
        boost::transformed_range<
            scalar_connection_transform, const scalar_connection_map>;

    /**
     *  Adds a simulator to the system.
     *
     *  `s.name` must be unique in the context of the present system.
     */
    void add_simulator(const simulator& s);

    /// \overload
    void add_simulator(std::string_view name, std::shared_ptr<cse::model> model)
    {
        add_simulator({std::string(name), model});
    }

    /**
     *  Returns a list of the simulators in the system.
     *
     *  \returns
     *      A range of `simulator` objects.
     */
    simulator_range simulators() const noexcept;

    /**
     *  Establishes a connection between two scalar variables.
     *
     *  The same target variable may not be connected several times.
     */
    void add_scalar_connection(const scalar_connection& c);

    /// \overload
    void add_scalar_connection(
        const variable_qname& source, const variable_qname& target)
    {
        add_scalar_connection({source, target});
    }

    /**
     *  Returns a list of the scalar connections in the system.
     *
     *  \returns
     *      A range of `scalar_connection` objects.
     */
    scalar_connection_range scalar_connections() const noexcept;

    /**
     *  Retrieves the description of a variable, given its qualified name.
     *
     *  This is a convenience function that provides fast variable lookup
     *  (O(1) on average).
     */
    const variable_description& get_variable_description(
        const variable_qname& v) const;

private:
    // Simulators, indexed by name.
    simulator_map simulators_;

    // Scalar connections. Target is key, source is value.
    scalar_connection_map scalarConnections_;

    // Cache for fast lookup of model info, indexed by model UUID.
    struct model_info
    {
        std::unordered_map<std::string, variable_description> variables;
    };
    std::unordered_map<std::string, model_info> modelCache_;
};


/**
 *  Checks whether `name` is a valid simulator name.
 *
 *  The rules are the same as for C(++) identifiers:  The name may only
 *  consist of ASCII letters, numbers and underscores, and the first
 *  character must be a letter or an underscore.
 *
 *  If `reason` is non-null and the function returns `false`, a
 *  human-readable reason will be stored in the string pointed to by
 *  `reason`.
 */
bool is_valid_simulator_name(std::string_view name, std::string* reason) noexcept;


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
 *  of type `variable_qname`, to scalar values of type `scalar_value`.
 *  Being a plain map (`std::unordered_map`, to be precise) it is not
 *  linked to a particular `system_structure` instance, and does not
 *  perform any validation of variable names or values.
 *
 *  Instead, the `add_parameter_value()` function may be used as a
 *  convenient method for populating a `parameter_set` with verified
 *  values.
 */
using parameter_set = std::unordered_map<variable_qname, scalar_value>;


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
    variable_qname variable,
    scalar_value value);


} // namespace cse
#endif // header guard
