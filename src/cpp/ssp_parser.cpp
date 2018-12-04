#include <cse/ssp_parser.hpp>

namespace cse
{

ssp_parser::ssp_parser(boost::filesystem::path xmlPath)
    : xmlPath_(xmlPath)
{
    // Root node
    std::string path = "ssd:SystemStructureDescription";

    boost::property_tree::ptree tmpTree;
    boost::property_tree::read_xml(xmlPath.string(), pt_);

    tmpTree = pt_.get_child(path);
    systemDescription_.name = get_attribute(tmpTree, "name");
    systemDescription_.version = get_attribute(tmpTree, "version");

    path += ".ssd:System";
    tmpTree = pt_.get_child(path);

    systemDescription_.systemName = get_attribute(tmpTree, "name");
    systemDescription_.systemDescription = get_attribute(tmpTree, "description");

    for (const auto& infos : tmpTree.get_child("ssd:SimulationInformation")) {
        if (infos.first == "ssd:FixedStepMaster") {
            simulationInformation_.description = get_attribute(infos.second, "description");
            simulationInformation_.stepSize = get_attribute(infos.second, "stepSize");
        }
    }

    for (const auto& component : tmpTree.get_child("ssd:Elements")) {
        std::cout << get_attribute(component.second, "name");
        /*for (const auto& connector : component.second.get_child("ssd:Connectors")) {
            std::cout << get_attribute(connector.second, "name");
        }*/
    }

    for (const auto& connection : tmpTree.get_child("ssd:Connections")) {
        std::cout << get_attribute(connection.second, "startElement");
    }
}

ssp_parser::~ssp_parser() noexcept = default;

std::string ssp_parser::get_attribute(boost::property_tree::ptree tree, std::string key)
{
    return tree.get<std::string>("<xmlattr>." + key);
}

} // namespace cse