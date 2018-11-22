#ifndef CSECORE_OBSERVABLE_HPP
#define CSECORE_OBSERVABLE_HPP

#include "cse/model.hpp"

namespace cse
{

/// Interface for observable entities in a simulation.
class observable
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
    virtual void expose_for_getting(variable_type, variable_index) = 0;

    /**
         *  Returns the value of a real variable.
         *
         *  The variable must previously have been exposed with `expose_for_getting()`.
         */
    virtual double get_real(variable_index) const = 0;

    /**
         *  Returns the value of an integer variable.
         *
         *  The variable must previously have been exposed with `expose_for_getting()`.
         */
    virtual int get_integer(variable_index) const = 0;

    /**
         *  Returns the value of a boolean variable.
         *
         *  The variable must previously have been exposed with `expose_for_getting()`.
         */
    virtual bool get_boolean(variable_index) const = 0;

    /**
         *  Returns the value of a string variable.
         *
         *  The variable must previously have been exposed with `expose_for_getting()`.
         *
         *  The returned `std::string_view` is only guaranteed to remain valid
         *  until the next call of this or any other of the object's methods.
         */
    virtual std::string_view get_string(variable_index) const = 0;

    virtual ~observable() noexcept {}
};


} // namespace cse

#endif //Header guard
