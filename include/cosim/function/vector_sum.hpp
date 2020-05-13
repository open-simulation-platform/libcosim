/**
 *  \file
 *  Vector sum function.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_FUNCTION_VECTOR_SUM_HPP
#define COSIM_FUNCTION_VECTOR_SUM_HPP

#include <cosim/function/function.hpp>

#include <stdexcept>
#include <type_traits>
#include <variant>
#include <vector>


namespace cosim
{
namespace detail
{
function_type_description vector_sum_description(
    std::variant<int, function_parameter_placeholder> inputCount,
    std::variant<variable_type, function_parameter_placeholder> numericType,
    std::variant<int, function_parameter_placeholder> dimension);
}


/**
 *  A vector sum function instance.
 *
 *  See `vector_sum_function_type` for a description of this function.
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
    function_description description() const override
    {
        return detail::vector_sum_description(
            static_cast<int>(inputs_.size()),
            std::is_integral_v<T> ? variable_type::integer : variable_type::real,
            static_cast<int>(inputs_.front().size()));
    }

    void set_real(
        [[maybe_unused]] const function_io_reference& reference,
        [[maybe_unused]] double value) override
    {
        if constexpr (std::is_same_v<T, double>) {
            if (reference.group == 0 && reference.io == 0) {
                inputs_.at(reference.group_instance).at(reference.io_instance) = value;
                return;
            }
        }
        throw_bad_io_ref();
    }

    void set_integer(
        [[maybe_unused]] const function_io_reference& reference,
        [[maybe_unused]] int value) override
    {
        if constexpr (std::is_same_v<T, int>) {
            if (reference.group == 0 && reference.io == 0) {
                inputs_.at(reference.group_instance).at(reference.io_instance) = value;
                return;
            }
        }
        throw_bad_io_ref();
    }

    void set_boolean(
        const function_io_reference& /*reference*/,
        bool /*value*/) override
    {
        throw_bad_io_ref();
    }

    void set_string(
        const function_io_reference& /*reference*/,
        std::string_view /*value*/) override
    {
        throw_bad_io_ref();
    }

    double get_real([[maybe_unused]] const function_io_reference& reference) const override
    {
        if constexpr (std::is_same_v<T, double>) {
            if (reference.group == 0 && reference.io == 0) {
                return inputs_.at(reference.group_instance).at(reference.io_instance);
            }
            if (reference.group == 1 && reference.group_instance == 0 && reference.io == 0) {
                return output_.at(reference.io_instance);
            }
        }
        throw_bad_io_ref();
    }

    int get_integer([[maybe_unused]] const function_io_reference& reference) const override
    {
        if constexpr (std::is_same_v<T, int>) {
            if (reference.group == 0 && reference.io == 0) {
                return inputs_.at(reference.group_instance).at(reference.io_instance);
            }
            if (reference.group == 1 && reference.group_instance == 0 && reference.io == 0) {
                return output_.at(reference.io_instance);
            }
        }
        throw_bad_io_ref();
    }

    bool get_boolean(const function_io_reference& /*reference*/) const override
    {
        throw_bad_io_ref();
    }

    std::string_view get_string(const function_io_reference& /*reference*/) const override
    {
        throw_bad_io_ref();
    }

    void calculate() override
    {
        output_.assign(inputs_[0].begin(), inputs_[0].end());
        for (std::size_t i = 1; i < inputs_.size(); ++i) {
            for (std::size_t j = 0; j < output_.size(); ++j) {
                output_[j] += inputs_[i][j];
            }
        }
    }

    /// Reference to a component of an input vector, for convencience.
    constexpr static function_io_reference in_io_reference(int inputVector, int component)
    {
        return {0, inputVector, 0, component};
    }

    /// Reference to a component of the output vector, for convencience.
    constexpr static function_io_reference out_io_reference(int component)
    {
        return {1, 0, 0, component};
    }

private:
    [[noreturn]] static void throw_bad_io_ref()
    {
        throw std::out_of_range("Invalid function variable reference");
    }

    std::vector<std::vector<T>> inputs_;
    std::vector<T> output_;
};


/**
 *  A vector sum function type.
 *
 *  ### Operation
 *
 *      out = in[0] + in[1] + ... + in[inputCount-1]
 *
 *  ### Parameters
 *
 *  | Parameter   | Type          | Default | Description                           |
 *  |-------------|---------------|---------|---------------------------------------|
 *  | inputCount  | integer       | 1       | Number of input vectors               |
 *  | numericType | variable type | real    | Vector element type (real or integer) |
 *  | dimension   | integer       | 1       | Dimension of input and output vectors |
 *
 *  ### Variables
 *
 *  | Group | Count        | Variable  | Count       | Causality | Type          | Description    |
 *  |-------|--------------|-----------|-------------|-----------|---------------|----------------|
 *  | in    | `inputCount` | (unnamed) | `dimension` | input     | `numericType` | Input vectors  |
 *  | out   | 1            | (unnamed) | `dimension` | output    | `numericType` | Output vectors |
 *
 *  ### Instance type
 *
 *  `vector_sum_function`
 */
class vector_sum_function_type : public function_type
{
public:
    /// Parameter indexes, for convenience.
    enum
    {
        inputCount_parameter_index,
        numericType_parameter_index,
        dimension_parameter_index,
    };

    // Overridden `function_type` methods
    function_type_description description() const override;

    std::unique_ptr<function> instantiate(
        const function_parameter_value_map& parameters) override;
};


} // namespace cosim
#endif // header guard
