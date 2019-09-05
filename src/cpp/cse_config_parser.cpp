#include "cse/cse_config_parser.hpp"

#include "cse/algorithm.hpp"
#include "cse/fmi/fmu.hpp"

#include <boost/lexical_cast.hpp>
#include <gsl/gsl_util>
#include <nlohmann/json.hpp>
#include <xercesc/dom/DOM.hpp>
#include <xercesc/util/PlatformUtils.hpp>
#include <xercesc/util/XMLString.hpp>

#include <string>


namespace cse
{

namespace
{

class cse_config_parser
{

public:
    explicit cse_config_parser(const boost::filesystem::path& configPath);
    ~cse_config_parser() noexcept;

    struct SimulationInformation
    {
        std::string description;
        double stepSize = 0.1;
        double startTime = 0.0;
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

    struct Simulator
    {
        std::string name;
        std::string source;
        int decimationFactor;
    };

    const std::vector<Simulator>& get_elements() const;

    struct Connection
    {
        std::string sourceSimulator;
        std::string sourceConnector;
        std::string targetSimulator;
        std::string targetConnector;
    };

    const std::vector<Connection>& get_variable_connections() const;
    const std::vector<Connection>& get_plug_socket_connections() const;
    const std::vector<Connection>& get_bond_connections() const;

private:
    SystemDescription systemDescription_;
    SimulationInformation simulationInformation_;
    std::vector<Simulator> simulators_;
    std::vector<Connection> variableConnections_;
    std::vector<Connection> plugSocketConnections_;
    std::vector<Connection> bondConnections_;
};

cse_config_parser::cse_config_parser(const boost::filesystem::path& configPath)
{
    // Root node
    xercesc::XMLPlatformUtils::Initialize();
    const auto xerces_cleanup = gsl::final_action([]() {
        xercesc::XMLPlatformUtils::Terminate();
    });
    const auto domImpl = xercesc::DOMImplementationRegistry::getDOMImplementation(xercesc::XMLString::transcode("LS"));
    const auto parser = static_cast<xercesc::DOMImplementationLS*>(domImpl)->createLSParser(xercesc::DOMImplementationLS::MODE_SYNCHRONOUS, 0);
    const auto doc = parser->parseURI(configPath.string().c_str());

    //    const auto rootElement = doc->getElementsByTagName(xercesc::XMLString::transcode("OspSystemStructure"))->item(0);
    const auto rootElement = doc->getDocumentElement();

    const auto simulators = rootElement->getElementsByTagName(xercesc::XMLString::transcode("Simulators"))->item(0);
    for (XMLSize_t i = 0; i < simulators->getChildNodes()->getLength(); i++) {
        const auto simulator = simulators->getChildNodes()->item(i);

        const auto attrs = simulator->getAttributes();
        std::string name = xercesc::XMLString::transcode(attrs->getNamedItem(xercesc::XMLString::transcode("name"))->getNodeValue());
        std::string source = xercesc::XMLString::transcode(attrs->getNamedItem(xercesc::XMLString::transcode("source"))->getNodeValue());

        int decimationFactor = 1;
        if (auto decFacNode = attrs->getNamedItem(xercesc::XMLString::transcode("decimationFactor"))) {
            decimationFactor = boost::lexical_cast<int>(decFacNode->getNodeValue());
            //TODO: Possibly obsolete with schema?
        }
        //TODO: Initial values
        simulators_.push_back({name, source, decimationFactor});
    }

    auto descNodes = rootElement->getElementsByTagName(xercesc::XMLString::transcode("Description"));
    if (descNodes->getLength() > 0) {
        simulationInformation_.description = xercesc::XMLString::transcode(descNodes->item(0)->getTextContent());
    }

    auto ssNodes = rootElement->getElementsByTagName(xercesc::XMLString::transcode("BaseStepSize"));
    if (ssNodes->getLength() > 0) {
        simulationInformation_.stepSize = boost::lexical_cast<double>(ssNodes->item(0)->getTextContent());
    }

    auto stNodes = rootElement->getElementsByTagName(xercesc::XMLString::transcode("StartTime"));
    if (stNodes->getLength() > 0) {
        simulationInformation_.startTime = boost::lexical_cast<double>(stNodes->item(0)->getTextContent());
    }

    auto varConns = rootElement->getElementsByTagName(xercesc::XMLString::transcode("VariableConnections"));
    for (XMLSize_t i = 0; i < varConns->getLength(); i++) {
        auto varConn = varConns->item(i);
        const auto attrs = varConn->getAttributes();
        std::string sourceSimulator = xercesc::XMLString::transcode(attrs->getNamedItem(xercesc::XMLString::transcode("sourceSimulator"))->getNodeValue());
        std::string sourceVariable = xercesc::XMLString::transcode(attrs->getNamedItem(xercesc::XMLString::transcode("sourceVariable"))->getNodeValue());
        std::string targetSimulator = xercesc::XMLString::transcode(attrs->getNamedItem(xercesc::XMLString::transcode("targetSimulator"))->getNodeValue());
        std::string targetVariable = xercesc::XMLString::transcode(attrs->getNamedItem(xercesc::XMLString::transcode("targetVariable"))->getNodeValue());

        variableConnections_.push_back({sourceSimulator, sourceVariable,
                                        targetSimulator, targetVariable});
    }

    auto plugSocketConns = rootElement->getElementsByTagName(xercesc::XMLString::transcode("PlugSocketConnections"));
    for (XMLSize_t i = 0; i < plugSocketConns->getLength(); i++) {
        auto plugSocketConn = plugSocketConns->item(i);
        const auto attrs = plugSocketConn->getAttributes();
        std::string sourceSimulator = xercesc::XMLString::transcode(attrs->getNamedItem(xercesc::XMLString::transcode("sourceSimulator"))->getNodeValue());
        std::string plug = xercesc::XMLString::transcode(attrs->getNamedItem(xercesc::XMLString::transcode("plug"))->getNodeValue());
        std::string targetSimulator = xercesc::XMLString::transcode(attrs->getNamedItem(xercesc::XMLString::transcode("targetSimulator"))->getNodeValue());
        std::string socket = xercesc::XMLString::transcode(attrs->getNamedItem(xercesc::XMLString::transcode("socket"))->getNodeValue());

        plugSocketConnections_.push_back({sourceSimulator, plug,
                                        targetSimulator, socket});
    }

    auto bondConns = rootElement->getElementsByTagName(xercesc::XMLString::transcode("BondConnections"));
    for (XMLSize_t i = 0; i < bondConns->getLength(); i++) {
        auto bondConn = bondConns->item(i);
        const auto attrs = bondConn->getAttributes();
        std::string simulatorA = xercesc::XMLString::transcode(attrs->getNamedItem(xercesc::XMLString::transcode("simulatorA"))->getNodeValue());
        std::string bondA = xercesc::XMLString::transcode(attrs->getNamedItem(xercesc::XMLString::transcode("bondA"))->getNodeValue());
        std::string simulatorB = xercesc::XMLString::transcode(attrs->getNamedItem(xercesc::XMLString::transcode("simulatorB"))->getNodeValue());
        std::string bondB = xercesc::XMLString::transcode(attrs->getNamedItem(xercesc::XMLString::transcode("bondB"))->getNodeValue());

        bondConnections_.push_back({simulatorA, bondA,
                                          simulatorB, bondB});
    }
}

cse_config_parser::~cse_config_parser() noexcept = default;

const cse_config_parser::SimulationInformation& cse_config_parser::get_simulation_information() const
{
    return simulationInformation_;
}

const std::vector<cse_config_parser::Simulator>& cse_config_parser::get_elements() const
{
    return simulators_;
}

const std::vector<cse_config_parser::Connection>& cse_config_parser::get_variable_connections() const
{
    return variableConnections_;
}

const std::vector<cse_config_parser::Connection>& cse_config_parser::get_plug_socket_connections() const
{
    return plugSocketConnections_;
}

const std::vector<cse_config_parser::Connection>& cse_config_parser::get_bond_connections() const
{
    return bondConnections_;
}

std::vector<std::string> parse_string_vector(const nlohmann::json& element)
{
    std::vector<std::string> v;
    for (const auto& child : element) {
        v.emplace_back(child.get<std::string>());
    }

    return v;
}

struct slave_info
{
    cse::simulator_index index;
    std::map<std::string, cse::variable_description> variables;
};


struct plug_socket_description
{
    std::string type;
    std::vector<std::string> variables;
};

struct bond_description
{
    std::vector<std::string> plugs;
    std::vector<std::string> sockets;
};

struct extended_model_description
{

    explicit extended_model_description(const boost::filesystem::path& msmiJson)
    {
        boost::filesystem::ifstream i(msmiJson);
        nlohmann::json j;
        i >> j;

        for (const auto& plug : j.at("plugs")) {
            plug_socket_description p;
            const auto name = plug.at("name").get<std::string>();
            p.type = plug.at("type").get<std::string>();
            p.variables = parse_string_vector(plug.at("variables"));
            plugs[name] = std::move(p);
        }

        for (const auto& socket : j.at("sockets")) {
            plug_socket_description s;
            const auto name = socket.at("name").get<std::string>();
            s.type = socket.at("type").get<std::string>();
            s.variables = parse_string_vector(socket.at("variables"));
            sockets[name] = std::move(s);
        }

        for (const auto& bond : j.at("bonds")) {
            bond_description b;
            const auto name = bond.at("name").get<std::string>();
            b.plugs = parse_string_vector(bond.at("plugs"));
            b.sockets = parse_string_vector(bond.at("sockets"));
            bonds[name] = std::move(b);
        }
    }

    std::unordered_map<std::string, plug_socket_description> plugs;
    std::unordered_map<std::string, plug_socket_description> sockets;
    std::unordered_map<std::string, bond_description> bonds;
};

} // namespace

void connect_variables(
    std::vector<cse_config_parser::Connection> variableConnections,
    std::unordered_map<std::string, slave_info>& slaves,
    cse::execution& execution)
{

    for (const auto& connection : variableConnections) {
        cse::variable_id output = {slaves[connection.sourceSimulator].index,
            slaves[connection.sourceSimulator].variables[connection.sourceConnector].type,
            slaves[connection.sourceSimulator].variables[connection.sourceConnector].index};

        cse::variable_id input = {slaves[connection.targetSimulator].index,
            slaves[connection.targetSimulator].variables[connection.targetConnector].type,
            slaves[connection.targetSimulator].variables[connection.targetConnector].index};

        auto c = std::make_shared<cse::scalar_connection>(output, input);
        execution.add_connection(c);
    }
}

void connect_plugs_and_sockets(
    std::vector<cse_config_parser::Connection> plugSocketConnections,
    std::unordered_map<std::string, slave_info>& slaves,
    cse::execution& execution,
    std::unordered_map<std::string, extended_model_description> emds)
{

    for (const auto& connection : plugSocketConnections) {
        const auto& plugVariables = emds.at(connection.sourceSimulator).plugs.at(connection.sourceConnector).variables;
        const auto& socketVariables = emds.at(connection.targetSimulator).sockets.at(connection.targetConnector).variables;

        assert(plugVariables.size() == socketVariables.size());

        std::vector<cse_config_parser::Connection> variableConnections;

        for (std::size_t i = 0; i < plugVariables.size(); ++i) {
            variableConnections.push_back({connection.sourceSimulator,
                plugVariables.at(i),
                connection.targetSimulator,
                socketVariables.at(i)});
        }

        connect_variables(variableConnections, slaves, execution);
    }
}

void connect_bonds(
    std::vector<cse_config_parser::Connection> bondConnections,
    std::unordered_map<std::string, slave_info>& slaves,
    cse::execution& execution,
    std::unordered_map<std::string, extended_model_description> emds)
{

    for (const auto& connection : bondConnections) {
        auto const& bondAPlugs = emds.at(connection.sourceSimulator).bonds.at(connection.sourceConnector).plugs;
        auto const& bondBSockets = emds.at(connection.targetSimulator).bonds.at(connection.targetConnector).sockets;
        assert(bondAPlugs.size() == bondBSockets.size());
        std::vector<cse_config_parser::Connection> plugSocketConnectionsA;
        for (std::size_t i = 0; i < bondAPlugs.size(); ++i) {
            plugSocketConnectionsA.push_back({connection.sourceSimulator,
                bondAPlugs.at(i),
                connection.targetSimulator,
                bondBSockets.at(i)});
        }
        connect_plugs_and_sockets(plugSocketConnectionsA, slaves, execution, emds);

        auto const& bondASockets = emds.at(connection.sourceSimulator).bonds.at(connection.sourceConnector).sockets;
        auto const& bondBPlugs = emds.at(connection.targetSimulator).bonds.at(connection.targetConnector).plugs;
        assert(bondASockets.size() == bondBPlugs.size());
        std::vector<cse_config_parser::Connection> plugSocketConnectionsB;
        for (std::size_t i = 0; i < bondASockets.size(); ++i) {
            plugSocketConnectionsB.push_back({connection.targetSimulator,
                bondBPlugs.at(i),
                connection.sourceSimulator,
                bondASockets.at(i)});
        }
        connect_plugs_and_sockets(plugSocketConnectionsB, slaves, execution, emds);
    }
}

std::pair<execution, simulator_map> load_cse_config(
    cse::model_uri_resolver& resolver,
    const boost::filesystem::path& configPath,
    std::optional<cse::time_point> overrideStartTime)
{
    simulator_map simulatorMap;
    const auto configFile = configPath / "SystemStructure.xml";
    const auto baseURI = path_to_file_uri(configFile);
    const auto parser = cse_config_parser(configFile);

    const auto& simInfo = parser.get_simulation_information();
    const cse::duration stepSize = cse::to_duration(simInfo.stepSize);

    auto simulators = parser.get_elements();

    const auto startTime = overrideStartTime ? *overrideStartTime : cse::to_time_point(simInfo.startTime);

    auto execution = cse::execution(
        startTime,
        std::make_unique<cse::fixed_step_algorithm>(stepSize));

    std::unordered_map<std::string, slave_info> slaves;
    std::unordered_map<std::string, extended_model_description> emds;
    for (const auto& simulator : simulators) {
        auto model = resolver.lookup_model(baseURI, simulator.source);
        auto slave = model->instantiate(simulator.name);
        simulator_index index = slaves[simulator.name].index = execution.add_slave(slave, simulator.name);

        simulatorMap[simulator.name] = simulator_map_entry{index, simulator.source, *model->description()};

        for (const auto& v : model->description()->variables) {
            slaves[simulator.name].variables[v.name] = v;
        }

        std::string msmiFileName = model->description()->name + ".xml";
        const auto msmiFilePath = configPath / msmiFileName;
        emds.emplace(simulator.name, msmiFilePath);
    }

    connect_variables(parser.get_variable_connections(), slaves, execution);
    connect_plugs_and_sockets(parser.get_plug_socket_connections(), slaves, execution, emds);
    connect_bonds(parser.get_bond_connections(), slaves, execution, emds);

    return std::make_pair(std::move(execution), std::move(simulatorMap));
}

} // namespace cse
