#ifndef CSE_SYSTEM_STRUCTURE_HPP
#define CSE_SYSTEM_STRUCTURE_HPP

#include <cse/orchestration.hpp>

#include <gsl/span>

#include <memory>
#include <string>
#include <string_view>


namespace cse
{


class system_structure
{
public:
    system_structure() noexcept;

    system_structure(const system_structure&) = delete;
    system_structure& operator=(const system_structure&) = delete;
    system_structure(system_structure&&) noexcept;
    system_structure& operator=(system_structure&&) noexcept;

    struct simulator
    {
        std::string name;
        std::shared_ptr<model> model;
    };

    void add_simulator(const simulator& s);

    struct variable_qname
    {
        std::string simulator_name;
        std::string variable_name;
    };

    struct scalar_connection
    {
        variable_qname source;
        variable_qname target;
    };

    void add_scalar_connection(const scalar_connection& c);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};


bool is_valid_simulator_name(std::string_view name) noexcept;

bool is_valid_connection(
    const variable_description& sourceVar,
    const variable_description& targetVar,
    std::string* reason);


} // namespace cse
#endif // header guard
