#include "cse/slave_simulator.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <boost/container/vector.hpp>


namespace cse {
    namespace {

        template<typename T>
        struct var_view_type {
            using type = T;
        };
        template<>
        struct var_view_type<std::string> {
            using type = std::string_view;
        };

        template<typename T>
        struct exposed_vars {
            std::vector<variable_index> indexes;
            boost::container::vector<T> originalValues;
            boost::container::vector<T> manipulatedValues;
            std::vector<std::function<T(T)>> manipulators;
            std::unordered_map<variable_index, std::size_t> indexMapping;

            void expose(variable_index i) {
                if (indexMapping.count(i)) return;
                indexes.push_back(i);
                originalValues.push_back(T()); // TODO: Use start value from model description
                manipulatedValues.push_back(T());
                manipulators.emplace_back();
                indexMapping[i] = indexes.size() - 1;
            }

            typename var_view_type<T>::type get(variable_index i) const {
                const auto it = indexMapping.find(i);
                if (it != indexMapping.end()) {
                    return manipulatedValues[it->second];
                } else {
                    std::ostringstream oss;
                    oss << "variable_index " << i
                        << " not found in exposed variables. Variables must be exposed before calling get()";
                    throw std::out_of_range(oss.str());
                }
            }

            void set(variable_index i, typename var_view_type<T>::type v) {
                const auto it = indexMapping.find(i);
                if (it != indexMapping.end()) {
                    originalValues[it->second] = v;
                } else {
                    std::ostringstream oss;
                    oss << "variable_index " << i
                        << " not found in exposed variables. Variables must be exposed before calling set()";
                    throw std::out_of_range(oss.str());
                }
            }

            void set_manipulator(variable_index i, std::function<T(T)> m)
            {
                manipulators[indexMapping[i]] = m;
            }

            void run_manipulators()
            {
                for (std::size_t i = 0; i < originalValues.size(); ++i) {
                    if (manipulators[i]) {
                        manipulatedValues[i] = manipulators[i](originalValues[i]);
                    } else {
                        manipulatedValues[i] = originalValues[i];
                    }
                }
            }
        };

// Copies the contents of a contiguous-storage container or span into another.
        template<typename Src, typename Tgt>
        void copy_contents(Src &&src, Tgt &&tgt) {
            assert(static_cast<std::size_t>(src.size()) <= static_cast<std::size_t>(tgt.size()));
            std::copy(src.data(), src.data() + src.size(), tgt.data());
        }
    } // namespace


    class slave_simulator::impl {
    public:
        impl(std::shared_ptr<async_slave> slave, std::string_view name)
                : slave_(std::move(slave)), name_(name), modelDescription_(slave_->model_description().get()) {
            assert(slave_);
            assert(!name_.empty());
        }

        ~impl() noexcept = default;

        impl(const impl &) = delete;

        impl &operator=(const impl &) = delete;

        impl(impl &&) noexcept = delete;

        impl &operator=(impl &&) noexcept = delete;

        std::string name() const {
            return name_;
        }

        cse::model_description model_description() const {
            return modelDescription_;
        }


        void expose_for_getting(variable_type type, variable_index index) {
            switch (type) {
                case variable_type::real:
                    realGetCache_.expose(index);
                    break;
                case variable_type::integer:
                    integerGetCache_.expose(index);
                    break;
                case variable_type::boolean:
                    booleanGetCache_.expose(index);
                    break;
                case variable_type::string:
                    stringGetCache_.expose(index);
                    break;
            }
        }

        double get_real(variable_index index) const {
            return realGetCache_.get(index);
        }

        int get_integer(variable_index index) const {
            return integerGetCache_.get(index);
        }

        bool get_boolean(variable_index index) const {
            return booleanGetCache_.get(index);
        }

        std::string_view get_string(variable_index index) const {
            return stringGetCache_.get(index);
        }

        void expose_for_setting(variable_type type, variable_index index) {
            switch (type) {
                case variable_type::real:
                    realSetCache_.expose(index);
                    break;
                case variable_type::integer:
                    integerSetCache_.expose(index);
                    break;
                case variable_type::boolean:
                    booleanSetCache_.expose(index);
                    break;
                case variable_type::string:
                    stringSetCache_.expose(index);
                    break;
            }
        }

        void set_real(variable_index index, double value) {
            realSetCache_.set(index, value);
        }

        void set_integer(variable_index index, int value) {
            integerSetCache_.set(index, value);
        }

        void set_boolean(variable_index index, bool value) {
            booleanSetCache_.set(index, value);
        }

        void set_string(variable_index index, std::string_view value) {
            stringSetCache_.set(index, value);
        }

        void set_real_input_manipulator(
            variable_index index,
            std::function<double(double)> manipulator)
        {
            realSetCache_.set_manipulator(index, manipulator);
        }

        void set_integer_input_manipulator(
            variable_index index,
            std::function<int(int)> manipulator)
        {
            integerSetCache_.set_manipulator(index, manipulator);
        }

        void set_boolean_input_manipulator(
            variable_index index,
            std::function<bool(bool)> manipulator)
        {
            booleanSetCache_.set_manipulator(index, manipulator);
        }

        void set_string_input_manipulator(
            variable_index index,
            std::function<std::string(std::string_view)> manipulator)
        {
            stringSetCache_.set_manipulator(index, manipulator);
        }

        void set_real_output_manipulator(
            variable_index index,
            std::function<double(double)> manipulator)
        {
            realSetCache_.set_manipulator(index, manipulator);
        }

        void set_integer_output_manipulator(
            variable_index index,
            std::function<int(int)> manipulator)
        {
            integerSetCache_.set_manipulator(index, manipulator);
        }

        void set_boolean_output_manipulator(
            variable_index index,
            std::function<bool(bool)> manipulator)
        {
            booleanSetCache_.set_manipulator(index, manipulator);
        }

        void set_string_output_manipulator(
            variable_index index,
            std::function<std::string(std::string_view)> manipulator)
        {
            stringSetCache_.set_manipulator(index, manipulator);
        }

        boost::fibers::future<void> setup(
                time_point startTime,
                std::optional<time_point> stopTime,
                std::optional<double> relativeTolerance) {
            return slave_->setup(startTime, stopTime, relativeTolerance);
        }

        boost::fibers::future<void> do_iteration() {
            // clang-format off
            return boost::fibers::async([=]() {
                set_variables();
                get_variables();
            });
            // clang-format on
        }

        boost::fibers::future<step_result> do_step(
                time_point currentT,
                duration deltaT) {
            // clang-format off
            return boost::fibers::async([=]() {
                if (slave_->state() == slave_state::initialisation) {
                    slave_->start_simulation().get();
                }
                set_variables();
                const auto result = slave_->do_step(currentT, deltaT).get();
                get_variables();
                return result;
            });
            // clang-format on
        }

    private:
        void set_variables() {
            realSetCache_.run_manipulators();
            integerSetCache_.run_manipulators();
            booleanSetCache_.run_manipulators();
            stringSetCache_.run_manipulators();
            slave_->set_variables(
                            gsl::make_span(realSetCache_.indexes),
                            gsl::make_span(realSetCache_.manipulatedValues),
                            gsl::make_span(integerSetCache_.indexes),
                            gsl::make_span(integerSetCache_.manipulatedValues),
                            gsl::make_span(booleanSetCache_.indexes),
                            gsl::make_span(booleanSetCache_.manipulatedValues),
                            gsl::make_span(stringSetCache_.indexes),
                            gsl::make_span(stringSetCache_.manipulatedValues))
                    .get();
        }

        void get_variables() {
            const auto values = slave_->get_variables(
                            gsl::make_span(realGetCache_.indexes),
                            gsl::make_span(integerGetCache_.indexes),
                            gsl::make_span(booleanGetCache_.indexes),
                            gsl::make_span(stringGetCache_.indexes))
                    .get();
            copy_contents(values.real, realGetCache_.originalValues);
            copy_contents(values.integer, integerGetCache_.originalValues);
            copy_contents(values.boolean, booleanGetCache_.originalValues);
            copy_contents(values.string, stringGetCache_.originalValues);
            realGetCache_.run_manipulators();
            integerGetCache_.run_manipulators();
            booleanGetCache_.run_manipulators();
            stringGetCache_.run_manipulators();
        }

    private:
        std::shared_ptr<async_slave> slave_;
        std::string name_;
        cse::model_description modelDescription_;

        exposed_vars<double> realGetCache_;
        exposed_vars<int> integerGetCache_;
        exposed_vars<bool> booleanGetCache_;
        exposed_vars<std::string> stringGetCache_;

        exposed_vars<double> realSetCache_;
        exposed_vars<int> integerSetCache_;
        exposed_vars<bool> booleanSetCache_;
        exposed_vars<std::string> stringSetCache_;
    };


    slave_simulator::slave_simulator(
            std::shared_ptr<async_slave> slave,
            std::string_view name)
            : pimpl_(std::make_unique<impl>(std::move(slave), name)) {
    }


    slave_simulator::~slave_simulator() noexcept = default;

    slave_simulator::slave_simulator(slave_simulator &&) noexcept = default;

    slave_simulator &slave_simulator::operator=(slave_simulator &&) noexcept = default;


    std::string slave_simulator::name() const {
        return pimpl_->name();
    }


    cse::model_description slave_simulator::model_description() const {
        return pimpl_->model_description();
    }


    void slave_simulator::expose_for_getting(variable_type type, variable_index index) {
        pimpl_->expose_for_getting(type, index);
    }


    double slave_simulator::get_real(variable_index index) const {
        return pimpl_->get_real(index);
    }


    int slave_simulator::get_integer(variable_index index) const {
        return pimpl_->get_integer(index);
    }


    bool slave_simulator::get_boolean(variable_index index) const {
        return pimpl_->get_boolean(index);
    }


    std::string_view slave_simulator::get_string(variable_index index) const {
        return pimpl_->get_string(index);
    }


    void slave_simulator::expose_for_setting(variable_type type, variable_index index) {
        pimpl_->expose_for_setting(type, index);
    }


    void slave_simulator::set_real(variable_index index, double value) {
        pimpl_->set_real(index, value);
    }


    void slave_simulator::set_integer(variable_index index, int value) {
        pimpl_->set_integer(index, value);
    }


    void slave_simulator::set_boolean(variable_index index, bool value) {
        pimpl_->set_boolean(index, value);
    }


    void slave_simulator::set_string(variable_index index, std::string_view value) {
        pimpl_->set_string(index, value);
    }

    void slave_simulator::set_real_input_manipulator(
        variable_index index,
        std::function<double(double)> manipulator)
    {
        pimpl_->set_real_input_manipulator(index, manipulator);
    }

    void slave_simulator::set_integer_input_manipulator(
        variable_index index,
        std::function<int(int)> manipulator)
    {
        pimpl_->set_integer_input_manipulator(index, manipulator);
    }

    void slave_simulator::set_boolean_input_manipulator(
        variable_index index,
        std::function<bool(bool)> manipulator)
    {
        pimpl_->set_boolean_input_manipulator(index, manipulator);
    }

    void slave_simulator::set_string_input_manipulator(
        variable_index index,
        std::function<std::string(std::string_view)> manipulator)
    {
        pimpl_->set_string_input_manipulator(index, manipulator);
    }

    void slave_simulator::set_real_output_manipulator(
        variable_index index,
        std::function<double(double)> manipulator)
    {
        pimpl_->set_real_output_manipulator(index, manipulator);
    }

    void slave_simulator::set_integer_output_manipulator(
        variable_index index,
        std::function<int(int)> manipulator)
    {
        pimpl_->set_integer_output_manipulator(index, manipulator);
    }

    void slave_simulator::set_boolean_output_manipulator(
        variable_index index,
        std::function<bool(bool)> manipulator)
    {
        pimpl_->set_boolean_output_manipulator(index, manipulator);
    }

    void slave_simulator::set_string_output_manipulator(
        variable_index index,
        std::function<std::string(std::string_view)> manipulator)
    {
        pimpl_->set_string_output_manipulator(index, manipulator);
    }

    boost::fibers::future<void> slave_simulator::setup(
            time_point startTime,
            std::optional<time_point> stopTime,
            std::optional<double> relativeTolerance) {
        return pimpl_->setup(startTime, stopTime, relativeTolerance);
    }


    boost::fibers::future<void> slave_simulator::do_iteration() {
        return pimpl_->do_iteration();
    }


    boost::fibers::future<step_result> slave_simulator::do_step(
            time_point currentT,
            duration deltaT) {
        return pimpl_->do_step(currentT, deltaT);
    }


} // namespace cse
