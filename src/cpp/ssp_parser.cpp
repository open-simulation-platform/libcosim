#include <cse/ssp_parser.hpp>

namespace cse
{

ssp_parser::ssp_parser(boost::filesystem::path xmlPath)
    : xmlPath_(xmlPath)
{
    systemDescription_ systDesc;
    simulationInformation_ simInfo;

    boost::property_tree::read_xml(xmlPath.string(), pt_);

    for (const auto& ssd : pt_.get_child("ssd:SystemStructureDescription.<xmlattr>")) {
        std::string attr_key = ssd.first;

        if (attr_key == "name") {
            systDesc.systemName = ssd.second.data();
        }
        else if (attr_key == "version") {
            systDesc.systemVersion = ssd.second.data();
        }

    }

    std::cout << systDesc.systemName << std::endl << systDesc.systemVersion << std::endl;


    boost::property_tree::ptree system = pt_.get_child("ssd:SystemStructureDescription.ssd:System");

    for (const auto& kv : system.get_child("ssd:SimulationInformation")) {
        if (kv.first == "ssd:FixedStepMaster") {
            // Set up algo
        }
    }

    for (const auto& elem : system.get_child("ssd:Elements")) {
        std::cout << elem.first << std::endl;
        for (const auto& connector : elem.second.get_child("ssd:Connectors")) {
            std::cout << connector.first << std::endl;
        }
    }

}

ssp_parser::~ssp_parser() noexcept = default;

} // namespace cse