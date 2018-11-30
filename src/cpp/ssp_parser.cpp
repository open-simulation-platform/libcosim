#include <cse/ssp_parser.hpp>

namespace cse
{

ssp_parser::ssp_parser(boost::filesystem::path xmlPath)
    : xmlPath_(xmlPath)
{
    boost::property_tree::ptree tmpTree;
    boost::property_tree::read_xml(xmlPath.string(), pt_);

    tmpTree = pt_.get_child("ssd:SystemStructureDescription");
    systemDescription_.name = get_attribute(tmpTree, "name");
    systemDescription_.version = get_attribute(tmpTree, "version");

    tmpTree = pt_.get_child("ssd:SystemStructureDescription.ssd:System");
    systemDescription_.systemName = get_attribute(tmpTree, "name");
    systemDescription_.systemDescription = get_attribute(tmpTree, "description");


    /*for (const auto& attr : system.get_child("<xmlattr>")) {
        std::string key = attr.first;

        if (key == "name")
            systemDescription_.systemName = attr.second.data();
        else if (key == "description")
            systemDescription_.systemDescription = attr.second.data();
    }*/

    for (const auto& kv : system.get_child("ssd:SimulationInformation")) {
        if (kv.first == "ssd:FixedStepMaster") {
            // Set up algo
        }

        for (const auto& attr : kv.second.get_child("<xmlattr>")) {
            std::string key = attr.first;
            std::string val = attr.second.data();

            if (key == "description") simulationInformation_.description = val;
            if (key == "stepSize") simulationInformation_.stepSize = val;
        }
    }

    for (const auto& elem : system.get_child("ssd:Elements")) {
        for (const auto& connector : elem.second.get_child("ssd:Connectors")) {
            (void)connector;
            //std::cout << connector.first << std::endl;
        }
    }
}

ssp_parser::~ssp_parser() noexcept = default;

std::string ssp_parser::get_attribute(boost::property_tree::ptree tree, std::string key) {
    return tree.get<std::string>("<xmlattr>." + key);
}

} // namespace cse