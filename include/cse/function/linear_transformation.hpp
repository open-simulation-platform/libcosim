/**
 *  \file
 *  Linear transformation function.
 */
#ifndef CSE_FUNCTION_LINEAR_TRANSFORMATION_HPP
#define CSE_FUNCTION_LINEAR_TRANSFORMATION_HPP

#include <cse/function/function.hpp>

#include <vector>


namespace cse
{


/// A scalar linear transformation function.
class linear_transformation_function : public function
{
public:
    /**
     *  Constructor.
     *
     *  \param [in] offset
     *      The constant term.
     *  \param [in] factor
     *      The scaling factor.
     */
    linear_transformation_function(double offset, double factor);

    // Overridden `function` methods
    function_type_description description() const override;
    void set_real_io(const function_io_reference& reference, double value) override;
    void set_integer_io(const function_io_reference& reference, int value) override;
    double get_real_io(const function_io_reference& reference) const override;
    int get_integer_io(const function_io_reference& reference) const override;
    void calculate() override;

private:
    double offset_ = 0.0;
    double factor_ = 1.0;
    double input_ = 0.0;
    double output_ = 0.0;
};


/// Factory class for `linear_transformation_function`.
class linear_transformation_function_type : public function_type
{
public:
    function_type_description description() const override;

    std::unique_ptr<function> instantiate(
        const std::unordered_map<int, function_parameter_value> parameters) override;
};


} // namespace cse
#endif // header guard
