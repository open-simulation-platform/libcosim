
#ifndef CSECORE_CONNECTION_GAIN_CONNECTION_HPP
#define CSECORE_CONNECTION_GAIN_CONNECTION_HPP

#include <cse/connection/connection.hpp>

namespace cse
{

class gain_connection : public connection
{

public:
    void set_source_value(variable_id id, std::variant<double, int, bool, std::string_view> value) override;
    std::variant<double, int, bool, std::string_view> get_destination_value(variable_id id) override;
};

} // namespace cse


#endif
