
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

    struct SimulationInformation
    {
        std::string description;
        std::string stepSize;
    };

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
    SimulationInformation simulationInformation_;
    std::vector<Component> elements_;
    std::vector<Connection> connections_;

    std::string get_attribute(boost::property_tree::ptree tree, std::string key);
};

} // namespace cse

#endif //CSECORE_SSP_PARSER_HPP
