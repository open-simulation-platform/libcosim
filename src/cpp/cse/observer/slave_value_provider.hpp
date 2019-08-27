#ifndef CSE_OBSERVER_SLAVE_VALUE_PROVIDER_HPP
#define CSE_OBSERVER_SLAVE_VALUE_PROVIDER_HPP

#include <cse/execution.hpp>
#include <cse/model.hpp>
#include <cse/observer/observer.hpp>

#include <gsl/span>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace cse
{

class slave_value_provider
{

public:
    slave_value_provider(observable* obs);
    ~slave_value_provider() noexcept;
    void observe();
    void get_real(gsl::span<const value_reference> variables, gsl::span<double> values);
    void get_int(gsl::span<const value_reference> variables, gsl::span<int> values);
    void get_boolean(gsl::span<const value_reference> variables, gsl::span<bool> values);
    void get_string(gsl::span<const value_reference> variables, gsl::span<std::string> values);

private:
    std::unordered_map<value_reference, double> realSamples_;
    std::unordered_map<value_reference, int> intSamples_;
    std::unordered_map<value_reference, bool> boolSamples_;
    std::unordered_map<value_reference, std::string> stringSamples_;
    observable* observable_;
    std::mutex lock_;
};

} // namespace cse

#endif // Header guard
