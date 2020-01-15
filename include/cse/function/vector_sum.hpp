#ifndef CSE_FUNCTION_VECTOR_SUM_HPP
#define CSE_FUNCTION_VECTOR_SUM_HPP

#include <cse/function/function.hpp>

#include <vector>


namespace cse
{


/**
 *  A vector sum function that supports real and integer numbers, and an
 *  arbitrary number of inputs with arbitrary dimension.
 */
class vector_sum_function : public function
{
public:
    function_type_description type_description() const override;

    void set_parameter(int index, function_parameter_value value) override;

    void initialize() override;

    void set_io(
        int groupIndex,
        int groupInstance,
        int ioIndex,
        int ioInstance,
        scalar_value_view value) override;

    scalar_value_view get_io(
        int groupIndex,
        int groupInstance,
        int ioIndex,
        int ioInstance) const override;

    void calculate() override;

private:
    int inputCount_ = 1;
    bool integerType_ = false;
    int dimension_ = 1;

    // The last vector in ios_ is the output
    std::vector<std::vector<double>> realIOs_;
    std::vector<std::vector<int>> integerIOs_;
};

} // namespace cse
#endif // header guard
