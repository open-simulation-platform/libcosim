#ifndef CSECORE_CONNECTION_HPP
#define CSECORE_CONNECTION_HPP

#include "exception.hpp"

#include "cse/execution.hpp"
#include "cse/model.hpp"

#include <string_view>
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

    virtual void set_source_value(variable_id id, std::variant<double, int, bool, std::string_view> value) = 0;

    const std::vector<variable_id>& get_destinations() const
    {
        return destinations_;
    }

    virtual std::variant<double, int, bool, std::string_view> get_destination_value(variable_id id) = 0;

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
    scalar_connection(variable_id source, variable_id destination);

    void set_source_value(variable_id, std::variant<double, int, bool, std::string_view> value) override;

    std::variant<double, int, bool, std::string_view> get_destination_value(variable_id) override;

private:
    std::variant<double, int, bool, std::string_view> value_;
};

class sum_connection : public multi_connection
{

public:
    sum_connection(const std::vector<variable_id>& sources, variable_id destination);

    void set_source_value(variable_id id, std::variant<double, int, bool, std::string_view> value) override;

    std::variant<double, int, bool, std::string_view> get_destination_value(variable_id id) override;

private:
    std::unordered_map<variable_id, std::variant<double, int, bool, std::string_view>> values_;
};
} // namespace cse

#endif //CSECORE_CONNECTION_HPP
