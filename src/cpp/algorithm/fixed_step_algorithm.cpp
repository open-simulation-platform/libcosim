#if defined(WIN32) && !defined(NOMINMAX)
#    define NOMINMAX
#endif
#include "cse/algorithm/fixed_step_algorithm.hpp"

#include "cse/error.hpp"
#include "cse/exception.hpp"
#include "cse/log/logger.hpp"

#include <algorithm>
#include <cstdlib>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <vector>


namespace cse
{
namespace
{

std::string exception_ptr_msg(std::exception_ptr ep)
{
    assert(ep);
    try {
        std::rethrow_exception(ep);
    } catch (const std::exception& e) {
        return std::string(e.what());
    } catch (...) {
        return "Unknown error";
    }
}

int calculate_decimation_factor(
    std::string_view name,
    duration baseStepSize,
    duration simulatorStepSize)
{
    if (simulatorStepSize == duration::zero()) return 1;
    const auto result = std::div(simulatorStepSize.count(), baseStepSize.count());
    const int factor = std::max<int>(1, static_cast<int>(result.quot));
    if (result.rem > 0 || result.quot < 1) {
        duration actualStepSize = baseStepSize * factor;
        const auto startTime = time_point();
        BOOST_LOG_SEV(log::logger(), log::warning)
            << "Effective step size for " << name
            << " will be " << to_double_duration(actualStepSize, startTime) << " s"
            << " instead of configured value "
            << to_double_duration(simulatorStepSize, startTime) << " s";
    }
    return factor;
}

} // namespace


class fixed_step_algorithm::impl
{
public:
    explicit impl(duration baseStepSize)
        : baseStepSize_(baseStepSize)
    {
        CSE_INPUT_CHECK(baseStepSize.count() > 0);
    }

    ~impl() noexcept = default;

    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;

    impl(impl&&) = delete;
    impl& operator=(impl&&) = delete;

    void add_simulator(simulator_index i, simulator* s, duration stepSizeHint)
    {
        assert(simulators_.count(i) == 0);
        simulators_[i].sim = s;
        simulators_[i].decimationFactor =
            calculate_decimation_factor(s->name(), baseStepSize_, stepSizeHint);
    }

    void remove_simulator(simulator_index i)
    {
        simulators_.erase(i);
        disconnect_simulator_variables(i);
    }

    void add_function(function_index i, function* f)
    {
        assert(functions_.count(i) == 0);
        functions_[i].fun = f;
    }

    void connect_variables(
        variable_id output,
        variable_id input,
        bool inputAlreadyConnected)
    {
        if (inputAlreadyConnected) disconnect_variable(input);
        auto& sourceSimInfo = simulators_.at(output.simulator);
        auto& targetSimInfo = simulators_.at(input.simulator);
        sourceSimInfo.sim->expose_for_getting(output.type, output.reference);
        targetSimInfo.sim->expose_for_setting(input.type, input.reference);
        sourceSimInfo.outgoingSimConnections.push_back({output, input});
    }

    void connect_variables(
        variable_id output,
        function_io_id input,
        bool inputAlreadyConnected)
    {
        if (inputAlreadyConnected) disconnect_variable(input);
        auto& simInfo = simulators_.at(output.simulator);
        simInfo.sim->expose_for_getting(output.type, output.reference);
        simInfo.outgoingFunConnections.push_back({output, input});
    }

    void connect_variables(
        function_io_id output,
        variable_id input,
        bool inputAlreadyConnected)
    {
        if (inputAlreadyConnected) disconnect_variable(input);
        auto& funInfo = functions_.at(output.function);
        auto& simInfo = simulators_.at(input.simulator);
        simInfo.sim->expose_for_setting(input.type, input.reference);
        funInfo.outgoingSimConnections.push_back({output, input});
        update_function_decimation_factor(funInfo);
    }

    void disconnect_variable(variable_id input)
    {
        for (auto& s : simulators_) {
            auto& conns = s.second.outgoingSimConnections;
            const auto it = std::find_if(
                conns.begin(),
                conns.end(),
                [input](const auto& c) { return c.target == input; });
            if (it != conns.end()) {
                conns.erase(it);
                // There can be only one connection to an input variable,
                // so we return as early as possible.
                return;
            }
        }
    }

    void disconnect_variable(function_io_id input)
    {
        for (auto& s : simulators_) {
            auto& conns = s.second.outgoingFunConnections;
            const auto it = std::find_if(
                conns.begin(),
                conns.end(),
                [input](const auto& c) { return c.target == input; });
            if (it != conns.end()) {
                conns.erase(it);
                // There can be only one connection to an input variable,
                // so we return as early as possible.
                return;
            }
        }
    }

    void setup(time_point startTime, std::optional<time_point> stopTime)
    {
        startTime_ = startTime;
        stopTime_ = stopTime;
    }

    void initialize()
    {
        for_all_simulators([=](auto s) {
            return s->setup(startTime_, stopTime_, std::nullopt);
        });

        // Run N iterations of the simulators and functions' step/calculation
        // procedures, where N is the number of simulators in the system,
        // to propagate initial values.
        for (std::size_t i = 0; i < simulators_.size(); ++i) {
            for_all_simulators([=](auto s) {
                return s->do_iteration();
            });
            for (const auto& s : simulators_) {
                transfer_variables(s.second.outgoingFunConnections);
            }
            for (const auto& f : functions_) {
                f.second.fun->calculate();
            }
            for (const auto& s : simulators_) {
                transfer_variables(s.second.outgoingSimConnections);
            }
            for (const auto& f : functions_) {
                transfer_variables(f.second.outgoingSimConnections);
            }
        }

        for_all_simulators([](auto s) {
            return s->start_simulation();
        });
    }

    std::pair<duration, std::unordered_set<simulator_index>> do_step(time_point currentT)
    {
        for (auto& s : simulators_) {
            auto& info = s.second;
            if (stepCounter_ % info.decimationFactor == 0) {
                info.stepResult = info.sim->do_step(currentT, baseStepSize_ * info.decimationFactor);
            }
        }

        ++stepCounter_;

        std::unordered_set<simulator_index> finished;
        bool failed = false;
        std::stringstream errMessages;
        for (auto& [idx, info] : simulators_) {
            if (stepCounter_ % info.decimationFactor == 0) {
                if (auto ep = info.stepResult.get_exception_ptr()) {
                    errMessages
                        << info.sim->name() << ": "
                        << exception_ptr_msg(ep) << '\n';
                    failed = true;
                } else if (info.stepResult.get() != step_result::complete) {
                    errMessages
                        << info.sim->name() << ": "
                        << "Step not complete" << '\n';
                    failed = true;
                }
                finished.insert(idx);
            }
        }
        if (failed) {
            throw error(make_error_code(errc::simulation_error), errMessages.str());
        }

        std::unordered_set<function_index> updatedFunctions;
        for (auto simIndex : finished) {
            auto& simInfo = simulators_.at(simIndex);
            transfer_variables(simInfo.outgoingSimConnections);
            transfer_variables(simInfo.outgoingFunConnections);
            for (const auto& fc : simInfo.outgoingFunConnections) {
                updatedFunctions.insert(fc.target.function);
            }
        }
        for (auto funIndex : updatedFunctions) {
            const auto& funInfo = functions_.at(funIndex);
            funInfo.fun->calculate();
            transfer_variables(funInfo.outgoingSimConnections);
        }

        return std::pair(baseStepSize_, std::move(finished));
    }

    void set_stepsize_decimation_factor(cse::simulator_index i, int factor)
    {
        CSE_INPUT_CHECK(factor > 0);
        simulators_.at(i).decimationFactor = factor;
    }


private:
    struct connection_ss
    {
        variable_id source;
        variable_id target;
    };

    struct connection_sf
    {
        variable_id source;
        function_io_id target;
    };

    struct connection_fs
    {
        function_io_id source;
        variable_id target;
    };

    struct simulator_info
    {
        simulator* sim;
        int decimationFactor = 1;
        boost::fibers::future<step_result> stepResult;
        std::vector<connection_ss> outgoingSimConnections;
        std::vector<connection_sf> outgoingFunConnections;
    };

    struct function_info
    {
        function* fun;
        int decimationFactor = 1;
        std::vector<connection_fs> outgoingSimConnections;
    };

    void disconnect_simulator_variables(simulator_index i)
    {
        for (auto& s : simulators_) {
            auto& connections = s.second.outgoingSimConnections;
            const auto newEnd = std::remove_if(
                connections.begin(),
                connections.end(),
                [i](const auto& c) { return c.target.simulator == i; });
            connections.erase(newEnd, connections.end());
        }
        for (auto& f : functions_) {
            auto& connections = f.second.outgoingSimConnections;
            const auto newEnd = std::remove_if(
                connections.begin(),
                connections.end(),
                [i](const auto& c) { return c.target.simulator == i; });
            connections.erase(newEnd, connections.end());
        }
    }

    void update_function_decimation_factor(function_info& f)
    {
        // The decimation factor of a function is equal to the greatest one
        // among the simulators that receive its results.
        int decimationFactor = 1;
        for (const auto& c : f.outgoingSimConnections) {
            decimationFactor = std::max(
                decimationFactor,
                simulators_.at(c.target.simulator).decimationFactor);
        }
        f.decimationFactor = decimationFactor;
    }

    template<typename F>
    void for_all_simulators(F f)
    {
        std::unordered_map<simulator_index, boost::fibers::future<void>> results;
        for (const auto& s : simulators_) {
            results.emplace(s.first, f(s.second.sim));
        }

        bool failed = false;
        std::stringstream errMessages;
        for (auto& r : results) {
            try {
                r.second.get();
            } catch (const std::exception& e) {
                errMessages
                    << simulators_.at(r.first).sim->name() << ": "
                    << e.what() << '\n';
                failed = true;
            }
        }
        if (failed) {
            throw error(make_error_code(errc::simulation_error), errMessages.str());
        }
    }

    void transfer_variables(const std::vector<connection_ss>& connections)
    {
        for (const auto& c : connections) {
            const auto sdf = simulators_.at(c.source.simulator).decimationFactor;
            const auto tdf = simulators_.at(c.target.simulator).decimationFactor;
            if (stepCounter_ % std::lcm(sdf, tdf) == 0) {
                transfer_variable(c);
            }
        }
    }

    void transfer_variables(const std::vector<connection_sf>& connections)
    {
        for (const auto& c : connections) {
            const auto sdf = simulators_.at(c.source.simulator).decimationFactor;
            const auto tdf = functions_.at(c.target.function).decimationFactor;
            if (stepCounter_ % std::lcm(sdf, tdf) == 0) {
                transfer_variable(c);
            }
        }
    }

    void transfer_variables(const std::vector<connection_fs>& connections)
    {
        for (const auto& c : connections) {
            const auto sdf = functions_.at(c.source.function).decimationFactor;
            const auto tdf = simulators_.at(c.target.simulator).decimationFactor;
            if (stepCounter_ % std::lcm(sdf, tdf) == 0) {
                transfer_variable(c);
            }
        }
    }

    void transfer_variable(const connection_ss& c)
    {
        assert(c.source.type == c.target.type);
        switch (c.target.type) {
            case variable_type::real:
                simulators_.at(c.target.simulator).sim->set_real(c.target.reference, simulators_.at(c.source.simulator).sim->get_real(c.source.reference));
                break;
            case variable_type::integer:
                simulators_.at(c.target.simulator).sim->set_integer(c.target.reference, simulators_.at(c.source.simulator).sim->get_integer(c.source.reference));
                break;
            case variable_type::boolean:
                simulators_.at(c.target.simulator).sim->set_boolean(c.target.reference, simulators_.at(c.source.simulator).sim->get_boolean(c.source.reference));
                break;
            case variable_type::string:
                simulators_.at(c.target.simulator).sim->set_string(c.target.reference, simulators_.at(c.source.simulator).sim->get_string(c.source.reference));
                break;
            case variable_type::enumeration:
                CSE_PANIC_M("Can't handle variable of type 'enumeration' yet");
        }
    }

    void transfer_variable(const connection_sf& c)
    {
        assert(c.source.type == c.target.type);
        switch (c.target.type) {
            case variable_type::real:
                functions_.at(c.target.function).fun->set_real_io(c.target.reference, simulators_.at(c.source.simulator).sim->get_real(c.source.reference));
                break;
            case variable_type::integer:
                functions_.at(c.target.function).fun->set_integer_io(c.target.reference, simulators_.at(c.source.simulator).sim->get_integer(c.source.reference));
                break;
            case variable_type::boolean:
                //                functions_.at(c.target.function).fun->set_boolean_io(
                //                    c.target.reference,
                //                    simulators_.at(c.source.simulator).sim->get_boolean(c.source.reference));
                //                break;
            case variable_type::string:
                //                functions_.at(c.target.function).fun->set_string_io(
                //                    c.target.reference,
                //                    simulators_.at(c.source.simulator).sim->get_string(c.source.reference));
                //                break;
            case variable_type::enumeration:
                CSE_PANIC_M("Can't handle variable of type 'enumeration' yet");
        }
    }

    void transfer_variable(const connection_fs& c)
    {
        assert(c.source.type == c.target.type);
        switch (c.target.type) {
            case variable_type::real:
                simulators_.at(c.target.simulator).sim->set_real(c.target.reference, functions_.at(c.source.function).fun->get_real_io(c.source.reference));
                break;
            case variable_type::integer:
                simulators_.at(c.target.simulator).sim->set_integer(c.target.reference, functions_.at(c.source.function).fun->get_integer_io(c.source.reference));
                break;
            case variable_type::boolean:
                //                simulators_.at(c.target.simulator).sim->set_boolean(
                //                    c.target.reference,
                //                    functions_.at(c.source.function).fun->get_boolean_io(c.source.reference));
                //                break;
            case variable_type::string:
                //                simulators_.at(c.target.simulator).sim->set_string(
                //                    c.target.reference,
                //                    functions_.at(c.source.function).fun->get_string_io(c.source.reference));
                //                break;
            case variable_type::enumeration:
                CSE_PANIC_M("Can't handle variable of type 'enumeration' yet");
        }
    }

    const duration baseStepSize_;
    time_point startTime_;
    std::optional<time_point> stopTime_;
    std::unordered_map<simulator_index, simulator_info> simulators_;
    std::unordered_map<function_index, function_info> functions_;
    int64_t stepCounter_ = 0;
};


fixed_step_algorithm::fixed_step_algorithm(duration baseStepSize)
    : pimpl_(std::make_unique<impl>(baseStepSize))
{
}


fixed_step_algorithm::~fixed_step_algorithm() noexcept {}


fixed_step_algorithm::fixed_step_algorithm(fixed_step_algorithm&& other) noexcept
    : pimpl_(std::move(other.pimpl_))
{
}


fixed_step_algorithm& fixed_step_algorithm::operator=(
    fixed_step_algorithm&& other) noexcept
{
    pimpl_ = std::move(other.pimpl_);
    return *this;
}


void fixed_step_algorithm::add_simulator(
    simulator_index i,
    simulator* s,
    duration stepSizeHint)
{
    pimpl_->add_simulator(i, s, stepSizeHint);
}


void fixed_step_algorithm::remove_simulator(simulator_index i)
{
    pimpl_->remove_simulator(i);
}


void fixed_step_algorithm::add_function(function_index i, function* f)
{
    pimpl_->add_function(i, f);
}

void fixed_step_algorithm::connect_variables(
    variable_id output, variable_id input, bool inputAlreadyConnected)
{
    pimpl_->connect_variables(output, input, inputAlreadyConnected);
}

void fixed_step_algorithm::connect_variables(
    variable_id output, function_io_id input, bool inputAlreadyConnected)
{
    pimpl_->connect_variables(output, input, inputAlreadyConnected);
}

void fixed_step_algorithm::connect_variables(
    function_io_id output, variable_id input, bool inputAlreadyConnected)
{
    pimpl_->connect_variables(output, input, inputAlreadyConnected);
}

void fixed_step_algorithm::disconnect_variable(variable_id input)
{
    pimpl_->disconnect_variable(input);
}

void fixed_step_algorithm::disconnect_variable(function_io_id input)
{
    pimpl_->disconnect_variable(input);
}


void fixed_step_algorithm::setup(
    time_point startTime,
    std::optional<time_point> stopTime)
{
    pimpl_->setup(startTime, stopTime);
}


void fixed_step_algorithm::initialize()
{
    pimpl_->initialize();
}


std::pair<duration, std::unordered_set<simulator_index>> fixed_step_algorithm::do_step(
    time_point currentT)
{
    return pimpl_->do_step(currentT);
}

void fixed_step_algorithm::set_stepsize_decimation_factor(cse::simulator_index simulator, int factor)
{
    pimpl_->set_stepsize_decimation_factor(simulator, factor);
}


} // namespace cse
