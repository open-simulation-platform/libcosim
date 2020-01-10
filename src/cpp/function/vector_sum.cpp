#include "cse/function/vector_sum.hpp"

#include <gsl/span>

#include <cassert>


using namespace std::literals;


namespace cse
{

enum vector_sum_parameters
{
    vector_sum_inputCount = 0,
    vector_sum_numberType = 1,
    vector_sum_dimension = 2,
};


function_type_description vector_sum_function::type_description() const
{
    return {
        "VectorSum",
        {
            // parameters (in the same order as vector_sum_parameter enum!)
            function_parameter_description{
                "inputCount", // name
                function_parameter_type::integer, // type
                1, // default_value
                1, // min_value
                {} // max_value
            },
            function_parameter_description{
                "numberType", // name
                function_parameter_type::type, // type
                variable_type::real, // default_value
                {}, // min_value
                {} // max_value
            },
            function_parameter_description{
                "dimension", // name
                function_parameter_type::integer, // type
                1, // default_value
                1, // min_value
                {} // max_value
            },
        },
        {
            // io_groups
            function_io_group_description{
                "in", // name
                function_parameter_placeholder{vector_sum_inputCount}, // count
                {
                    // ios
                    function_io_description{
                        ""s, // name (inherited from group)
                        function_parameter_placeholder{vector_sum_numberType}, // type
                        function_parameter_placeholder{vector_sum_dimension}, // count
                    },
                }},
            function_io_group_description{
                "out", // name
                1, // count
                {
                    // ios
                    function_io_description{
                        ""s, // name = (inherited from group)
                        function_parameter_placeholder{vector_sum_numberType}, // type
                        function_parameter_placeholder{vector_sum_dimension}, // count
                    },
                }},
        }};
}


void vector_sum_function::set_parameter(int index, function_parameter_value value)
{
    // TODO: Check that parameter values are within bounds
    switch (index) {
        case vector_sum_inputCount:
            inputCount_ = std::get<int>(value);
            break;
        case vector_sum_numberType:
            integerType_ = std::get<variable_type>(value) == variable_type::integer;
            break;
        case vector_sum_dimension:
            dimension_ = std::get<int>(value);
            break;
    };
}


void vector_sum_function::initialize()
{
    if (integerType_) {
        integerIOs_ = std::vector<std::vector<int>>(
            inputCount_ + 1,
            std::vector<int>(dimension_, 0));
    } else {
        realIOs_ = std::vector<std::vector<double>>(
            inputCount_ + 1,
            std::vector<double>(dimension_, 0.0));
    }
}


void vector_sum_function::set_io(int groupIndex, int ioIndex, scalar_value_view value)
{
    if (integerType_) {
        integerIOs_.at(groupIndex).at(ioIndex) = std::get<int>(value);
    } else {
        realIOs_.at(groupIndex).at(ioIndex) = std::get<double>(value);
    }
}


scalar_value_view vector_sum_function::get_io(int groupIndex, int ioIndex) const
{
    if (integerType_) {
        return integerIOs_.at(groupIndex).at(ioIndex);
    } else {
        return realIOs_.at(groupIndex).at(ioIndex);
    }
}


namespace
{
template<typename T>
void add_vectors(gsl::span<std::vector<T>> inputs, std::vector<T>& output)
{
    assert(inputs[0].size() == output.size());
    output.assign(inputs[0].begin(), inputs[0].end());
    for (const auto& term : inputs.subspan(1)) {
        assert(term.size() == output.size());
        for (std::size_t i = 0; i < output.size(); ++i) {
            output[i] += term[i];
        }
    }
}
} // namespace


void vector_sum_function::calculate()
{
    if (integerType_) {
        add_vectors(gsl::make_span(integerIOs_).first(inputCount_), integerIOs_.back());
    } else {
        add_vectors(gsl::make_span(realIOs_).first(inputCount_), realIOs_.back());
    }
}

} // namespace cse
