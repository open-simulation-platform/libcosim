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
        : inputs_(inputCount, std::vector<T>(dimension))
        , output_(dimension)
    {
        if (inputCount < 1) {
            throw std::invalid_argument("Invalid inputCount value");
        }
        if (dimension < 1) {
            throw std::invalid_argument("Invalid dimension value");
        }
    }

    // Overridden `function` methods.
    function_type_description description() const override
    {
        return detail::vector_sum_description(
            inputs_.size(),
            std::is_integral_v<T> ? variable_type::integer : variable_type::real,
            inputs_.front().size());
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
                inputs_.at(reference.group_instance).at(reference.io_instance) = value;
                return;
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
                inputs_.at(reference.group_instance).at(reference.io_instance) = value;
                return;
            }
        }
        throw std::out_of_range("Invalid function variable reference");
    }

    double get_real_io(const function_io_reference& reference) const override
    {
        if constexpr (std::is_same_v<T, double>) {
            if (reference.group == 0 && reference.io == 0) {
                return inputs_.at(reference.group_instance).at(reference.io_instance);
            }
            if (reference.group == 1 && reference.group_instance == 0 && reference.io == 0) {
                return output_.at(reference.io_instance);
            }
        }
        throw std::out_of_range("Invalid function variable reference");
    }

    int get_integer_io(const function_io_reference& reference) const override
    {
        if constexpr (std::is_same_v<T, int>) {
            if (reference.group == 0 && reference.io == 0) {
                return inputs_.at(reference.group_instance).at(reference.io_instance);
            }
            if (reference.group == 1 && reference.group_instance == 0 && reference.io == 0) {
                return output_.at(reference.io_instance);
            }
        }
        throw std::out_of_range("Invalid function variable reference");
    }
#ifdef __GNUC__
#    pragma GCC diagnostic pop
#endif

    void calculate() override
    {
        output_.assign(inputs_[0].begin(), inputs_[0].end());
        for (std::size_t i = 1; i < inputs_.size(); ++i) {
            for (std::size_t j = 0; j < output_.size(); ++j) {
                output_[j] += inputs_[i][j];
            }
        }
    }

private:
    std::vector<std::vector<T>> inputs_;
    std::vector<T> output_;
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
