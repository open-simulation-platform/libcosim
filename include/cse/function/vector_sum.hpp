/**
 *  \file
 *  Vector sum function.
 */
#ifndef CSE_FUNCTION_VECTOR_SUM_HPP
#define CSE_FUNCTION_VECTOR_SUM_HPP

#include <cse/function/function.hpp>

#include <stdexcept>
#include <type_traits>
#include <variant>
#include <vector>


namespace cse
{
namespace detail
{
function_type_description vector_sum_description(
    std::variant<int, function_parameter_placeholder> inputCount,
    std::variant<variable_type, function_parameter_placeholder> numericType,
    std::variant<int, function_parameter_placeholder> dimension);
}


/**
 *  A vector sum function that supports real and integer numbers, and an
 *  arbitrary number of inputs with arbitrary dimension.
 *
 *  \tparam T
 *      The vector value type, either `double` or `int`.
 */
template<typename T>
class vector_sum_function : public function
{
public:
    static_assert(std::is_same_v<T, double> || std::is_same_v<T, int>);

    /**
     *  Constructor.
     *
     *  \param [in] inputCount
     *      The number of input vectors.
     *  \param [in] dimension
     *      The dimension of the input and output vectors.
     */
    vector_sum_function(int inputCount, int dimension)
        : ios_(inputCount, std::vector<T>(dimension))
    {
    }

    // Overridden `function` methods.
    function_type_description description() const override
    {
        return detail::vector_sum_description(
            ios_.size() - 1,
            std::is_integral_v<T> ? variable_type::integer : variable_type::real,
            ios_.front().size());
    }

#ifdef __GNUC__
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wunused-but-set-parameter"
#endif
    void set_real_io(
        const function_io_reference& reference,
        double value) override
    {
        if constexpr (std::is_same_v<T, double>) {
            if (reference.group == 0 && reference.io == 0) {
                ios_.at(reference.group_instance).at(reference.io_instance) = value;
            }
        }
        throw std::out_of_range("Invalid function variable reference");
    }

    void set_integer_io(
        const function_io_reference& reference,
        int value) override
    {
        if constexpr (std::is_same_v<T, int>) {
            if (reference.group == 0 && reference.io == 0) {
                ios_.at(reference.group_instance).at(reference.io_instance) = value;
            }
        }
        throw std::out_of_range("Invalid function variable reference");
    }

    double get_real_io(const function_io_reference& reference) const override
    {
        if constexpr (std::is_same_v<T, double>) {
            if (reference.group == 0 && reference.io == 0) {
                return ios_.at(reference.group_instance).at(reference.io_instance);
            }
            if (reference.group == 1 && reference.group_instance == 0 && reference.io == 0) {
                return ios_.back().at(reference.io_instance);
            }
        }
        throw std::out_of_range("Invalid function variable reference");
    }

    int get_integer_io(const function_io_reference& reference) const override
    {
        if constexpr (std::is_same_v<T, int>) {
            if (reference.group == 0 && reference.io == 0) {
                return ios_.at(reference.group_instance).at(reference.io_instance);
            }
            if (reference.group == 1 && reference.group_instance == 0 && reference.io == 0) {
                return ios_.back().at(reference.io_instance);
            }
        }
        throw std::out_of_range("Invalid function variable reference");
    }
#ifdef __GNUC__
#    pragma GCC diagnostic pop
#endif

    void calculate() override
    {
        auto& output = ios_.back();
        output.assign(ios_[0].begin(), ios_[0].end());
        for (std::size_t i = 1; i < ios_.size() - 1; ++i) {
            for (std::size_t j = 0; j < output.size(); ++j) {
                output[j] += ios_[i][j];
            }
        }
    }

private:
    // The last vector in ios_ is the output
    std::vector<std::vector<T>> ios_;
};


/// Factory class for `vector_sum_function`.
class vector_sum_function_type : public function_type
{
public:
    function_type_description description() const override;

    std::unique_ptr<function> instantiate(
        const std::unordered_map<int, function_parameter_value> parameters) override;
};


} // namespace cse
#endif // header guard
