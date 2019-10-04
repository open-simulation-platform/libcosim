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
    struct simulator
    {
        std::string name;
        std::shared_ptr<model> model;
    };

    struct scalar_connection
    {
        variable_qname source;
        variable_qname target;
    };

    system_structure() noexcept;

    system_structure(const system_structure&) = delete;
    system_structure& operator=(const system_structure&) = delete;
    system_structure(system_structure&&) noexcept;
    system_structure& operator=(system_structure&&) noexcept;

    void add_simulator(const simulator& s);

    const simulator& get_simulator(std::string_view name) const;

    void add_scalar_connection(const scalar_connection& c);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};


bool is_valid_simulator_name(std::string_view name) noexcept;

bool is_valid_connection(
    const variable_description& source,
    const variable_description& target,
    std::string* reason);


} // namespace cse
#endif // header guard
