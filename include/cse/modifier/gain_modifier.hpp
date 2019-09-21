
#ifndef CSECORE_GAIN_MODIFIER_HPP
#define CSECORE_GAIN_MODIFIER_HPP


#include <cse/modifier/modifier.hpp>

namespace cse
{

class gain_modifier : modifier
{

public:
    gain_modifier(double gain);

private:
    scalar apply(variable_type, scalar value) override;

private:
    double gain_;
};

} // namespace cse

#endif
