/**
 *  \file
 *  Linear transformation function.
 */
#ifndef CSE_FUNCTION_LINEAR_TRANSFORMATION_HPP
#define CSE_FUNCTION_LINEAR_TRANSFORMATION_HPP

#include <cse/function/function.hpp>


namespace cse
{


/**
 *  A scalar linear transformation function instance.
 *
 *  See `linear_transformation_function_type` for a description of this
 *  function.
 */
class linear_transformation_function : public function
{
public:
    /// Reference to the "in" variable, for convencience.
    constexpr static auto in_io_reference = function_io_reference{0, 0, 0, 0};

    /// Reference to the "out" variable, for convencience.
    constexpr static auto out_io_reference = function_io_reference{1, 0, 0, 0};

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
    function_description description() const override;
    void set_real(const function_io_reference& reference, double value) override;
    void set_integer(const function_io_reference& reference, int value) override;
    void set_boolean(const function_io_reference& reference, bool value) override;
    void set_string(const function_io_reference& reference, std::string_view value) override;
    double get_real(const function_io_reference& reference) const override;
    int get_integer(const function_io_reference& reference) const override;
    bool get_boolean(const function_io_reference& reference) const override;
    std::string_view get_string(const function_io_reference& reference) const override;
    void calculate() override;

private:
    double offset_ = 0.0;
    double factor_ = 1.0;
    double input_ = 0.0;
    double output_ = 0.0;
};


/**
 *  A scalar linear transformation function type.
 *
 *  ### Operation
 *
 *      out = offset + factor * in
 *
 *  ### Parameters
 *
 *  | Parameter | Type | Default | Description           |
 *  |-----------|------|---------|-----------------------|
 *  | offset    | real | 0.0     | Constant term         |
 *  | factor    | real | 1.0     | Linear scaling factor |
 *
 *  ### Variables
 *
 *  | Group | Count | Variable  | Count | Causality | Type | Description  |
 *  |-------|-------|-----------|-------|-----------|------|--------------|
 *  | in    | 1     | (unnamed) | 1     | input     | real | Input value  |
 *  | out   | 1     | (unnamed) | 1     | output    | real | Output value |
 *
 *  ### Instance type
 *
 *  `linear_transformation_function`
 */
class linear_transformation_function_type : public function_type
{
public:
    /// Parameter indexes, for convenience.
    enum {
        offset_parameter_index,
        factor_parameter_index,
    };

    // Overridden `function_type` methods
    function_type_description description() const override;

    std::unique_ptr<function> instantiate(
        const function_parameter_value_map& parameters) override;
};


} // namespace cse
#endif // header guard
