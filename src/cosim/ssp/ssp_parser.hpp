/**
 *  \file
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef LIBCOSIM_SSP_PARSER_HPP
#define LIBCOSIM_SSP_PARSER_HPP

#include "cosim/algorithm.hpp"

#include <boost/filesystem/path.hpp>
#include <boost/property_tree/ptree.hpp>

#include <optional>
#include <string>
#include <unordered_map>

namespace cosim
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
        std::shared_ptr<cosim::algorithm> algorithm;
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
        cosim::variable_type type;
    };

    struct Parameter
    {
        std::string name;
        cosim::variable_type type;
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
        std::vector<ParameterSet> parameterSets;
        std::unordered_map<std::string, Connector> connectors;
    };

    [[nodiscard]] const std::unordered_map<std::string, Component>& get_elements() const;

    struct LinearTransformation
    {
        double offset;
        double factor;
    };

    struct Connection
    {
        Component startElement;
        Connector startConnector;
        Component endElement;
        Connector endConnector;
        std::optional<LinearTransformation> linearTransformation;
    };

    [[nodiscard]] const std::vector<Connection>& get_connections() const;

private:
    SystemDescription systemDescription_;
    DefaultExperiment defaultExperiment_;

    std::vector<Connection> connections_;
    std::unordered_map<std::string, Component> elements_;
};

struct slave_info
{
    cosim::simulator_index index;
    std::map<std::string, cosim::variable_description> variables;
};

cosim::time_point get_default_start_time(const ssp_parser& parser);

cosim::variable_id get_variable(
    const std::map<std::string,
        slave_info>& slaves,
    const std::string& element,
    const std::string& connector);


std::optional<ssp_parser::ParameterSet> get_parameter_set(
    const ssp_parser::Component& component,
    std::optional<std::string> parameterSetName);

} // namespace cosim

#endif
