
#ifndef CSECORE_CONNECTION__SUM_CONNECTION_HPP
#define CSECORE_CONNECTION__SUM_CONNECTION_HPP

#include <cse/connection/connection.hpp>

namespace cse
{

/**
 * A class representing a sum connection.
 *
 * The destination value is calculated as the sum of all source variable
 * values. Only valid when used with variables of type `real` or `integer`.
 * Mixing of variable types is not allowed.
 */
class sum_connection : public connection
{

public:
    /// Constructor which takes a number of source variables and one destination variable.
    sum_connection(const std::vector<variable_id>& sources, variable_id destination);

    void set_source_value(variable_id id, std::variant<double, int, bool, std::string_view> value) override;

    std::variant<double, int, bool, std::string_view> get_destination_value(variable_id id) override;

private:
    std::unordered_map<variable_id, std::variant<double, int, bool, std::string_view>> values_;
};

} // namespace cse

#endif
