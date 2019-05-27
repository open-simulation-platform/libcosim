#include "cse/observer/slave_value_provider.hpp"

#include "cse/error.hpp"

#include <mutex>

namespace cse
{

namespace
{

template<typename T>
void get(
    gsl::span<const variable_index> variables,
    const std::unordered_map<variable_index, T>& samples,
    gsl::span<T> values)
{
    if (samples.empty()) {
        throw std::out_of_range("no samples available");
    }
    for (int i = 0; i < values.size(); i++) {
        variable_index valueRef = variables[i];
        values[i] = samples.at(valueRef);
    }
}

} // namespace

slave_value_provider::slave_value_provider(observable* observable)
    : observable_(observable)
{
    for (const auto& vd : observable->model_description().variables) {
        observable->expose_for_getting(vd.type, vd.index);
        switch (vd.type) {
            case cse::variable_type::real:
                realSamples_[vd.index] = double();
                break;
            case cse::variable_type::integer:
                intSamples_[vd.index] = int();
                break;
            case cse::variable_type::boolean:
                boolSamples_[vd.index] = bool();
                break;
            case cse::variable_type::string:
                stringSamples_[vd.index] = std::string();
                break;
            default:
                CSE_PANIC();
        }
    }
    observe();
}

slave_value_provider::~slave_value_provider() noexcept = default;

void slave_value_provider::observe()
{
    std::lock_guard<std::mutex> lock(lock_);

    for (auto& [idx, value] : realSamples_) {
        value = observable_->get_real(idx);
    }
    for (auto& [idx, value] : intSamples_) {
        value = observable_->get_integer(idx);
    }
    for (auto& [idx, value] : boolSamples_) {
        value = observable_->get_boolean(idx);
    }
    for (auto& [idx, value] : stringSamples_) {
        value = observable_->get_string(idx);
    }
}

void slave_value_provider::get_real(gsl::span<const variable_index> variables, gsl::span<double> values)
{
    std::lock_guard<std::mutex> lock(lock_);
    get<double>(variables, realSamples_, values);
}

void slave_value_provider::get_int(gsl::span<const variable_index> variables, gsl::span<int> values)
{
    std::lock_guard<std::mutex> lock(lock_);
    get<int>(variables, intSamples_, values);
}

void slave_value_provider::get_boolean(gsl::span<const variable_index> variables, gsl::span<bool> values)
{
    std::lock_guard<std::mutex> lock(lock_);
    get<bool>(variables, boolSamples_, values);
}

void slave_value_provider::get_string(gsl::span<const variable_index> variables, gsl::span<std::string> values)
{
    std::lock_guard<std::mutex> lock(lock_);
    get<std::string>(variables, stringSamples_, values);
}

} // namespace cse
