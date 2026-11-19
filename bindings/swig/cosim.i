// =============================================================================
// Language-independent SWIG interface for libcosim (spike).
//
// This file is %included by each language-specific module file
// (python/cosim_python.i, c/cosim_c.i). Those files provide:
//
//   - typemaps for cosim::time_point and cosim::duration (as double seconds),
//   - the %exception handler,
//
// and then %include this file. Everything else is shared.
//
// Spike policy: the libcosim headers are NOT modified. Every adaptation is
// done here and tagged with one of these markers, so we can count how much
// of it should move into libcosim as in-place changes (proposal §6):
//
//   [HIDE]    hides an extension-API or binding-hostile declaration
//   [ADAPT]   %extend / %inline helper that replaces a hostile signature
//   [GLUE]    convenience logic that belongs in libcosim (proposal §6.3)
//   [REDECL]  re-declaration of a type SWIG must know about but that we do
//             not want to %include (opaque base classes)
// =============================================================================

%{
#include <cosim/algorithm/ecco_algorithm.hpp>
#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/exception.hpp>
#include <cosim/execution.hpp>
#include <cosim/lib_info.hpp>
#include <cosim/manipulator/override_manipulator.hpp>
#include <cosim/manipulator/scenario_manager.hpp>
#include <cosim/model_description.hpp>
#include <cosim/observer/file_observer.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/observer/time_series_observer.hpp>
#include <cosim/orchestration.hpp>
#include <cosim/osp_config_parser.hpp>
#include <cosim/ssp/ssp_loader.hpp>
#include <cosim/time.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>
%}

%include <stdint.i>
%include <std_string.i>
%include <std_vector.i>
%include <std_map.i>
%include <std_shared_ptr.i>

// -----------------------------------------------------------------------------
// Make SWIG aware that these std templates exist, so the specialisations in
// libcosim headers parse cleanly (they are ignored anyway).
// -----------------------------------------------------------------------------
namespace std
{
template<class T> struct hash;
template<class T> struct is_error_condition_enum;
}

// C has no operator overloading; other languages map these to equality.
#ifdef SWIGC
%ignore cosim::operator==(const function_io_id&, const function_io_id&);
%rename(equals) cosim::operator==;
%ignore cosim::operator!=;
#endif

// -----------------------------------------------------------------------------
// stdint.i only declares the global ::uint32_t etc. libcosim uses the
// std::-qualified names, which SWIG would otherwise treat as unknown types.
// -----------------------------------------------------------------------------
namespace std
{
typedef ::int32_t int32_t;
typedef ::uint32_t uint32_t;
typedef ::int64_t int64_t;
typedef ::uint64_t uint64_t;
}

// -----------------------------------------------------------------------------
// Plain typedefs used throughout the API
// -----------------------------------------------------------------------------
namespace cosim
{
typedef std::uint32_t value_reference;
typedef int simulator_index;
typedef int function_index;
typedef std::int64_t step_number;
}

// -----------------------------------------------------------------------------
// Container instantiations. Must precede every use, including %extend bodies
// (which SWIG processes where the extended class is declared).
// -----------------------------------------------------------------------------
%template(ValueReferenceVector) std::vector<cosim::value_reference>;
%template(DoubleVector) std::vector<double>;
%template(IntVector) std::vector<int>;
%template(Int64Vector) std::vector<std::int64_t>;
%template(StringVector) std::vector<std::string>;
%template(StringIntMap) std::map<std::string, int>;

// -----------------------------------------------------------------------------
// Constructors: every class exposes exactly ONE constructor, an %extend
// replacement with default arguments. compactdefaultargs makes SWIG emit a
// single wrapper for it (Python: keyword defaults; C: one function, all
// arguments required) instead of one overload per defaulted argument, which
// C would have to name-mangle.
//
// A by-name %ignore hides all original constructors -- and also the %extend
// replacement, which is then re-enabled by signature (more specific wins).
//
// C: the experimental C backend still name-mangles the remaining constructor
// when hidden overloads had default arguments, so C gets no constructors for
// these classes but free factory functions instead (end of this file).
// Both call the same helper in cosim::swig::create.
// -----------------------------------------------------------------------------
%feature("compactdefaultargs");

%{
namespace cosim { namespace swig { namespace create {
inline cosim::fixed_step_algorithm* fixed_step_algorithm(cosim::duration step, unsigned int threads)
{
    return threads == 0 ? new cosim::fixed_step_algorithm(step) : new cosim::fixed_step_algorithm(step, threads);
}
inline cosim::time_series_observer* time_series_observer(size_t bufferSize)
{
    return bufferSize == 0 ? new cosim::time_series_observer() : new cosim::time_series_observer(bufferSize);
}
inline cosim::file_observer* file_observer(const std::string& logDir, const std::string& configPath)
{
    return configPath.empty()
        ? new cosim::file_observer(cosim::filesystem::path(logDir))
        : new cosim::file_observer(cosim::filesystem::path(logDir), cosim::filesystem::path(configPath));
}
}}}
%}

%define COSIM_SINGLE_CONSTRUCTOR(NAME, SIGNATURE)
%ignore cosim::NAME::NAME;
#ifndef SWIGC
%rename("%s") cosim::NAME::NAME SIGNATURE;
#endif
%enddef

// -----------------------------------------------------------------------------
// Ownership: everything handed to or returned from an execution is held by
// shared_ptr. Must come before any declaration of these classes.
// -----------------------------------------------------------------------------
%shared_ptr(cosim::execution)
%shared_ptr(cosim::algorithm)
%shared_ptr(cosim::fixed_step_algorithm)
%shared_ptr(cosim::observer)
%shared_ptr(cosim::last_value_provider)
%shared_ptr(cosim::last_value_observer)
%shared_ptr(cosim::time_series_provider)
%shared_ptr(cosim::time_series_observer)
%shared_ptr(cosim::file_observer)
%shared_ptr(cosim::manipulator)
%shared_ptr(cosim::override_manipulator)
%shared_ptr(cosim::scenario_manager)

// -----------------------------------------------------------------------------
// [REDECL] Opaque base classes. Their headers are the C++ extension API
// (pure virtual interfaces with spans, raw pointers, time points), so we only
// tell SWIG that they exist and how the hierarchy looks, to get upcasts.
// -----------------------------------------------------------------------------
namespace cosim
{
%nodefaultctor algorithm;
%nodefaultdtor algorithm;
class algorithm { };

%nodefaultctor observer;
%nodefaultdtor observer;
class observer { };

%nodefaultctor last_value_provider;
%nodefaultdtor last_value_provider;
class last_value_provider : public observer { };

%nodefaultctor time_series_provider;
%nodefaultdtor time_series_provider;
class time_series_provider : public observer { };

%nodefaultctor manipulator;
%nodefaultdtor manipulator;
class manipulator { };
}

// -----------------------------------------------------------------------------
// [HIDE] Observer and manipulator interface methods. These are overrides of
// the extension API, called by the execution, never by users. Ignoring them
// by name works for every class that %includes them.
// -----------------------------------------------------------------------------
%ignore simulator_added;
%ignore simulator_removed;
%ignore variables_connected;
%ignore variable_disconnected;
%ignore simulation_initialized;
%ignore step_complete;
%ignore simulator_step_complete;
%ignore state_restored;
%ignore step_commencing;

// =============================================================================
// log/simple.hpp — [ADAPT] Boost.Log is inline in the header; expose a plain
// enum-based level setter instead.
// =============================================================================
%{
#include <cosim/log/simple.hpp>
%}
%inline %{
namespace cosim { namespace swig {
enum class log_level { trace, debug, info, warning, error, fatal };
inline void set_log_level(log_level level)
{
    cosim::log::set_global_output_level(static_cast<cosim::log::severity_level>(level));
}
}}
%}

// =============================================================================
// lib_info.hpp
// =============================================================================
%ignore cosim::library_short_name;
%include "cosim/lib_info.hpp"

// =============================================================================
// exception.hpp — only the error code enum is exposed; the exception itself
// is translated by the language-specific %exception handler.
// =============================================================================
%ignore cosim::error;                       // [HIDE] std::error_code based
%ignore cosim::nonfatal_bad_value;          // [HIDE]
%ignore cosim::make_error_code;             // [HIDE] std::error_code
%ignore cosim::make_error_condition;        // [HIDE] std::error_condition
%ignore cosim::error_category;              // [HIDE]
%ignore std::is_error_condition_enum;       // [HIDE] std specialisation
%include "cosim/exception.hpp"

// =============================================================================
// time.hpp — cosim::time_point / cosim::duration are mapped to double seconds
// by the language-specific typemaps. Nothing in this header is wrapped.
// =============================================================================
%ignore cosim::detail::clock;               // [HIDE] std::chrono clock
%ignore cosim::to_duration;
%ignore cosim::to_time_point;
%ignore cosim::to_double_time_point;
%ignore cosim::to_double_duration;
%include "cosim/time.hpp"

// =============================================================================
// model_description.hpp
// =============================================================================
%ignore cosim::scalar_value;                // [HIDE] std::variant
%ignore cosim::scalar_value_view;           // [HIDE] std::variant
%ignore cosim::variable_description::start; // [HIDE] optional<variant>
%ignore cosim::find_variable;               // [HIDE] returns std::optional
%ignore cosim::to_text;                     // [HIDE] returns std::string_view
%ignore cosim::operator<<;
%include "cosim/model_description.hpp"

// [ADAPT] typed access to the optional variant start value
%extend cosim::variable_description {
    bool has_start() const { return $self->start.has_value(); }
    double start_real() const { return std::get<double>($self->start.value()); }
    int start_integer() const { return std::get<int>($self->start.value()); }
    bool start_boolean() const { return std::get<bool>($self->start.value()); }
    std::string start_string() const { return std::get<std::string>($self->start.value()); }
}

%template(VariableDescriptionVector) std::vector<cosim::variable_description>;

// =============================================================================
// execution.hpp
// =============================================================================
%ignore cosim::function_io_id;              // [HIDE] functions not in spike scope
%ignore cosim::entity_index_maps;           // [HIDE] unordered_map; see [GLUE] loader
%ignore cosim::inject_system_structure;     // [HIDE] needs system_structure
%ignore std::hash;                          // [HIDE] std specialisations (boost::hash_combine)

%ignore cosim::execution::execution(execution&&);
%ignore cosim::execution::operator=;
%ignore cosim::execution::add_slave;        // [HIDE] needs cosim::slave (extension API)
%ignore cosim::execution::add_function;     // [HIDE] functions not in spike scope
%ignore cosim::execution::connect_variables(variable_id, function_io_id);
%ignore cosim::execution::connect_variables(function_io_id, variable_id);
%ignore cosim::execution::simulate_until(std::optional<time_point>); // [HIDE] std::optional
%ignore cosim::execution::simulate_until_async; // [HIDE] std::future
%ignore cosim::execution::get_real_time_config;  // [HIDE] struct of std::atomic
%ignore cosim::execution::get_real_time_metrics; // [HIDE] struct of std::atomic
%ignore cosim::execution::export_current_state;  // [HIDE] serialization::node
%ignore cosim::execution::import_state;          // [HIDE] serialization::node

%include "cosim/execution.hpp"

%template(VariableIdVector) std::vector<cosim::variable_id>;

%extend cosim::execution {
    // [ADAPT] replaces simulate_until(std::optional<time_point>)
    bool simulate_until(cosim::time_point targetTime)
    {
        return $self->simulate_until(targetTime);
    }

    // [ADAPT] replaces get_real_time_config() (struct of std::atomic)
    void enable_real_time_simulation() { $self->get_real_time_config()->real_time_simulation = true; }
    void disable_real_time_simulation() { $self->get_real_time_config()->real_time_simulation = false; }
    bool is_real_time_simulation() const { return $self->get_real_time_config()->real_time_simulation; }
    void set_real_time_factor_target(double factor) { $self->get_real_time_config()->real_time_factor_target = factor; }
    double get_real_time_factor_target() const { return $self->get_real_time_config()->real_time_factor_target; }

    // [ADAPT] replaces get_real_time_metrics() (struct of std::atomic)
    double rolling_average_real_time_factor() const { return $self->get_real_time_metrics()->rolling_average_real_time_factor; }
    double total_average_real_time_factor() const { return $self->get_real_time_metrics()->total_average_real_time_factor; }
}

// =============================================================================
// algorithm/fixed_step_algorithm.hpp
// =============================================================================
%ignore cosim::fixed_step_algorithm_params; // [HIDE] contains std::unordered_map
COSIM_SINGLE_CONSTRUCTOR(fixed_step_algorithm, (cosim::duration, unsigned int))
%ignore cosim::fixed_step_algorithm_params; // [HIDE] contains std::unordered_map
%ignore cosim::fixed_step_algorithm::operator=;
// [HIDE] algorithm interface (extension API)
%ignore cosim::fixed_step_algorithm::add_simulator;
%ignore cosim::fixed_step_algorithm::remove_simulator;
%ignore cosim::fixed_step_algorithm::add_function;
%ignore cosim::fixed_step_algorithm::connect_variables;
%ignore cosim::fixed_step_algorithm::disconnect_variable;
%ignore cosim::fixed_step_algorithm::setup;
%ignore cosim::fixed_step_algorithm::initialize;
%ignore cosim::fixed_step_algorithm::do_step;
%ignore cosim::fixed_step_algorithm::set_stepsize_decimation_factor;
%ignore cosim::fixed_step_algorithm::export_current_state;
%ignore cosim::fixed_step_algorithm::import_state;
%include "cosim/algorithm/fixed_step_algorithm.hpp"

%extend cosim::fixed_step_algorithm {
    // [ADAPT] replaces the std::optional<unsigned int> worker-thread argument
    // (0 = let libcosim choose).
    fixed_step_algorithm(cosim::duration baseStepSize, unsigned int workerThreadCount = 0)
    {
        return cosim::swig::create::fixed_step_algorithm(baseStepSize, workerThreadCount);
    }
}

// =============================================================================
// observer/last_value_observer.hpp
// =============================================================================
%ignore cosim::last_value_observer::get_real;    // [HIDE] gsl::span out-params
%ignore cosim::last_value_observer::get_integer; // [HIDE] (the vector versions
%ignore cosim::last_value_observer::get_boolean; // [HIDE]  below use different
%ignore cosim::last_value_observer::get_string;  // [HIDE]  names, so by-name is OK)
%include "cosim/observer/last_value_observer.hpp"


%extend cosim::last_value_observer {
    // [ADAPT] vector-returning replacements for the span-based getters.
    std::vector<double> get_reals(cosim::simulator_index sim, const std::vector<cosim::value_reference>& refs)
    {
        std::vector<double> values(refs.size());
        $self->get_real(sim, refs, values);
        return values;
    }
    std::vector<int> get_integers(cosim::simulator_index sim, const std::vector<cosim::value_reference>& refs)
    {
        std::vector<int> values(refs.size());
        $self->get_integer(sim, refs, values);
        return values;
    }
    // std::vector<bool> is not contiguous, so we cannot hand it to gsl::span.
    std::vector<int> get_booleans(cosim::simulator_index sim, const std::vector<cosim::value_reference>& refs)
    {
        auto values = std::make_unique<bool[]>(refs.size());
        $self->get_boolean(sim, refs, gsl::span<bool>(values.get(), refs.size()));
        return std::vector<int>(values.get(), values.get() + refs.size());
    }
    std::vector<std::string> get_strings(cosim::simulator_index sim, const std::vector<cosim::value_reference>& refs)
    {
        std::vector<std::string> values(refs.size());
        $self->get_string(sim, refs, values);
        return values;
    }
}

// =============================================================================
// observer/time_series_observer.hpp
// =============================================================================
// [ADAPT] result struct replacing three output spans
%inline %{
namespace cosim { namespace swig {
struct real_samples
{
    std::vector<double> values;
    std::vector<std::int64_t> steps;
    std::vector<double> times;
};
struct integer_samples
{
    std::vector<int> values;
    std::vector<std::int64_t> steps;
    std::vector<double> times;
};
}}
%}

// Helper used by the %extend bodies below. %extend code is emitted where the
// class is declared, so helpers must precede the class header %include.
%{
namespace cosim { namespace swig {
template<typename T, typename GetFn>
void fetch_samples(GetFn get, std::size_t maxSamples, std::vector<T>& values,
    std::vector<std::int64_t>& steps, std::vector<double>& times)
{
    values.resize(maxSamples);
    steps.resize(maxSamples);
    std::vector<cosim::time_point> tps(maxSamples);
    const auto n = get(gsl::span<T>(values), gsl::span<cosim::step_number>(steps), gsl::span<cosim::time_point>(tps));
    values.resize(n);
    steps.resize(n);
    times.resize(n);
    for (std::size_t i = 0; i < n; ++i) times[i] = cosim::to_double_time_point(tps[i]);
}
}}
%}

// [HIDE] 3 span out-params. By signature, because the [ADAPT] replacements
// below reuse the names (a by-name %ignore would hide them too).
%ignore cosim::time_series_observer::get_real_samples(simulator_index, value_reference, step_number, gsl::span<double>, gsl::span<step_number>, gsl::span<time_point>);
%ignore cosim::time_series_observer::get_integer_samples(simulator_index, value_reference, step_number, gsl::span<int>, gsl::span<step_number>, gsl::span<time_point>);
%ignore cosim::time_series_observer::get_step_numbers;     // [HIDE] span out-param
%ignore cosim::time_series_observer::get_synchronized_real_series; // [HIDE]
// Python keeps both original constructors (overloads are fine there); C gets
// time_series_observer_create() instead (see COSIM_SINGLE_CONSTRUCTOR).
#ifdef SWIGC
%ignore cosim::time_series_observer::time_series_observer;
#endif
%include "cosim/observer/time_series_observer.hpp"

%extend cosim::time_series_observer {
    // [ADAPT]
    cosim::swig::real_samples get_real_samples(cosim::simulator_index sim,
        cosim::value_reference ref, cosim::step_number fromStep, std::size_t maxSamples)
    {
        cosim::swig::real_samples s;
        cosim::swig::fetch_samples<double>(
            [&](auto v, auto st, auto t) { return $self->get_real_samples(sim, ref, fromStep, v, st, t); },
            maxSamples, s.values, s.steps, s.times);
        return s;
    }
    // [ADAPT]
    cosim::swig::integer_samples get_integer_samples(cosim::simulator_index sim,
        cosim::value_reference ref, cosim::step_number fromStep, std::size_t maxSamples)
    {
        cosim::swig::integer_samples s;
        cosim::swig::fetch_samples<int>(
            [&](auto v, auto st, auto t) { return $self->get_integer_samples(sim, ref, fromStep, v, st, t); },
            maxSamples, s.values, s.steps, s.times);
        return s;
    }
}

// =============================================================================
// observer/file_observer.hpp
// =============================================================================
%ignore cosim::file_observer_config;              // [HIDE] std::optional, filesystem::path
COSIM_SINGLE_CONSTRUCTOR(file_observer, (const std::string&, const std::string&))
%include "cosim/observer/file_observer.hpp"

%extend cosim::file_observer {
    // [ADAPT] string paths instead of cosim::filesystem::path, one
    // constructor instead of two (empty configPath = log everything).
    file_observer(const std::string& logDir, const std::string& configPath = "")
    {
        return cosim::swig::create::file_observer(logDir, configPath);
    }
    std::string log_path() { return $self->get_log_path().string(); }
}

// =============================================================================
// manipulator/override_manipulator.hpp
// =============================================================================
%include "cosim/manipulator/override_manipulator.hpp"

// =============================================================================
// manipulator/scenario_manager.hpp
// =============================================================================
%ignore cosim::scenario_manager::scenario_manager(scenario_manager&&);
%ignore cosim::scenario_manager::operator=;
// [HIDE] scenario struct / filesystem::path (by signature; see above)
%ignore cosim::scenario_manager::load_scenario(const scenario::scenario&, time_point);
%ignore cosim::scenario_manager::load_scenario(const cosim::filesystem::path&, time_point);
%include "cosim/manipulator/scenario_manager.hpp"

%extend cosim::scenario_manager {
    // [ADAPT] string path instead of cosim::filesystem::path
    void load_scenario(const std::string& scenarioFile, cosim::time_point currentTime)
    {
        $self->load_scenario(cosim::filesystem::path(scenarioFile), currentTime);
    }
}

// =============================================================================
// [GLUE] One-call loaders (proposal §6.3). Today this logic is duplicated in
// libcosimc and in every C++ client; it belongs in libcosim.
// =============================================================================

%inline %{
namespace cosim { namespace swig {

class loaded_execution
{
public:
    std::shared_ptr<cosim::execution> execution() const { return execution_; }
    std::map<std::string, int> simulators() const { return simulators_; }
    double start_time() const { return startTime_; }
    bool has_end_time() const { return hasEndTime_; }
    double end_time() const { return endTime_; }

#ifndef SWIG
    std::shared_ptr<cosim::execution> execution_;
    std::map<std::string, int> simulators_;
    double startTime_ = 0.0;
    bool hasEndTime_ = false;
    double endTime_ = 0.0;
#endif
};

#ifndef SWIG
inline loaded_execution make_loaded(
    std::shared_ptr<cosim::execution> exe,
    const cosim::system_structure& sys,
    const cosim::variable_value_map& initialValues,
    cosim::time_point start)
{
    loaded_execution le;
    const auto maps = cosim::inject_system_structure(*exe, sys, initialValues);
    le.execution_ = std::move(exe);
    le.simulators_.insert(maps.simulators.begin(), maps.simulators.end());
    le.startTime_ = cosim::to_double_time_point(start);
    return le;
}
#endif

/// Loads an OSP system structure file and returns a ready-to-run execution.
inline loaded_execution load_osp_config(const std::string& configPath)
{
    auto resolver = cosim::default_model_uri_resolver();
    const auto config = cosim::load_osp_config(cosim::filesystem::path(configPath), *resolver);
    std::shared_ptr<cosim::algorithm> algo = std::visit(
        [](const auto& params) -> std::shared_ptr<cosim::algorithm> {
            using P = std::decay_t<decltype(params)>;
            if constexpr (std::is_same_v<P, cosim::fixed_step_algorithm_params>) {
                return std::make_shared<cosim::fixed_step_algorithm>(params);
            } else {
                return std::make_shared<cosim::ecco_algorithm>(params);
            }
        },
        config.algorithm_configuration);
    auto exe = std::make_shared<cosim::execution>(config.start_time, algo);
    auto le = make_loaded(exe, config.system_structure, config.initial_values, config.start_time);
    if (config.end_time) {
        le.hasEndTime_ = true;
        le.endTime_ = cosim::to_double_time_point(*config.end_time);
    }
    return le;
}

/// Loads an SSP archive or directory using its own algorithm.
inline loaded_execution load_ssp(const std::string& configPath)
{
    cosim::ssp_loader loader;
    const auto config = loader.load(cosim::filesystem::path(configPath));
    auto exe = std::make_shared<cosim::execution>(config.start_time, config.algorithm);
    return make_loaded(exe, config.system_structure, config.parameter_sets.at(""), config.start_time);
}

}} // namespace cosim::swig
%}

// =============================================================================
// C only: factory functions replacing constructors (see COSIM_SINGLE_CONSTRUCTOR)
// =============================================================================
#ifdef SWIGC
%inline %{
namespace cosim { namespace swig {
inline std::shared_ptr<cosim::fixed_step_algorithm> fixed_step_algorithm_create(cosim::duration baseStepSize, unsigned int workerThreadCount)
{
    return std::shared_ptr<cosim::fixed_step_algorithm>(create::fixed_step_algorithm(baseStepSize, workerThreadCount));
}
inline std::shared_ptr<cosim::time_series_observer> time_series_observer_create(size_t bufferSize)
{
    return std::shared_ptr<cosim::time_series_observer>(create::time_series_observer(bufferSize));
}
inline std::shared_ptr<cosim::file_observer> file_observer_create(const std::string& logDir, const std::string& configPath)
{
    return std::shared_ptr<cosim::file_observer>(create::file_observer(logDir, configPath));
}
}}
%}
#endif
