
#ifndef CSECORE_LINEAR_TRANSFORMATION_HPP
#define CSECORE_LINEAR_TRANSFORMATION_HPP

#include <cse/connection/scalar_connection.hpp>

namespace cse
{

class linear_transformation_connection: public scalar_connection {

public:
    linear_transformation_connection(variable_id source, variable_id destination, double offset, double factor);
    void set_source_value(variable_id id, std::variant<double, int, bool, std::string_view> value) override;

private:
    double offset_;
    double factor_;
};

}

#endif //CSECORE_LINEAR_TRANSFORMATION_HPP
