
#ifndef CSECORE_MODIFIER_GAIN_MODIFIER_HPP
#define CSECORE_MODIFIER_GAIN_MODIFIER_HPP

#include <cse/modifier/modifier.hpp>

namespace cse
{

class gain_modifier : public modifier
{

public:
    gain_modifier(double gain);

    scalar apply(variable_type, scalar value) override;

private:
    double gain_;
};

} // namespace cse

#endif
