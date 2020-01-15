#ifndef CSE_FUNCTION_LINEAR_TRANSFORMATION_HPP
#define CSE_FUNCTION_LINEAR_TRANSFORMATION_HPP

#include <cse/function/function.hpp>

#include <vector>


namespace cse
{


/**
 *  A scalar linear transformation function.
 */
class linear_transformation_function : public function
{
public:
    function_type_description type_description() const override;

    void set_parameter(int index, function_parameter_value value) override;

    void initialize() override;

    void set_io(int groupIndex, int ioIndex, scalar_value_view value) override;

    scalar_value_view get_io(int groupIndex, int ioIndex) const override;

    void calculate() override;

private:
    double offset_ = 0.0;
    double factor_ = 1.0;

    double input_ = 0.0;
    double output_ = 0.0;
};

} // namespace cse
#endif // header guard
