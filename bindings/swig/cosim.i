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
// cosim::filesystem::path is mapped to strings by language-specific typemaps.
// -----------------------------------------------------------------------------
namespace cosim { namespace filesystem { class path; } }

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
%template(BoolVector) std::vector<bool>;
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
%shared_ptr(cosim::model_uri_resolver)

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

%nodefaultctor model_uri_resolver;
%nodefaultdtor model_uri_resolver;
class model_uri_resolver { };
}

// (Observer, manipulator and algorithm interface overrides are private in the
// concrete classes, so SWIG does not see them.)

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
%ignore cosim::entity_index_maps;           // [HIDE] unordered_map; see configured_execution
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
%ignore cosim::execution::get_real_time_config;  // [HIDE] struct of std::atomic (plain accessors exist)
%ignore cosim::execution::get_real_time_metrics; // [HIDE] struct of std::atomic (plain accessors exist)
%ignore cosim::execution::export_current_state;  // [HIDE] serialization::node
%ignore cosim::execution::import_state;          // [HIDE] serialization::node

// [ADAPT] configured_execution::entity_maps holds std::unordered_maps (no C
// support) and end_time is a std::optional.
%immutable cosim::configured_execution::execution;
%ignore cosim::configured_execution::entity_maps;
%ignore cosim::configured_execution::end_time;
%extend cosim::configured_execution {
    std::map<std::string, int> simulators() const
    {
        return std::map<std::string, int>($self->entity_maps.simulators.begin(), $self->entity_maps.simulators.end());
    }
    std::map<std::string, int> functions() const
    {
        return std::map<std::string, int>($self->entity_maps.functions.begin(), $self->entity_maps.functions.end());
    }
    bool has_end_time() const { return $self->end_time.has_value(); }
    double end_time_seconds() const { return cosim::to_double_time_point($self->end_time.value()); }
}

%include "cosim/execution.hpp"

%template(VariableIdVector) std::vector<cosim::variable_id>;

%extend cosim::execution {
    // [ADAPT] replaces simulate_until(std::optional<time_point>)
    bool simulate_until(cosim::time_point targetTime)
    {
        return $self->simulate_until(targetTime);
    }
}

// =============================================================================
// algorithm/fixed_step_algorithm.hpp
// =============================================================================
COSIM_SINGLE_CONSTRUCTOR(fixed_step_algorithm, (cosim::duration, unsigned int))
%ignore cosim::fixed_step_algorithm_params; // [HIDE] contains std::unordered_map
%ignore cosim::fixed_step_algorithm::operator=;
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
// [HIDE] span-based overloads; the native std::vector overloads are wrapped.
%ignore cosim::last_value_observer::get_real(simulator_index, gsl::span<const value_reference>, gsl::span<double>);
%ignore cosim::last_value_observer::get_integer(simulator_index, gsl::span<const value_reference>, gsl::span<int>);
%ignore cosim::last_value_observer::get_boolean(simulator_index, gsl::span<const value_reference>, gsl::span<bool>);
%ignore cosim::last_value_observer::get_string(simulator_index, gsl::span<const value_reference>, gsl::span<std::string>);
%include "cosim/observer/last_value_observer.hpp"

// =============================================================================
// observer/time_series_observer.hpp
// =============================================================================
// [HIDE] span-based overloads; the native overloads returning
// time_series_samples<T> are wrapped.
%ignore cosim::time_series_observer::get_real_samples(simulator_index, value_reference, step_number, gsl::span<double>, gsl::span<step_number>, gsl::span<time_point>);
%ignore cosim::time_series_observer::get_integer_samples(simulator_index, value_reference, step_number, gsl::span<int>, gsl::span<step_number>, gsl::span<time_point>);
%ignore cosim::time_series_observer::get_step_numbers;     // [HIDE] span out-param
%ignore cosim::time_series_observer::get_synchronized_real_series; // [HIDE]
// Python keeps both original constructors (overloads are fine there); C gets
// time_series_observer_create() instead (see COSIM_SINGLE_CONSTRUCTOR).
#ifdef SWIGC
%ignore cosim::time_series_observer::time_series_observer;
#endif
// [ADAPT] `times` is a std::vector<time_point>; element types inside
// containers do not go through the time_point typemaps, so expose seconds.
%ignore cosim::time_series_samples::times;
%include "cosim/observer/time_series_observer.hpp"

%extend cosim::time_series_samples {
    std::vector<double> times_seconds() const
    {
        std::vector<double> t;
        t.reserve($self->times.size());
        for (const auto& tp : $self->times) t.push_back(cosim::to_double_time_point(tp));
        return t;
    }
}
%template(RealSamples) cosim::time_series_samples<double>;
%template(IntegerSamples) cosim::time_series_samples<int>;

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
%ignore cosim::scenario_manager::load_scenario(const scenario::scenario&, time_point); // [HIDE] in-memory scenario
%include "cosim/manipulator/scenario_manager.hpp"

// =============================================================================
// Loaders: osp_config_parser.hpp, ssp/ssp_loader.hpp
// load_execution_from_osp_config() and load_execution_from_ssp() are native.
// =============================================================================
%ignore cosim::osp_config;                  // [HIDE] system_structure, std::variant
%ignore cosim::load_osp_config;             // [HIDE] lower-level; use load_execution_from_osp_config
%ignore cosim::ssp_configuration;           // [HIDE] system_structure, unordered_map
%ignore cosim::ssp_loader;                  // [HIDE] lower-level; use load_execution_from_ssp
%include "cosim/osp_config_parser.hpp"
%include "cosim/ssp/ssp_loader.hpp"

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
