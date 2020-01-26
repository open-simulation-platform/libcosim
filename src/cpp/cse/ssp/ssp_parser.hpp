#ifndef CSECORE_SSP_PARSER_HPP
#define CSECORE_SSP_PARSER_HPP

#include "cse/algorithm.hpp"

#include <boost/filesystem/path.hpp>
#include <boost/property_tree/ptree.hpp>

#include <optional>
#include <string>

namespace cse
{

class ssp_parser
{

public:
    explicit ssp_parser(const boost::filesystem::path& xmlPath);
    ~ssp_parser() noexcept;

    struct DefaultExperiment
    {
        double startTime = 0.0;
        std::optional<double> stopTime;
        std::shared_ptr<cse::algorithm> algorithm;
    };

    const DefaultExperiment& get_default_experiment() const;

    struct System
    {
        std::string name;
        std::optional<std::string> description;
    };

    struct SystemDescription
    {
        std::string name;
        std::string version;
        System system;
    };

    struct Connector
    {
        std::string name;
        std::string kind;
        cse::variable_type type;
    };

    struct Parameter
    {
        std::string name;
        cse::variable_type type;
        scalar_value value;
    };

    struct ParameterSet
    {
        std::string name;
        std::vector<Parameter> parameters;
    };

    struct Component
    {
        std::string name;
        std::string source;
        std::optional<double> stepSizeHint;
        std::vector<Connector> connectors;
        std::vector<ParameterSet> parameterSets;
    };

    const std::vector<Component>& get_elements() const;

    struct LinearTransformation
    {
        double offset;
        double factor;
    };

    struct Connection
    {
        std::string startElement;
        std::string startConnector;
        std::string endElement;
        std::string endConnector;
        std::optional<LinearTransformation> linearTransformation;
    };

    const std::vector<Connection>& get_connections() const;

private:
    SystemDescription systemDescription_;
    DefaultExperiment defaultExperiment_;

    std::vector<Component> elements_;
    std::vector<Connection> connections_;
};

struct slave_info
{
    cse::simulator_index index;
    std::map<std::string, cse::variable_description> variables;
};

cse::time_point get_default_start_time(const ssp_parser& parser);

cse::variable_id get_variable(
    const std::map<std::string,
        slave_info>& slaves,
    const std::string& element,
    const std::string& connector);


std::optional<ssp_parser::ParameterSet> get_parameter_set(
    const ssp_parser::Component& component,
    std::optional<std::string> parameterSetName);

} // namespace cse

#endif
