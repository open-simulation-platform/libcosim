#include <cse/async_slave.hpp>

#include <type_traits>
#include <utility>

#include <boost/container/vector.hpp>
#include <boost/fiber/future/async.hpp>


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
    pseudo_async_slave(std::unique_ptr<cse::slave> slave)
        : slave_(std::move(slave))
    {}

    ~pseudo_async_slave() = default;

    // Disable copy and move, since we leak `this` references.
    pseudo_async_slave(pseudo_async_slave&&) = delete;
    pseudo_async_slave& operator=(pseudo_async_slave&&) = delete;
    pseudo_async_slave(const pseudo_async_slave&) = delete;
    pseudo_async_slave& operator=(const pseudo_async_slave&) = delete;

    // cse::async_slave function implementations
    boost::fibers::future<cse::model_description> model_description() override
    {
        return boost::fibers::async([=]() {
            return slave_->model_description();
        });
    }

    boost::fibers::future<void> setup(
        cse::time_point startTime,
        std::optional<time_point> stopTime,
        std::optional<double> relativeTolerance)
        override
    {
        return boost::fibers::async([=]() {
            slave_->setup(startTime, stopTime, relativeTolerance);
        });
    }

    boost::fibers::future<void> start_simulation() override
    {
        return boost::fibers::async([=]() {
            slave_->start_simulation();
        });
    }

    boost::fibers::future<void> end_simulation() override
    {
        return boost::fibers::async([=]() {
            slave_->end_simulation();
        });
    }

    boost::fibers::future<cse::step_result> do_step(
        cse::time_point currentT,
        cse::time_duration deltaT)
        override
    {
        return boost::fibers::async([=]() {
            return slave_->do_step(currentT, deltaT);
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
        realBuffer_.resize(realVariables.size());
        integerBuffer_.resize(integerVariables.size());
        booleanBuffer_.resize(booleanVariables.size());
        stringBuffer_.resize(stringVariables.size());

        return boost::fibers::async(
            [
                =,
                rvi = to_vector(realVariables),
                ivi = to_vector(integerVariables),
                bvi = to_vector(booleanVariables),
                svi = to_vector(stringVariables)
            ]() {
                auto rva = gsl::make_span(realBuffer_);
                auto iva = gsl::make_span(integerBuffer_);
                auto bva = gsl::make_span(booleanBuffer_);
                auto sva = gsl::make_span(stringBuffer_);
                slave_->get_real_variables(gsl::make_span(rvi), rva);
                slave_->get_integer_variables(gsl::make_span(ivi), iva);
                slave_->get_boolean_variables(gsl::make_span(bvi), bva);
                slave_->get_string_variables(gsl::make_span(svi), sva);
                return variable_values{rva, iva, bva, sva};
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
                // NOTE: We don't handle nonfatal_bad_value correctly here.
                // All functions should get called, and the exceptions should get merged.
                slave_->set_real_variables(gsl::make_span(rvi), gsl::make_span(rva));
                slave_->set_integer_variables(gsl::make_span(ivi), gsl::make_span(iva));
                slave_->set_boolean_variables(gsl::make_span(bvi), gsl::make_span(bva));
                slave_->set_string_variables(gsl::make_span(svi), gsl::make_span(sva));
            });
    }

    // clang-format on

private:
    std::unique_ptr<cse::slave> slave_;

    // We need Boost's vector<bool> to avoid the issues with std::vector<bool>.
    // The others are just for consistency.
    boost::container::vector<double> realBuffer_;
    boost::container::vector<int> integerBuffer_;
    boost::container::vector<bool> booleanBuffer_;
    boost::container::vector<std::string> stringBuffer_;
};
} // namespace


std::unique_ptr<async_slave> make_pseudo_async(std::unique_ptr<slave> s)
{
    return std::make_unique<pseudo_async_slave>(std::move(s));
}


} // namespace cse
