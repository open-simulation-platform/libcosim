#include <cse/ssp_parser.hpp>

namespace cse
{

namespace
{
template <typename T>
T get_attribute(const boost::property_tree::ptree& tree, const std::string& key)
{
    return tree.get<T>("<xmlattr>." + key);
}
}

ssp_parser::ssp_parser(boost::filesystem::path xmlPath)
    : xmlPath_(xmlPath)
{
    // Root node
    std::string path = "ssd:SystemStructureDescription";

    boost::property_tree::ptree tmpTree;
    boost::property_tree::read_xml(xmlPath.string(), pt_);

    tmpTree = pt_.get_child(path);
    systemDescription_.name = get_attribute<std::string>(tmpTree, "name");
    systemDescription_.version = get_attribute<std::string>(tmpTree, "version");

    const auto& defaultExperiment = tmpTree.get_child("ssd:DefaultExperiment");
    defaultExperiment_.startTime = get_attribute<double>(defaultExperiment, "startTime");
    defaultExperiment_.stopTime = get_attribute<double>(defaultExperiment, "stopTime");

    path += ".ssd:System";
    tmpTree = pt_.get_child(path);

    systemDescription_.systemName = get_attribute<std::string>(tmpTree, "name");
    systemDescription_.systemDescription = get_attribute<std::string>(tmpTree, "description");

    for (const auto& infos : tmpTree.get_child("ssd:SimulationInformation")) {
        if (infos.first == "ssd:FixedStepMaster") {
            simulationInformation_.description = get_attribute<std::string>(infos.second, "description");
            simulationInformation_.stepSize = get_attribute<double>(infos.second, "stepSize");
        }
    }

    for (const auto& component : tmpTree.get_child("ssd:Elements")) {
        elements_.emplace_back();
        elements_.back().name = get_attribute<std::string>(component.second, "name");
        elements_.back().source = get_attribute<std::string>(component.second, "source");

        for (const auto& connector : component.second.get_child("ssd:Connectors")) {
            std::cout << connector.first << std::endl;
        }
    }

    for (const auto& connection : tmpTree.get_child("ssd:Connections")) {
        connections_.emplace_back();
        connections_.back().startElement = get_attribute<std::string>(connection.second, "startElement");
        connections_.back().startConnector = get_attribute<std::string>(connection.second, "startConnector");
        connections_.back().endElement = get_attribute<std::string>(connection.second, "endElement");
        connections_.back().endConnector = get_attribute<std::string>(connection.second, "endConnector");
    }
}

ssp_parser::~ssp_parser() noexcept = default;

    const ssp_parser::SimulationInformation &ssp_parser::get_simulation_information() const {
        return simulationInformation_;
    }

    const ssp_parser::DefaultExperiment &ssp_parser::get_default_experiment() const {
        return defaultExperiment_;
    }

    const std::vector<ssp_parser::Component> &ssp_parser::get_elements() const {
        return elements_;
    }

    const std::vector<ssp_parser::Connection> &ssp_parser::get_connections() const {
        return connections_;
}

} // namespace cse
