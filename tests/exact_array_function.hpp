/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef LIBCOSIM_TEST_EXACT_ARRAY_FUNCTION_HPP
#define LIBCOSIM_TEST_EXACT_ARRAY_FUNCTION_HPP

#include <cosim/function/function.hpp>

#include <array>
#include <stdexcept>
#include <vector>

class exact_array_function final : public cosim::function
{
public:
    static constexpr int dimension = 2;

    cosim::function_description description() const override
    {
        cosim::function_description result;
        result.io_groups = {
            {"in",
                2,
                {{"",
                    cosim::variable_type::uint64,
                    cosim::variable_causality::input,
                    1,
                    {std::uint64_t{dimension}}}}},
            {"out",
                1,
                {{"",
                    cosim::variable_type::uint64,
                    cosim::variable_causality::output,
                    1,
                    {std::uint64_t{dimension}}}}}};
        return result;
    }

    void set_variable_value(
        const cosim::function_io_reference& reference,
        const cosim::variable_value& value) override
    {
        if (reference.group != 0 || reference.io != 0 ||
            reference.io_instance != 0 ||
            reference.group_instance < 0 ||
            reference.group_instance >= static_cast<int>(inputs_.size())) {
            throw std::out_of_range("Invalid function variable reference");
        }
        if (value.type() != cosim::variable_type::uint64 ||
            value.dimensions() != std::vector<std::uint64_t>{dimension}) {
            throw std::invalid_argument("Function variable value mismatch");
        }
        inputs_[reference.group_instance] = value;
    }

    cosim::variable_value get_variable_value(
        const cosim::function_io_reference& reference) const override
    {
        if (reference.group == 0 && reference.io == 0 &&
            reference.io_instance == 0 &&
            reference.group_instance >= 0 &&
            reference.group_instance < static_cast<int>(inputs_.size())) {
            return inputs_[reference.group_instance];
        }
        if (reference == output_reference) {
            return output_;
        }
        throw std::out_of_range("Invalid function variable reference");
    }

    bool supports_event_iteration() const noexcept override { return true; }

    void calculate() override
    {
        const auto& first =
            std::get<cosim::uint64_values>(inputs_[0].values()).values;
        const auto& second =
            std::get<cosim::uint64_values>(inputs_[1].values()).values;
        output_ = cosim::variable_value(
            cosim::uint64_values{{first[0] + second[0],
                first[1] + second[1]}},
            {dimension});
    }

    void set_real(const cosim::function_io_reference&, double) override
    {
        throw std::out_of_range("Invalid function variable reference");
    }

    void set_integer(const cosim::function_io_reference&, int) override
    {
        throw std::out_of_range("Invalid function variable reference");
    }

    void set_boolean(const cosim::function_io_reference&, bool) override
    {
        throw std::out_of_range("Invalid function variable reference");
    }

    void set_string(const cosim::function_io_reference&, std::string_view) override
    {
        throw std::out_of_range("Invalid function variable reference");
    }

    double get_real(const cosim::function_io_reference&) const override
    {
        throw std::out_of_range("Invalid function variable reference");
    }

    int get_integer(const cosim::function_io_reference&) const override
    {
        throw std::out_of_range("Invalid function variable reference");
    }

    bool get_boolean(const cosim::function_io_reference&) const override
    {
        throw std::out_of_range("Invalid function variable reference");
    }

    std::string_view get_string(const cosim::function_io_reference&) const override
    {
        throw std::out_of_range("Invalid function variable reference");
    }

    static constexpr cosim::function_io_reference input_reference(int instance)
    {
        return {0, instance, 0, 0};
    }

    static constexpr cosim::function_io_reference output_reference{1, 0, 0, 0};

private:
    std::array<cosim::variable_value, 2> inputs_{{cosim::variable_value(
                                                      cosim::uint64_values{{0, 0}}, {dimension}),
        cosim::variable_value(
            cosim::uint64_values{{0, 0}}, {dimension})}};
    cosim::variable_value output_{
        cosim::uint64_values{{0, 0}}, {dimension}};
};

#endif
