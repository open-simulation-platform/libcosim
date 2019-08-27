/**
 *  \file
 *  \brief Types and functions for running an execution.
 */
#ifndef CSE_EXECUTION_HPP
#define CSE_EXECUTION_HPP

#include <cse/async_slave.hpp>
#include <cse/model.hpp>

#include <boost/fiber/future.hpp>
#include <boost/functional/hash.hpp>

#include <memory>
#include <optional>
#include <sstream>


namespace cse
{

/// An index which identifies a sub-simulator in an execution.
using simulator_index = int;

/// An number which identifies a specific time step in an execution.
using step_number = long long;

/// An object which uniquely identifies a variable in a simulation.
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
inline bool operator==(const variable_id& a, const variable_id& b) noexcept
{
    return a.simulator == b.simulator && a.type == b.type && a.reference == b.reference;
}

/// Inequality operator for `variable_id`.
inline bool operator!=(const variable_id& a, const variable_id& b) noexcept
{
    return !operator==(a, b);
}

/// Writes a textual representation of `v` to `stream`.
inline std::ostream& operator<<(std::ostream& stream, variable_id v)
{
    return stream << "(simulator " << v.simulator
                  << ", type " << v.type
                  << ", variable " << v.reference << ")";
}

} // namespace cse

// Specialisation of std::hash for variable_id
namespace std
{
template<>
class hash<cse::variable_id>
{
public:
    std::size_t operator()(const cse::variable_id& v) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, v.simulator);
        boost::hash_combine(seed, v.type);
        boost::hash_combine(seed, v.reference);
        return seed;
    }
};
} // namespace std

namespace cse
{


// Forward declarations
class algorithm;
class observer;
class manipulator;
class simulator;
class connection;


/**
 *  A class which represents an execution, i.e., a co-simulation run.
 *
 *  The `execution` class manages all the entities involved in an execution
 *  and provides a high-level API for driving the co-simulation algorithm
 *  forward.
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
     *      An object that provides asynchronous communication with the slave.
     *  \param name
     *      An execution-specific name for the slave.
     */
    simulator_index add_slave(
        std::shared_ptr<async_slave> slave,
        std::string_view name);

    /// Adds an observer to the execution.
    void add_observer(std::shared_ptr<observer> obs);

    /// Adds a manipulator to the execution.
    void add_manipulator(std::shared_ptr<manipulator> man);

    /**
     *  Adds a connection to the execution.
     *
     *  After this, the values of the connection's source variables will be
     *  passed to the connection object, and from there to the connection's
     *  destination variables at the co-simulation algorithm's discretion.
     *  Different algorithms may handle this in different ways, and could for
     *  instance choose to extrapolate or correct the variable value during
     *  transfer.
     *
     *  When calling this method, the validity of both variables are checked
     *  against the metadata of their respective `simulator`s. If either is
     *  found to be invalid (i.e. not found, wrong type or causality, an
     *  exception will be thrown. If one of the connection's destination
     *  variables is already connected, an exception will be thrown.
     */
    void add_connection(std::shared_ptr<connection> conn);

    /**
     * Convenience method for removing a connection from the execution.
     *
     * Searches for a connection containing a destination variable which
     * matches the `destination` argument and then removes it. Throws an
     * exception if no such connection is found.
     *
     * @param destination
     *      Any of the connection's destination variables.
     */
    void remove_connection(variable_id destination);

    /// Returns all variable connections in the execution.
    const std::vector<std::shared_ptr<connection>>& get_connections();


    /// Returns the current logical time.
    time_point current_time() const noexcept;

    /**
     *  Advance the co-simulation forward to the given logical time.
     *
     *  This function returns immediately, and its actions will be performed
     *  asynchronously.  As it is not possible to perform more than one
     *  asynchronous operation at a time per `execution` object, client code
     *  must verify that the operation completed before calling the function
     *  again (e.g. by calling `boost::fibers::future::get()` on the result).
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
     */
    boost::fibers::future<bool> simulate_until(std::optional<time_point> targetTime);

    /**
     *  Advance the co-simulation forward one single step
     *
     *  \returns
     *      The actual duration of the step.
     *      `current_time()` may be called to determine the actual time after
     *      the step completed.
     */
    duration step();

    /// Stops the co-simulation temporarily.
    void stop_simulation();

    /// Is the simulation loop currently running
    bool is_running() const noexcept;

    /// Enables real time simulation
    void enable_real_time_simulation();

    /// Disables real time simulation
    void disable_real_time_simulation();

    /// Sets the custom real time factor for the simulation
    void set_real_time_factor_target(double realTimeFactor);

    /// Returns if this is a real time simulation
    bool is_real_time_simulation();

    /// Returns the current real time factor
    double get_measured_real_time_factor();

    /// Returns the current real time factor target
    double get_real_time_factor_target();

    /// Returns a map of currently modified variables
    std::vector<variable_id> get_modified_variables();

    /// Set initial value for a variable of type real. Must be called before simulation is started.
    void set_real_initial_value(simulator_index sim, value_reference var, double value);

    /// Set initial value for a variable of type integer. Must be called before simulation is started.
    void set_integer_initial_value(simulator_index sim, value_reference var, int value);

    /// Set initial value for a variable of type boolean. Must be called before simulation is started.
    void set_boolean_initial_value(simulator_index sim, value_reference var, bool value);

    /// Set initial value for a variable of type string. Must be called before simulation is started.
    void set_string_initial_value(simulator_index sim, value_reference var, const std::string& value);


private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};


} // namespace cse
#endif // header guard
