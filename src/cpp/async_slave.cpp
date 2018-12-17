#include <cse/async_slave.hpp>

#include <type_traits>
#include <utility>

#include <boost/container/vector.hpp>
#include <boost/fiber/future/async.hpp>
#include <boost/fiber/protected_fixedsize_stack.hpp>

#include "cse/error.hpp"


namespace cse
{
namespace
{

// Copies the contents of `span` into a new vector.
template<typename T>
boost::container::vector<std::remove_cv_t<T>> to_vector(gsl::span<T> span)
{
    return {span.begin(), span.end()};
}


class pseudo_async_slave : public cse::async_slave
{
public:
    pseudo_async_slave(std::shared_ptr<cse::slave> slave)
        : slave_(std::move(slave))
        , state_(slave_state::created)
    {}

    ~pseudo_async_slave() = default;

    // Disable copy and move, since we leak `this` references.
    pseudo_async_slave(pseudo_async_slave&&) = delete;
    pseudo_async_slave& operator=(pseudo_async_slave&&) = delete;
    pseudo_async_slave(const pseudo_async_slave&) = delete;
    pseudo_async_slave& operator=(const pseudo_async_slave&) = delete;

    // cse::async_slave function implementations
    slave_state state() const noexcept override
    {
        return state_;
    }

    boost::fibers::future<cse::model_description> model_description() override
    {
        CSE_PRECONDITION(
            state_ != slave_state::error &&
            state_ != slave_state::indeterminate);
        const auto oldState = state_;
        state_ = slave_state::indeterminate;

        return boost::fibers::async([=]() {
            try {
                const auto result = slave_->model_description();
                state_ = oldState;
                return result;
            } catch (...) {
                state_ = slave_state::error;
                throw;
            }
        });
    }

    boost::fibers::future<void> setup(
        cse::time_point startTime,
        std::optional<time_point> stopTime,
        std::optional<double> relativeTolerance)
        override
    {
        CSE_PRECONDITION(state_ == slave_state::created);
        state_ = slave_state::indeterminate;

        return boost::fibers::async([=]() {
            try {
                slave_->setup(startTime, stopTime, relativeTolerance);
                state_ = slave_state::initialisation;
            } catch (...) {
                state_ = slave_state::error;
                throw;
            }
        });
    }

    boost::fibers::future<void> start_simulation() override
    {
        CSE_PRECONDITION(state_ == slave_state::initialisation);
        state_ = slave_state::indeterminate;

        return boost::fibers::async([=]() {
            try {
                slave_->start_simulation();
                state_ = slave_state::simulation;
            } catch (...) {
                state_ = slave_state::error;
                throw;
            }
        });
    }

    boost::fibers::future<void> end_simulation() override
    {
        CSE_PRECONDITION(state_ == slave_state::simulation);
        state_ = slave_state::indeterminate;

        return boost::fibers::async([=]() {
            try {
                slave_->end_simulation();
                state_ = slave_state::terminated;
            } catch (...) {
                state_ = slave_state::error;
                throw;
            }
        });
    }

    boost::fibers::future<cse::step_result> do_step(
        cse::time_point currentT,
        cse::duration deltaT)
        override
    {
        CSE_PRECONDITION(state_ == slave_state::simulation);
        state_ = slave_state::indeterminate;

        return boost::fibers::async(
            boost::fibers::launch::post,
            std::allocator_arg,
            boost::fibers::protected_fixedsize_stack(100*1024*1024),
            [=]() {
                try {
                    const auto result = slave_->do_step(currentT, deltaT);
                    state_ = slave_state::simulation;
                    return result;
                } catch (...) {
                    state_ = slave_state::error;
                    throw;
                }
            });
    }

    // clang-format off

    boost::fibers::future<variable_values> get_variables(
        gsl::span<const cse::variable_index> realVariables,
        gsl::span<const cse::variable_index> integerVariables,
        gsl::span<const cse::variable_index> booleanVariables,
        gsl::span<const cse::variable_index> stringVariables)
        override
    {
        CSE_PRECONDITION(
            state_ == slave_state::initialisation ||
            state_ == slave_state::simulation);

        realBuffer_.resize(realVariables.size());
        integerBuffer_.resize(integerVariables.size());
        booleanBuffer_.resize(booleanVariables.size());
        stringBuffer_.resize(stringVariables.size());

        const auto oldState = state_;
        state_ = slave_state::indeterminate;

        return boost::fibers::async(
            [
                =,
                rvi = to_vector(realVariables),
                ivi = to_vector(integerVariables),
                bvi = to_vector(booleanVariables),
                svi = to_vector(stringVariables)
            ]() {
                try {
                    auto rva = gsl::make_span(realBuffer_);
                    auto iva = gsl::make_span(integerBuffer_);
                    auto bva = gsl::make_span(booleanBuffer_);
                    auto sva = gsl::make_span(stringBuffer_);
                    slave_->get_real_variables(gsl::make_span(rvi), rva);
                    slave_->get_integer_variables(gsl::make_span(ivi), iva);
                    slave_->get_boolean_variables(gsl::make_span(bvi), bva);
                    slave_->get_string_variables(gsl::make_span(svi), sva);
                    state_ = oldState;
                    return variable_values{rva, iva, bva, sva};
                } catch (...) {
                    state_ = slave_state::error;
                    throw;
                }
            });
    }

    boost::fibers::future<void> set_variables(
        gsl::span<const cse::variable_index> realVariables,
        gsl::span<const double> realValues,
        gsl::span<const cse::variable_index> integerVariables,
        gsl::span<const int> integerValues,
        gsl::span<const cse::variable_index> booleanVariables,
        gsl::span<const bool> booleanValues,
        gsl::span<const cse::variable_index> stringVariables,
        gsl::span<const std::string> stringValues)
        override
    {
        CSE_PRECONDITION(
            state_ == slave_state::initialisation ||
            state_ == slave_state::simulation);
        const auto oldState = state_;
        state_ = slave_state::indeterminate;

        return boost::fibers::async(
            [
                =,
                rvi = to_vector(realVariables),
                rva = to_vector(realValues),
                ivi = to_vector(integerVariables),
                iva = to_vector(integerValues),
                bvi = to_vector(booleanVariables),
                bva = to_vector(booleanValues),
                svi = to_vector(stringVariables),
                sva = to_vector(stringValues)
            ]() {
                try {
                    // NOTE: We don't handle nonfatal_bad_value correctly here.
                    // All functions should get called, and the exceptions should get merged.
                    // TODO: This check is at the moment necessary for some tests to pass. Should maybe find an alternative solution!
                    if (!rvi.empty()) {
                        slave_->set_real_variables(gsl::make_span(rvi), gsl::make_span(rva));
                    }
                    if (!ivi.empty()) {
                        slave_->set_integer_variables(gsl::make_span(ivi), gsl::make_span(iva));
                    }
                    if (!bvi.empty()) {
                        slave_->set_boolean_variables(gsl::make_span(bvi), gsl::make_span(bva));
                    }
                    if (!svi.empty()) {
                        slave_->set_string_variables(gsl::make_span(svi), gsl::make_span(sva));
                    }
                    state_ = oldState;
                } catch (...) {
                    state_ = slave_state::error;
                    throw;
                }
            });
    }

    // clang-format on

private:
    std::shared_ptr<cse::slave> slave_;
    slave_state state_;

    // We need Boost's vector<bool> to avoid the issues with std::vector<bool>.
    // The others are just for consistency.
    boost::container::vector<double> realBuffer_;
    boost::container::vector<int> integerBuffer_;
    boost::container::vector<bool> booleanBuffer_;
    boost::container::vector<std::string> stringBuffer_;
};
} // namespace


std::shared_ptr<async_slave> make_pseudo_async(std::shared_ptr<slave> s)
{
    return std::make_shared<pseudo_async_slave>(std::move(s));
}


} // namespace cse
