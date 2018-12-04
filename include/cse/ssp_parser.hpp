
#ifndef CSECORE_SSP_PARSER_HPP
#define CSECORE_SSP_PARSER_HPP

#include <boost/foreach.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <exception>
#include <string>

#include <boost/filesystem/path.hpp>

#include <cse/algorithm.hpp>
#include <cse/fmi/fmu.hpp>


namespace cse
{

class ssp_parser
{

public:
    ssp_parser(boost::filesystem::path xmlPath);
    ~ssp_parser() noexcept;

    struct DefaultExperiment
    {
        double startTime;
        double stopTime;
    };

    const DefaultExperiment &get_default_experiment() const;

    struct SimulationInformation
    {
        std::string description;
        double stepSize;
    };

    const SimulationInformation &get_simulation_information() const;

    struct SystemDescription
    {
        std::string name;
        std::string version;
        std::string systemName;
        std::string systemDescription;
    };

    struct Connector
    {
        std::string name;
        std::string kind;
        std::string type;
    };

    struct Component
    {
        std::string name;
        std::string source;
        std::vector<Connector> connectors;
    };

    const std::vector<Component> &get_elements() const;

    struct Connection
    {
        std::string startElement;
        std::string startConnector;
        std::string endElement;
        std::string endConnector;
    };

private:
    boost::filesystem::path xmlPath_;
    boost::property_tree::ptree pt_;

    SystemDescription systemDescription_;
    DefaultExperiment defaultExperiment_;
    SimulationInformation simulationInformation_;
    std::vector<Component> elements_;
    std::vector<Connection> connections_;

};

} // namespace cse

#endif //CSECORE_SSP_PARSER_HPP
