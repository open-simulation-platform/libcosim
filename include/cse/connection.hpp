#ifndef CSECORE_CONNECTION_HPP
#define CSECORE_CONNECTION_HPP

#include "exception.hpp"

#include <cse/execution.hpp>
#include <cse/model.hpp>

#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace cse
{

class multi_connection
{
public:

    virtual const std::vector<variable_id>& get_sources() = 0;
    virtual void set_real_source_value(variable_id id, double value) = 0;

    virtual const std::vector<variable_id>& get_destinations() = 0;
    virtual double get_real_destination_value(variable_id id) = 0;
};

class sum_connection : public multi_connection
{

public:
    sum_connection(std::vector<variable_id> sources, variable_id destination)
        : sources_(sources)
        , destinations_{destination}
    {
        for (const auto id : sources) {
            sourceValues_[id] = 0.0;
        }
    }

    const std::vector<variable_id>& get_sources() override
    {
        return sources_;
    }

    const std::vector<variable_id>& get_destinations() override
    {
        return destinations_;
    }

    void set_real_source_value(variable_id id, double value) override
    {
        sourceValues_.at(id) = value;
    }

    double get_real_destination_value(variable_id id) override
    {
        double sum = 0.0;
        for (const auto& entry : sourceValues_) {
            sum += entry.second;
        }
        return sum;
    }

private:
    std::vector<variable_id> sources_;
    std::vector<variable_id> destinations_;
    std::unordered_map<variable_id, double> sourceValues_;
};

} // namespace cse

#endif //CSECORE_CONNECTION_HPP
