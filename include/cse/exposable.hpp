#ifndef CSECORE_EXPOSABLE_HPP
#define CSECORE_EXPOSABLE_HPP

#include <cse/model.hpp>

#include <string>

namespace cse
{

/// Common base class for `observable` and `manipulable` entities.
class exposable
{
public:
    /// Returns the entity's name.
    virtual std::string name() const = 0;

    /// Returns a description of the entity.
    virtual cse::model_description model_description() const = 0;

    /**
     *  Exposes a variable for retrieval with `get_xxx()`.
     *
     *  The purpose is fundamentally to select which variables get transferred
     *  from remote simulators at each step, so that each individual `get_xxx()`
     *  function call doesn't trigger a separate RPC operation.
     */
    virtual void expose_for_getting(variable_type, value_reference) = 0;

    /**
     *  Exposes a variable for assignment with `set_xxx()`.
     *
     *  The purpose is fundamentally to select which variables get transferred
     *  to remote simulators at each step, so that each individual `set_xxx()`
     *  function call doesn't trigger a new data exchange.
     *
     *  Calling this function more than once for the same variable has no
     *  effect.
     */
    virtual void expose_for_setting(variable_type, value_reference) = 0;
};
} // namespace cse
#endif //CSECORE_EXPOSABLE_HPP
