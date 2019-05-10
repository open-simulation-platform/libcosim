#include "cse/ssp_parser.hpp"

#include "cse/algorithm.hpp"
#include "cse/fmi/fmu.hpp"
#include "cse/fmi/importer.hpp"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <string>


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
    explicit ssp_parser(const boost::filesystem::path& xmlPath);
    ~ssp_parser() noexcept;

    struct DefaultExperiment
    {
        double startTime = 0.0;
        double stopTime = std::numeric_limits<double>::infinity();
    };

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

ssp_parser::ssp_parser(const boost::filesystem::path& xmlPath)
    : xmlPath_(xmlPath)
{
    // Root node
    std::string path = "ssd:SystemStructureDescription";

    boost::property_tree::ptree tmpTree;
    boost::property_tree::read_xml(xmlPath.string(), pt_,
        boost::property_tree::xml_parser::no_comments | boost::property_tree::xml_parser::trim_whitespace);

    tmpTree = pt_.get_child(path);
    systemDescription_.name = get_attribute<std::string>(tmpTree, "name");
    systemDescription_.version = get_attribute<std::string>(tmpTree, "version");

    if (const auto defaultExperiment = tmpTree.get_child_optional("ssd:DefaultExperiment")) {
        defaultExperiment_.startTime = get_attribute<double>(*defaultExperiment, "startTime");
        defaultExperiment_.stopTime = get_attribute<double>(*defaultExperiment, "stopTime");
    }


    tmpTree = pt_.get_child(path + ".ssd:System");
    systemDescription_.systemName = get_attribute<std::string>(tmpTree, "name");
    systemDescription_.systemDescription = get_attribute<std::string>(tmpTree, "description");

    if (const auto annotations = tmpTree.get_child_optional("ssd:Annotations")) {
        for (const auto& annotation : *annotations) {
            const auto& annotationType = get_attribute<std::string>(annotation.second, "type");
            if (annotationType == "org.open-simulation-platform") {
                for (const auto& infos : annotation.second.get_child("osp:SimulationInformation")) {
                    if (infos.first == "osp:FixedStepMaster") {
                        simulationInformation_.description = get_attribute<std::string>(infos.second, "description");
                        simulationInformation_.stepSize = get_attribute<double>(infos.second, "stepSize");
                    }
                }
            }
        }
    }

    for (const auto& component : tmpTree.get_child("ssd:Elements")) {

        auto& e = elements_.emplace_back();
        e.name = get_attribute<std::string>(component.second, "name");
        e.source = get_attribute<std::string>(component.second, "source");

        if (component.second.get_child_optional("ssd:Connectors")) {
            for (const auto& connector : component.second.get_child("ssd:Connectors")) {
                if (connector.first == "ssd::Connector") {
                    auto& c = e.connectors.emplace_back();
                    c.name = get_attribute<std::string>(connector.second, "name");
                    c.kind = get_attribute<std::string>(connector.second, "kind");
                    c.type = get_attribute<std::string>(connector.second, "type");
                }
            }
        }
    }

    if (tmpTree.get_child_optional("ssd:Connections")) {
        for (const auto& connection : tmpTree.get_child("ssd:Connections")) {
            auto& c = connections_.emplace_back();
            c.startElement = get_attribute<std::string>(connection.second, "startElement");
            c.startConnector = get_attribute<std::string>(connection.second, "startConnector");
            c.endElement = get_attribute<std::string>(connection.second, "endElement");
            c.endConnector = get_attribute<std::string>(connection.second, "endConnector");
        }
    }
}

ssp_parser::~ssp_parser() noexcept = default;

const ssp_parser::SimulationInformation& ssp_parser::get_simulation_information() const
{
    return simulationInformation_;
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


std::pair<execution, simulator_map> load_ssp(
    cse::model_uri_resolver& resolver,
    const boost::filesystem::path& sspDir,
    cse::time_point startTime)
{
    simulator_map simulatorMap;
    const auto ssdPath = sspDir / "SystemStructure.ssd";
    const auto baseURI = make_file_uri(ssdPath);
    const auto parser = ssp_parser(ssdPath);

    const auto& simInfo = parser.get_simulation_information();
    const cse::duration stepSize = cse::to_duration(simInfo.stepSize);

    auto elements = parser.get_elements();

    auto execution = cse::execution(
        startTime,
        std::make_unique<cse::fixed_step_algorithm>(stepSize));

    std::map<std::string, slave_info> slaves;
//    auto importer = cse::fmi::importer::create();
    for (const auto& component : elements) {
        auto model = resolver.lookup_model(baseURI, component.source);
        auto slave = model->instantiate(component.name);
        simulator_index index = slaves[component.name].index = execution.add_slave(slave, component.name);

        simulatorMap[component.name] = simulator_map_entry{index, component.source};

        for (const auto& v : model->description()->variables) {
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

    return std::make_pair(std::move(execution), std::move(simulatorMap));
}

} // namespace cse
