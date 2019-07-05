#if defined(_WIN32) && !defined(NOMINMAX)
#    define NOMINMAX
#endif
#include "cse/algorithm.hpp"

#include "cse/error.hpp"
#include "cse/exception.hpp"

#include <algorithm>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <vector>


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
} // namespace


namespace cse
{

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

    void add_simulator(simulator_index i, simulator* s)
    {
        assert(simulators_.count(i) == 0);
        simulators_[i].sim = s;
    }

    void remove_simulator(simulator_index i)
    {
        remove_connections(i);
        simulators_.erase(i);
    }

    void add_connection(std::shared_ptr<connection> c)
    {
        for (const auto& source : c->get_sources()) {
            auto& simInfo = find_simulator(source.simulator);
            simInfo.sim->expose_for_getting(source.type, source.index);
            simInfo.outgoingConnections[source].push_back(c);
        }
        for (const auto& destination : c->get_destinations()) {
            auto& simInfo = find_simulator(destination.simulator);
            simInfo.sim->expose_for_setting(destination.type, destination.index);
            simInfo.incomingConnections[destination] = c;
        }
    }

    void remove_connection(std::shared_ptr<connection> c)
    {
        remove_sources(c);
        remove_destinations(c);
    }

    void setup(time_point startTime, std::optional<time_point> stopTime)
    {
        startTime_ = startTime;
        stopTime_ = stopTime;
    }

    void initialize()
    {
        setup_simulators();
        for (std::size_t i = 0; i < simulators_.size(); ++i) {
            iterate_simulators();
            for (const auto& [idx, sim] : simulators_) {
                transfer_sources(idx);
                transfer_destinations(idx);
            }
        }
    }

    std::pair<duration, std::unordered_set<simulator_index>> do_step(time_point currentT)
    {
        for (auto& s : simulators_) {
            auto& info = s.second;
            if (stepCounter_ % info.decimationFactor == 0) {
                transfer_destinations(s.first);
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

        for (auto idx : finished) {
            transfer_sources(idx);
        }

        return std::pair(baseStepSize_, std::move(finished));
    }

    void set_stepsize_decimation_factor(cse::simulator_index i, int factor)
    {
        CSE_INPUT_CHECK(factor > 0);
        simulators_.at(i).decimationFactor = factor;
    }


private:
    struct simulator_info
    {
        simulator* sim;
        int decimationFactor = 1;
        boost::fibers::future<step_result> stepResult;
        std::unordered_map<variable_id, std::vector<std::shared_ptr<connection>>> outgoingConnections;
        std::unordered_map<variable_id, std::shared_ptr<connection>> incomingConnections;
    };

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

    void setup_simulators()
    {
        for_all_simulators([=](auto s) {
            return s->setup(startTime_, stopTime_, std::nullopt);
        });
    }

    void iterate_simulators()
    {
        for_all_simulators([=](auto s) {
            return s->do_iteration();
        });
    }

    void remove_destinations(std::shared_ptr<connection> c)
    {
        for (const auto& destinationId : c->get_destinations()) {
            auto& connectedSim = find_simulator(destinationId.simulator);
            connectedSim.incomingConnections.erase(destinationId);
        }
    }

    void remove_sources(std::shared_ptr<connection> c)
    {
        for (const auto& sourceId : c->get_sources()) {
            auto& sourceSim = find_simulator(sourceId.simulator);
            auto& outgoingConns = sourceSim.outgoingConnections.at(sourceId);
            outgoingConns.erase(std::remove(outgoingConns.begin(), outgoingConns.end(), c), outgoingConns.end());
            if (outgoingConns.empty()) {
                sourceSim.outgoingConnections.erase(sourceId);
            }
        }
    }

    void remove_connections(simulator_index i)
    {
        for (auto& entry : simulators_.at(i).outgoingConnections) {
            for (auto& sourceConn : entry.second) {
                remove_destinations(sourceConn);
            }
        }
        for (auto& entry : simulators_.at(i).incomingConnections) {
            remove_sources(entry.second);
        }
    }

    simulator_info& find_simulator(simulator_index i)
    {
        auto sim = simulators_.find(i);
        if (sim == simulators_.end()) {
            std::ostringstream oss;
            oss << "Cannot find simulator with index " << i;
            throw std::out_of_range(oss.str());
        }
        return sim->second;
    }

    void transfer_sources(simulator_index i)
    {
        for (auto& [sourceVar, connections] : simulators_[i].outgoingConnections) {
            for (const auto& c : connections) {
                switch (sourceVar.type) {
                    case variable_type::real:
                        c->set_source_value(sourceVar, simulators_.at(i).sim->get_real(sourceVar.index));
                        break;
                    case variable_type::integer:
                        c->set_source_value(sourceVar, simulators_.at(i).sim->get_integer(sourceVar.index));
                        break;
                    case variable_type::boolean:
                        c->set_source_value(sourceVar, simulators_.at(i).sim->get_boolean(sourceVar.index));
                        break;
                    case variable_type::string:
                        c->set_source_value(sourceVar, simulators_.at(i).sim->get_string(sourceVar.index));
                        break;
                    default:
                        CSE_PANIC();
                }
            }
        }
    }

    bool decimation_factor_match(variable_id destination, const std::vector<variable_id>& sources)
    {
        const auto idf = simulators_[destination.simulator].decimationFactor;
        bool match = true;
        for (const auto& source : sources) {
            const auto odf = simulators_[source.simulator].decimationFactor;
            match &= (stepCounter_ % std::lcm(odf, idf) == 0);
        }
        return match;
    }

    void transfer_destinations(simulator_index i)
    {
        for (auto& [destVar, connection] : simulators_[i].incomingConnections) {
            if (decimation_factor_match(destVar, connection->get_sources())) {
                switch (destVar.type) {
                    case variable_type::real:
                        simulators_.at(i).sim->set_real(destVar.index, std::get<double>(connection->get_destination_value(destVar)));
                        break;
                    case variable_type::integer:
                        simulators_.at(i).sim->set_integer(destVar.index, std::get<int>(connection->get_destination_value(destVar)));
                        break;
                    case variable_type::boolean:
                        simulators_.at(i).sim->set_boolean(destVar.index, std::get<bool>(connection->get_destination_value(destVar)));
                        break;
                    case variable_type::string:
                        simulators_.at(i).sim->set_string(destVar.index, std::get<std::string_view>(connection->get_destination_value(destVar)));
                        break;
                    default:
                        CSE_PANIC();
                }
            }
        }
    }

    const duration baseStepSize_;
    time_point startTime_;
    std::optional<time_point> stopTime_;
    std::unordered_map<simulator_index, simulator_info> simulators_;
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
    simulator* s)
{
    pimpl_->add_simulator(i, s);
}


void fixed_step_algorithm::remove_simulator(simulator_index i)
{
    pimpl_->remove_simulator(i);
}


void fixed_step_algorithm::add_connection(std::shared_ptr<connection> c)
{
    pimpl_->add_connection(c);
}

void fixed_step_algorithm::remove_connection(std::shared_ptr<connection> c)
{
    pimpl_->remove_connection(c);
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
