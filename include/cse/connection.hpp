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
    const std::vector<variable_id>& get_sources() const
    {
        return sources_;
    }
    virtual void set_real_source_value(variable_id id, double value) = 0;

    const std::vector<variable_id>& get_destinations() const
    {
        return destinations_;
    }
    virtual double get_real_destination_value(variable_id id) = 0;

protected:
    multi_connection(std::vector<variable_id> sources, std::vector<variable_id> destinations)
        : sources_(std::move(sources))
        , destinations_(std::move(destinations))
    {}
    std::vector<variable_id> sources_;
    std::vector<variable_id> destinations_;
};


class scalar_connection : public multi_connection
{

public:
    scalar_connection(variable_id source, variable_id destination)
        : multi_connection({source}, {destination})
    {}

    void set_real_source_value(variable_id, double value) override
    {
        realValue_ = value;
    }

    double get_real_destination_value(variable_id) override
    {
        return realValue_;
    }

private:
    double realValue_ = 0.0;
};

class sum_connection : public multi_connection
{

public:
    sum_connection(const std::vector<variable_id>& sources, variable_id destination)
        : multi_connection(sources, {destination})
    {
        for (const auto id : sources) {
            sourceValues_[id] = 0.0;
        }
    }

    void set_real_source_value(variable_id id, double value) override
    {
        sourceValues_.at(id) = value;
    }

    double get_real_destination_value(variable_id) override
    {
        double sum = 0.0;
        for (const auto& entry : sourceValues_) {
            sum += entry.second;
        }
        return sum;
    }

private:
    std::unordered_map<variable_id, double> sourceValues_;
};

} // namespace cse

#endif //CSECORE_CONNECTION_HPP
