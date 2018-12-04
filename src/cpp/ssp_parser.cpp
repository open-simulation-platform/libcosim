#include <cse/ssp_parser.hpp>

#include <string>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <cse/algorithm.hpp>
#include <cse/fmi/fmu.hpp>
#include <cse/fmi/importer.hpp>


namespace cse
{

namespace
{
template<typename T>
T get_attribute(const boost::property_tree::ptree& tree, const std::string& key)
{
    return tree.get<T>("<xmlattr>." + key);
}


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

    const DefaultExperiment& get_default_experiment() const;

    struct SimulationInformation
    {
        std::string description;
        double stepSize;
    };

    const SimulationInformation& get_simulation_information() const;

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

    const std::vector<Component>& get_elements() const;

    struct Connection
    {
        std::string startElement;
        std::string startConnector;
        std::string endElement;
        std::string endConnector;
    };

    const std::vector<Connection>& get_connections() const;

private:
    boost::filesystem::path xmlPath_;
    boost::property_tree::ptree pt_;

    SystemDescription systemDescription_;
    DefaultExperiment defaultExperiment_;
    SimulationInformation simulationInformation_;
    std::vector<Component> elements_;
    std::vector<Connection> connections_;
};

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
        auto& e = elements_.emplace_back();
        e.name = get_attribute<std::string>(component.second, "name");
        e.source = get_attribute<std::string>(component.second, "source");

        for (const auto& connector : component.second.get_child("ssd:Connectors")) {
            if (connector.first == "ssd::Connector") {
                auto &c = e.connectors.emplace_back();
                c.name = get_attribute<std::string>(connector.second, "name");
                c.kind = get_attribute<std::string>(connector.second, "kind");
                c.type = get_attribute<std::string>(connector.second, "type");
            }
        }
    }

    for (const auto& connection : tmpTree.get_child("ssd:Connections")) {
        auto& c = connections_.emplace_back();
        c.startElement = get_attribute<std::string>(connection.second, "startElement");
        c.startConnector = get_attribute<std::string>(connection.second, "startConnector");
        c.endElement = get_attribute<std::string>(connection.second, "endElement");
        c.endConnector = get_attribute<std::string>(connection.second, "endConnector");
    }
}

ssp_parser::~ssp_parser() noexcept = default;

const ssp_parser::SimulationInformation& ssp_parser::get_simulation_information() const
{
    return simulationInformation_;
}

const ssp_parser::DefaultExperiment& ssp_parser::get_default_experiment() const
{
    return defaultExperiment_;
}

const std::vector<ssp_parser::Component>& ssp_parser::get_elements() const
{
    return elements_;
}

const std::vector<ssp_parser::Connection>& ssp_parser::get_connections() const
{
    return connections_;
}


struct slave_info
{
    cse::simulator_index index;
    std::map<std::string, cse::variable_description> variables;
};

} // namespace

execution load_ssp(const boost::filesystem::path& sspDir, cse::time_point startTime)
{
    const auto parser = cse::ssp_parser(sspDir / "SystemStructure.ssd");

    const auto& simInfo = parser.get_simulation_information();
    const cse::duration stepSize = cse::to_duration(simInfo.stepSize);

    std::map<std::string, slave_info> slaves;

    auto elems = parser.get_elements();
    for (const auto& comp : elems) {
        std::cout << "Name:" << comp.name << std::endl;
        std::cout << "Source: " << comp.source << std::endl;
    }

    // Set up execution
    auto execution = cse::execution(
        startTime,
        std::make_unique<cse::fixed_step_algorithm>(stepSize));

    auto importer = cse::fmi::importer::create();
    for (const auto& component : elems) {
        auto fmu = importer->import(sspDir / component.source);
        auto slave = fmu->instantiate_slave(component.name);
        slaves[component.name].index = execution.add_slave(cse::make_pseudo_async(slave), component.name);
        for (const auto& v : fmu->model_description()->variables) {
            slaves[component.name].variables[v.name] = v;
        }
    }

    for (const auto& connection : parser.get_connections()) {
        cse::variable_id output = {slaves[connection.startElement].index,
            slaves[connection.startElement].variables[connection.startConnector].type,
            slaves[connection.startElement].variables[connection.startConnector].index};

        cse::variable_id input = {slaves[connection.endElement].index,
            slaves[connection.endElement].variables[connection.endConnector].type,
            slaves[connection.endElement].variables[connection.endConnector].index};

        execution.connect_variables(output, input);
    }

    return execution;
}


} // namespace cse
