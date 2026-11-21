/**
 *  \file
 *  Types and functions for running an execution.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_EXECUTION_HPP
#define COSIM_EXECUTION_HPP

#include <cosim/function/function.hpp>
#include <cosim/model_description.hpp>
#include <cosim/slave.hpp>
#include <cosim/system_structure.hpp>
#include <cosim/time.hpp>
#include <cosim/timer.hpp>

#include <boost/functional/hash.hpp>

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <sstream>
#include <unordered_map>


namespace cosim
{

/// An index which identifies a sub-simulator in an execution.
using simulator_index = int;

/// An index which identifies a function in an execution.
using function_index = int;

/// An number which identifies a specific time step in an execution.
using step_number = std::int64_t;

/// An object which uniquely identifies a simulator variable in a simulation.
struct variable_id
{
    /// The simulator that owns the variable.
    simulator_index simulator;

    /// The variable data type.
    variable_type type;

    /// The variable value reference.
    value_reference reference;
};

/// Equality operator for `variable_id`.
inline bool operator==(const variable_id& lhs, const variable_id& rhs) noexcept
{
    return lhs.simulator == rhs.simulator && lhs.type == rhs.type && lhs.reference == rhs.reference;
}

/// Inequality operator for `variable_id`.
inline bool operator!=(const variable_id& lhs, const variable_id& rhs) noexcept
{
    return !operator==(lhs, rhs);
}

/// An object which uniquely identifies a function variable in a simulation.
struct function_io_id
{
    function_index function;
    variable_type type;
    function_io_reference reference;
};

/// Equality operator for `function_io_id`.
inline bool operator==(const function_io_id& lhs, const function_io_id& rhs) noexcept
{
    return lhs.function == rhs.function && lhs.type == rhs.type && lhs.reference == rhs.reference;
}

/// Inequality operator for `function_io_id`.
inline bool operator!=(const function_io_id& lhs, const function_io_id& rhs) noexcept
{
    return !operator==(lhs, rhs);
}

/// Writes a textual representation of `variable` to `stream`.
inline std::ostream& operator<<(std::ostream& stream, variable_id variable)
{
    return stream << "(simulator " << variable.simulator
                  << ", type " << variable.type
                  << ", variable " << variable.reference << ")";
}

} // namespace cosim

// Specialisations of std::hash for variable_id and function_io_id
namespace std
{
template<>
class hash<cosim::variable_id>
{
public:
    std::size_t operator()(const cosim::variable_id& variable) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, variable.simulator);
        boost::hash_combine(seed, variable.type);
        boost::hash_combine(seed, variable.reference);
        return seed;
    }
};

template<>
class hash<cosim::function_io_id>
{
public:
    std::size_t operator()(const cosim::function_io_id& variable) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, variable.function);
        boost::hash_combine(seed, variable.type);
        boost::hash_combine(seed, variable.reference.group);
        boost::hash_combine(seed, variable.reference.group_instance);
        boost::hash_combine(seed, variable.reference.io);
        boost::hash_combine(seed, variable.reference.io_instance);
        return seed;
    }
};
} // namespace std

namespace cosim
{


// Forward declarations
class algorithm;
class observer;
class manipulator;
class simulator;


/**
 *  A class which represents an execution, i.e., a co-simulation run.
 *
 *  The `execution` class manages all the entities involved in an execution
 *  and provides a high-level API for driving the co-simulation algorithm
 *  forward.
 *
 *  Connected variables must have matching types. Enumeration and Integer
 *  are distinct types, even when assigning an Integer to a scalar
 *  Enumeration variable is supported.
 *
 *  \warning
 *      In general, the member functions of this class are not exception safe.
 *      This means that if any of them throw an exception, one must assume that
 *      the `execution` object is in an invalid state and can no longer be used.
 *      The same holds for its associated algorithm and any simulators or
 *      functions that are part of the execution.
 */
class execution
{
public:
    /**
     *  Constructor.
     *
     *  \param startTime
     *      The logical time at which the simulation will start.
     *  \param algo
     *      The co-simulation algorithm which will be used. One `algorithm`
     *      object may only be used with one `execution`.
     */
    execution(time_point startTime, std::shared_ptr<algorithm> algo);

    ~execution() noexcept;

    execution(const execution&) = delete;
    execution& operator=(const execution&) = delete;

    execution(execution&&) noexcept;
    execution& operator=(execution&&) noexcept;

    /**
     *  Adds a slave to the execution.
     *
     *  \param slave
     *      The slave object.
     *  \param name
     *      An execution-specific name for the slave.
     *  \param stepSizeHint
     *      The recommended co-simulation step size for this slave.
     *      Whether and how this is taken into account is algorithm dependent.
     *      If zero, the algorithm will attempt to choose a sensible default.
     *
     *  \pre `initialize()` has not been called.
     *  \throws std::logic_error if the execution is already initialized.
     */
    simulator_index add_slave(
        std::shared_ptr<slave> slave,
        std::string_view name,
        duration stepSizeHint = duration::zero());

    /**
     *  Returns the slave owned by this execution, including a slave added by
     *  `inject_system_structure()`. The returned pointer extends its lifetime.
     *  Configure instance-specific options before initialization. Do not
     *  independently drive the slave lifecycle or change values behind the
     *  execution's cached simulator interface.
     *
     *  \throws std::out_of_range if the simulator index is invalid.
     */
    std::shared_ptr<slave> get_slave(simulator_index index) const; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

    /**
     *  Adds a function to the execution.
     *
     *  \pre `initialize()` has not been called.
     *  \throws std::logic_error if the execution is already initialized.
     */
    function_index add_function(std::shared_ptr<function> fun);

    /// Adds an observer to the execution.
    void add_observer(std::shared_ptr<observer> obs);

    /// Adds a manipulator to the execution.
    void add_manipulator(std::shared_ptr<manipulator> man);

    /**
     *  Connects a simulator output variable to a simulator input variable.
     *
     *  After this, the values of the output variable will be passed to the
     *  input value at the co-simulation algorithm's discretion.  Different
     *  algorithms may handle this in different ways, and could for instance
     *  choose to extrapolate or correct the variable value during transfer.
     *
     *  When calling this method, the validity of both variables are checked
     *  against the metadata of their respective `simulator`s. If either is
     *  found to be invalid (i.e. not found, wrong type or causality, an
     *  exception will be thrown.
     */
    void connect_variables(variable_id output, variable_id input);

    /**
     *  Connects a simulator output variable to a function input variable.
     *
     *  After this, the values of the output variable will be passed to the
     *  input value at the co-simulation algorithm's discretion.  Different
     *  algorithms may handle this in different ways, and could for instance
     *  choose to extrapolate or correct the variable value during transfer.
     *
     *  When calling this method, the validity of both variables are checked
     *  against the metadata of their respective `simulator`s. If either is
     *  found to be invalid (i.e. not found, wrong type or causality, an
     *  exception will be thrown.
     */
    void connect_variables(variable_id output, function_io_id input);

    /**
     *  Connects a function output variable to a simulator input variable.
     *
     *  After this, the values of the output variable will be passed to the
     *  input value at the co-simulation algorithm's discretion.  Different
     *  algorithms may handle this in different ways, and could for instance
     *  choose to extrapolate or correct the variable value during transfer.
     *
     *  When calling this method, the validity of both variables are checked
     *  against the metadata of their respective `simulator`s. If either is
     *  found to be invalid (i.e. not found, wrong type or causality, an
     *  exception will be thrown.
     */
    void connect_variables(function_io_id output, variable_id input);

    /// Connects a function output variable to a function input variable.
    void connect_variables(function_io_id output, function_io_id input);

    /// Returns the current logical time.
    time_point current_time() const noexcept; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

    /**
     *  Initialize the co-simulation (in an algorithm-dependent manner).
     *
     *  After this function is called, it is no longer possible to add more
     *  subsimulators or functions.
     */
    void initialize();

    /// Returns whether initialization completed successfully.
    bool is_initialized() const noexcept; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

    /**
     *  Advance the co-simulation forward to the given logical time (blocks the current thread).
     *
     *  \param targetTime
     *      The logical time at which the co-simulation should pause (optional).
     *      If specified, this must always be greater than the value of
     *      `current_time()` at the moment the function is called. If not specified,
     *      the co-simulation will continue until `stop_simulation()` on another thread is called.
     *
     *  \returns
     *      `true` if the co-simulation was advanced to the given time,
     *      or `false` if it was stopped before this. In the latter case,
     *      `current_time()` may be called to determine the actual end time.
     *
     *  \note
     *      For backwards compatibility, this function automatically calls
     *      `initialize()` if this hasn't already been done. However, new code
     *      should always call `initialize()` before any of the
     *      simulation/stepping functions.
     */
    bool simulate_until(std::optional<time_point> targetTime);

    /**
     *  Asynchronously advance the co-simulation forward to the given logical time.
     *
     *  \param targetTime
     *      The logical time at which the co-simulation should pause (optional).
     *      If specified, this must always be greater than the value of
     *      `current_time()` at the moment the function is called. If not specified,
     *      the co-simulation will continue until `stop_simulation()` is called.
     *
     *  \returns
     *      `true` if the co-simulation was advanced to the given time,
     *      or `false` if it was stopped before this. In the latter case,
     *      `current_time()` may be called to determine the actual end time.
     *
     *  \note
     *      For backwards compatibility, this function automatically calls
     *      `initialize()` if this hasn't already been done. However, new code
     *      should always call `initialize()` before any of the
     *      simulation/stepping functions.
     *      The execution must remain alive until the returned future finishes.
     *      A stop requested after this function returns is not lost during
     *      worker startup.
     */
    std::future<bool> simulate_until_async(std::optional<time_point> targetTime);

    /**
     *  Advance the co-simulation forward one single step
     *
     *  \returns
     *      The actual duration of the step.
     *      `current_time()` may be called to determine the actual time after
     *      the step completed.
     *
     *  \note
     *      For backwards compatibility, this function automatically calls
     *      `initialize()` if this hasn't already been done. However, new code
     *      should always call `initialize()` before any of the
     *      simulation/stepping functions.
     */
    duration step();

    /// Stops the co-simulation temporarily (thread-safe operation).
    void stop_simulation();

    /// True until the simulation loop has finished, including after a stop request.
    bool is_running() const noexcept; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

    /// Returns a pointer to an object containing real time configuration
    const std::shared_ptr<real_time_config> get_real_time_config() const; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

    /// Returns a pointer to an object containing real time metrics
    std::shared_ptr<const real_time_metrics> get_real_time_metrics() const; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

    /// Returns the model description for a simulator with the given index
    model_description get_model_description(simulator_index index) const; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

    /// Returns a map of currently modified variables
    std::vector<variable_id> get_modified_variables() const; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

    /// Returns the algorithm used in this execution
    std::shared_ptr<cosim::algorithm> get_algorithm() const; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

    /// Set initial value for a variable of type real. Must be called before simulation is started.
    void set_real_initial_value(simulator_index sim, value_reference var, double value);

    /// Set initial value for a variable of type integer. Must be called before simulation is started.
    void set_integer_initial_value(simulator_index sim, value_reference var, int value);

    /// Set initial value for a variable of type boolean. Must be called before simulation is started.
    void set_boolean_initial_value(simulator_index sim, value_reference var, bool value);

    /// Set initial value for a variable of type string. Must be called before simulation is started.
    void set_string_initial_value(simulator_index sim, value_reference var, const std::string& value);

    /// Set an initial whole-variable value before simulation is started.
    void set_variable_initial_value(variable_id variable, const variable_value& value);

    /**
     *  Exports the current state of the co-simulation.
     *
     *  \pre `initialize()` has been called.
     *  \pre `!is_running()`
     *  \throws std::logic_error if either precondition is not met.
     */
    serialization::node export_current_state() const; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

    /**
     *  Imports a previously-exported co-simulation state.
     *
     *  Note that the data returned by `export_current_state()` only describe
     *  the *state* of the system, not its structure.  This means that it's the
     *  caller's responsibility to either
     *
     *  1. not modify the system structure between state export and state import
     *  2. restore the pre-export system structure prior to state import
     *
     *  \pre `initialize()` has been called.
     *  \pre `!is_running()`
     *  \throws std::logic_error if either precondition is not met.
     */
    void import_state(const serialization::node& exportedState);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};


/// Maps entity names to simulator/function indices in an `execution`.
struct entity_index_maps // NOLINT(bugprone-exception-escape): MSVC unordered-container moves may allocate.
{
    /// Mapping of simulator names to simulator indices.
    std::unordered_map<std::string, simulator_index> simulators;

    /// Mapping of function names to function indices.
    std::unordered_map<std::string, function_index> functions;
};

/**
 * Creates a slave for a named system entity during structure injection.
 * The hook may call `model::instantiate()` for entities that need no
 * customization. It must return a non-null, not-yet-initialized slave.
 */
using model_instantiator = std::function<std::shared_ptr<slave>(
    const std::shared_ptr<model>&, std::string_view)>;


/**
 *  Adds simulators and connections to an execution, and sets initial values,
 *  according to a predefined system structure description.
 *
 *  This function may be called multiple times for the same `execution`, as
 *  long as there is no conflict between the different `system_structure`
 *  objects.
 *
 *  \returns
 *      Mappings between entity names and their indexes in the execution.
 */
entity_index_maps inject_system_structure(
    execution& exe,
    const system_structure& sys,
    const variable_value_map& initialValues);

/**
 * Injects a structure using a custom model instantiation hook.
 *
 * The hook is called once per simulator entity, before that slave is added;
 * an empty hook uses `model::instantiate()`. Functions, connections and
 * initial values are handled as in the three-argument overload. As with that
 * overload, an exception can leave the execution partially modified.
 *
 * \throws std::invalid_argument if the hook returns a null slave.
 */
entity_index_maps inject_system_structure(
    execution& exe,
    const system_structure& sys,
    const variable_value_map& initialValues,
    const model_instantiator& instantiate);

} // namespace cosim
#endif // header guard
