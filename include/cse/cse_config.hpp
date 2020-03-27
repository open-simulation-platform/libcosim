
#ifndef CSECORE_CSE_CONFIG_HPP
#define CSECORE_CSE_CONFIG_HPP

#include <cse/execution.hpp>

namespace cse
{

/// An object which holds source and model description for a sub-simulator.
struct simulator_map_entry
{
    /// The simulator index.
    simulator_index index;

    /// The model description of the simulator.
    model_description description;
};

} // namespace cse


#endif //CSECORE_CSE_CONFIG_HPP
