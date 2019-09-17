#include "cse/cse_config_parser.hpp"

#include "cse/algorithm.hpp"
#include "cse/fmi/fmu.hpp"

#include <boost/lexical_cast.hpp>
#include <gsl/gsl_util>
#include <xercesc/dom/DOM.hpp>
#include <xercesc/util/PlatformUtils.hpp>
#include <xercesc/util/XMLString.hpp>

#include <memory>
#include <string>


namespace cse
{

namespace
{
std::shared_ptr<XMLCh> tc(const char* str)
{
    return std::shared_ptr<XMLCh>(
        xercesc::XMLString::transcode(str),
        [](XMLCh* ptr) { xercesc::XMLString::release(&ptr); });
}
std::shared_ptr<char> tc(const XMLCh* str)
{
    return std::shared_ptr<char>(
        xercesc::XMLString::transcode(str),
        [](char* ptr) { xercesc::XMLString::release(&ptr); });
}

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

    struct ScalarConnection
    {
        std::string sourceSimulator;
        std::string sourceConnector;
        std::string targetSimulator;
        std::string targetConnector;
    };

    struct SumConnection
    {
        std::vector<std::pair<std::string, std::string>> sources;
        std::pair<std::string, std::string> target;
    };

    const std::vector<ScalarConnection>& get_scalar_connections() const;
    const std::vector<SumConnection>& get_sum_connections() const;
    const std::vector<ScalarConnection>& get_plug_socket_connections() const;
    const std::vector<ScalarConnection>& get_bond_connections() const;

private:
    SystemDescription systemDescription_;
    SimulationInformation simulationInformation_;
    std::vector<Simulator> simulators_;
    std::vector<SumConnection> sumConnections_;
    std::vector<ScalarConnection> scalarConnections_;
    std::vector<ScalarConnection> plugSocketConnections_;
    std::vector<ScalarConnection> bondConnections_;
};

cse_config_parser::cse_config_parser(const boost::filesystem::path& configPath)
{
    // Root node
    xercesc::XMLPlatformUtils::Initialize();
    const auto xerces_cleanup = gsl::final_action([]() {
        xercesc::XMLPlatformUtils::Terminate();
    });
    const auto domImpl = xercesc::DOMImplementationRegistry::getDOMImplementation(tc("LS").get());
    const auto parser = static_cast<xercesc::DOMImplementationLS*>(domImpl)->createLSParser(xercesc::DOMImplementationLS::MODE_SYNCHRONOUS, 0);
    const auto doc = parser->parseURI(configPath.string().c_str());

    //    const auto rootElement = doc->getElementsByTagName(tc("OspSystemStructure"))->item(0);
    const auto rootElement = doc->getDocumentElement();

    const auto simulators = static_cast<xercesc::DOMElement*>(rootElement->getElementsByTagName(tc("Simulators").get())->item(0));
    for (auto element = simulators->getFirstElementChild(); element != nullptr; element = element->getNextElementSibling()) {
        std::string name = tc(element->getAttribute(tc("name").get())).get();
        std::string source = tc(element->getAttribute(tc("source").get())).get();

        int decimationFactor = 1;
        auto decFacNode = tc(element->getAttribute(tc("decimationFactor").get()));
        if (*decFacNode) {
            decimationFactor = boost::lexical_cast<int>(decFacNode);
            //TODO: Possibly obsolete with schema?
        }
        //TODO: Initial values
        simulators_.push_back({name, source, decimationFactor});
    }

    auto descNodes = rootElement->getElementsByTagName(tc("Description").get());
    if (descNodes->getLength() > 0) {
        simulationInformation_.description = tc(descNodes->item(0)->getTextContent()).get();
    }

    auto ssNodes = rootElement->getElementsByTagName(tc("BaseStepSize").get());
    if (ssNodes->getLength() > 0) {
        simulationInformation_.stepSize = boost::lexical_cast<double>(tc(ssNodes->item(0)->getTextContent()));
    }

    auto stNodes = rootElement->getElementsByTagName(tc("StartTime").get());
    if (stNodes->getLength() > 0) {
        simulationInformation_.startTime = boost::lexical_cast<double>(tc(stNodes->item(0)->getTextContent()).get());
    }

    auto varConns = static_cast<xercesc::DOMElement*>(rootElement->getElementsByTagName(tc("VariableConnections").get())->item(0));

    auto scalarConnections = varConns->getElementsByTagName(tc("ScalarConnection").get());
    for (size_t i = 0; i < scalarConnections->getLength(); i++) {
        auto scalarConnection = static_cast<xercesc::DOMElement*>(scalarConnections->item(i));
        auto source = static_cast<xercesc::DOMElement*>(scalarConnection->getElementsByTagName(tc("Source").get())->item(0));
        auto target = static_cast<xercesc::DOMElement*>(scalarConnection->getElementsByTagName(tc("Target").get())->item(0));
        std::string sourceSimulator = tc(source->getAttribute(tc("simulator").get())).get();
        std::string sourceVariable = tc(source->getAttribute(tc("endpoint").get())).get();
        std::string targetSimulator = tc(target->getAttribute(tc("simulator").get())).get();
        std::string targetVariable = tc(target->getAttribute(tc("endpoint").get())).get();

        scalarConnections_.push_back({sourceSimulator, sourceVariable,
            targetSimulator, targetVariable});
    }
    auto sumConnections = varConns->getElementsByTagName(tc("SumConnection").get());
    for (size_t i = 0; i < sumConnections->getLength(); i++) {
        SumConnection s;
        auto sumConnection = static_cast<xercesc::DOMElement*>(sumConnections->item(i));
        auto sources = static_cast<xercesc::DOMElement*>(sumConnection->getElementsByTagName(tc("Source").get())->item(0));
        for (auto sourceElement = sources->getFirstElementChild(); sourceElement != nullptr; sourceElement = sourceElement->getNextElementSibling()) {
            auto& source = s.sources.emplace_back();
            source.first = tc(sourceElement->getAttribute(tc("simulator").get())).get();
            source.second = tc(sourceElement->getAttribute(tc("endpoint").get())).get();
        }
        auto target = static_cast<xercesc::DOMElement*>(sumConnection->getElementsByTagName(tc("Target").get())->item(0));
        std::string targetSimulator = tc(target->getAttribute(tc("simulator").get())).get();
        std::string targetVariable = tc(target->getAttribute(tc("endpoint").get())).get();
        s.target = {targetSimulator, targetVariable};

        sumConnections_.push_back(std::move(s));
    }

    auto plugSocketConns = static_cast<xercesc::DOMElement*>(rootElement->getElementsByTagName(tc("PlugSocketConnections").get())->item(0));
    for (auto element = plugSocketConns->getFirstElementChild(); element != nullptr; element = element->getNextElementSibling()) {
        auto source = static_cast<xercesc::DOMElement*>(element->getElementsByTagName(tc("Source").get())->item(0));
        auto target = static_cast<xercesc::DOMElement*>(element->getElementsByTagName(tc("Target").get())->item(0));
        std::string sourceSimulator = tc(source->getAttribute(tc("simulator").get())).get();
        std::string plug = tc(source->getAttribute(tc("endpoint").get())).get();
        std::string targetSimulator = tc(target->getAttribute(tc("simulator").get())).get();
        std::string socket = tc(target->getAttribute(tc("endpoint").get())).get();

        plugSocketConnections_.push_back({sourceSimulator, plug, targetSimulator, socket});
    }

    auto bondConns = static_cast<xercesc::DOMElement*>(rootElement->getElementsByTagName(tc("BondConnections").get())->item(0));
    for (auto element = bondConns->getFirstElementChild(); element != nullptr; element = element->getNextElementSibling()) {
        auto bonds = element->getElementsByTagName(tc("Bond").get());
        auto bondA = static_cast<xercesc::DOMElement*>(bonds->item(0));
        auto bondB = static_cast<xercesc::DOMElement*>(bonds->item(1));

        std::string simulatorA = tc(bondA->getAttribute(tc("simulator").get())).get();
        std::string bondNameA = tc(bondA->getAttribute(tc("endpoint").get())).get();
        std::string simulatorB = tc(bondB->getAttribute(tc("simulator").get())).get();
        std::string bondNameB = tc(bondB->getAttribute(tc("endpoint").get())).get();

        bondConnections_.push_back({simulatorA, bondNameA, simulatorB, bondNameB});
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

const std::vector<cse_config_parser::ScalarConnection>& cse_config_parser::get_scalar_connections() const
{
    return scalarConnections_;
}

const std::vector<cse_config_parser::SumConnection>& cse_config_parser::get_sum_connections() const
{
    return sumConnections_;
}

const std::vector<cse_config_parser::ScalarConnection>& cse_config_parser::get_plug_socket_connections() const
{
    return plugSocketConnections_;
}

const std::vector<cse_config_parser::ScalarConnection>& cse_config_parser::get_bond_connections() const
{
    return bondConnections_;
}

struct slave_info
{
    cse::simulator_index index;
    std::map<std::string, cse::variable_description> variables;
};


struct plug_socket_description
{
    std::string name;
    std::string type;
    std::vector<std::string> variables;
};

struct bond_description
{
    std::string name;
    std::vector<std::string> plugs;
    std::vector<std::string> sockets;
};

struct extended_model_description
{

    explicit extended_model_description(const boost::filesystem::path& ospModelDescription)
    {

        xercesc::XMLPlatformUtils::Initialize();
        const auto xerces_cleanup = gsl::final_action([]() {
            xercesc::XMLPlatformUtils::Terminate();
        });
        const auto domImpl = xercesc::DOMImplementationRegistry::getDOMImplementation(tc("LS").get());
        const auto parser = static_cast<xercesc::DOMImplementationLS*>(domImpl)->createLSParser(xercesc::DOMImplementationLS::MODE_SYNCHRONOUS, 0);
        const auto doc = parser->parseURI(ospModelDescription.string().c_str());
        // TODO: Check return value for null

        const auto rootElement = doc->getDocumentElement();

        const auto socketsElement = static_cast<xercesc::DOMElement*>(rootElement->getElementsByTagName(tc("Sockets").get())->item(0));

        for (auto socketElement = socketsElement->getFirstElementChild(); socketElement != nullptr; socketElement = socketElement->getNextElementSibling()) {
            plug_socket_description s;
            s.name = tc(socketElement->getAttribute(tc("name").get())).get();
            s.type = tc(socketElement->getAttribute(tc("type").get())).get();
            auto variables = static_cast<xercesc::DOMElement*>(socketElement->getElementsByTagName(tc("Variables").get())->item(0));
            for (auto variableElement = variables->getFirstElementChild(); variableElement != nullptr; variableElement = variableElement->getNextElementSibling()) {
                std::string variableName = tc(variableElement->getAttribute(tc("name").get())).get();
                s.variables.push_back(variableName);
            }
            sockets[s.name] = std::move(s);
        }

        const auto plugsElement = static_cast<xercesc::DOMElement*>(rootElement->getElementsByTagName(tc("Plugs").get())->item(0));

        for (auto plugElement = plugsElement->getFirstElementChild(); plugElement != nullptr; plugElement = plugElement->getNextElementSibling()) {
            plug_socket_description p;
            p.name = tc(plugElement->getAttribute(tc("name").get())).get();
            p.type = tc(plugElement->getAttribute(tc("type").get())).get();
            auto variables = static_cast<xercesc::DOMElement*>(plugElement->getElementsByTagName(tc("Variables").get())->item(0));
            for (auto variableElement = variables->getFirstElementChild(); variableElement != nullptr; variableElement = variableElement->getNextElementSibling()) {
                std::string variableName = tc(variableElement->getAttribute(tc("name").get())).get();
                p.variables.push_back(variableName);
            }
            plugs[p.name] = std::move(p);
        }

        const auto bondsElement = static_cast<xercesc::DOMElement*>(rootElement->getElementsByTagName(tc("Bonds").get())->item(0));

        for (auto bondElement = bondsElement->getFirstElementChild(); bondElement != nullptr; bondElement = bondElement->getNextElementSibling()) {
            bond_description b;
            b.name = tc(bondElement->getAttribute(tc("name").get())).get();
            auto bondSockets = static_cast<xercesc::DOMElement*>(bondElement->getElementsByTagName(tc("BondSockets").get())->item(0));
            for (auto bondSocketElement = bondSockets->getFirstElementChild(); bondSocketElement != nullptr; bondSocketElement = bondSocketElement->getNextElementSibling()) {
                std::string bondSocketName = tc(bondSocketElement->getAttribute(tc("name").get())).get();
                b.sockets.push_back(bondSocketName);
            }
            auto bondPlugs = static_cast<xercesc::DOMElement*>(bondElement->getElementsByTagName(tc("BondPlugs").get())->item(0));
            for (auto bondPlugElement = bondPlugs->getFirstElementChild(); bondPlugElement != nullptr; bondPlugElement = bondPlugElement->getNextElementSibling()) {
                std::string bondPlugName = tc(bondPlugElement->getAttribute(tc("name").get())).get();
                b.plugs.push_back(bondPlugName);
            }
            bonds[b.name] = std::move(b);
        }
    }

    std::unordered_map<std::string, plug_socket_description> plugs;
    std::unordered_map<std::string, plug_socket_description> sockets;
    std::unordered_map<std::string, bond_description> bonds;
};

} // namespace

void connect_variables(
    const std::vector<cse_config_parser::ScalarConnection>& variableConnections,
    const std::unordered_map<std::string, slave_info>& slaves,
    cse::execution& execution)
{

    for (const auto& connection : variableConnections) {
        cse::variable_id output = {slaves.at(connection.sourceSimulator).index,
            slaves.at(connection.sourceSimulator).variables.at(connection.sourceConnector).type,
            slaves.at(connection.sourceSimulator).variables.at(connection.sourceConnector).index};

        cse::variable_id input = {slaves.at(connection.targetSimulator).index,
            slaves.at(connection.targetSimulator).variables.at(connection.targetConnector).type,
            slaves.at(connection.targetSimulator).variables.at(connection.targetConnector).index};

        auto c = std::make_shared<cse::scalar_connection>(output, input);
        execution.add_connection(c);
    }
}

void connect_plugs_and_sockets(
    const std::vector<cse_config_parser::ScalarConnection>& plugSocketConnections,
    const std::unordered_map<std::string, slave_info>& slaves,
    cse::execution& execution,
    const std::unordered_map<std::string, extended_model_description>& emds)
{

    for (const auto& connection : plugSocketConnections) {
        const auto& plugVariables = emds.at(connection.sourceSimulator).plugs.at(connection.sourceConnector).variables;
        const auto& socketVariables = emds.at(connection.targetSimulator).sockets.at(connection.targetConnector).variables;

        assert(plugVariables.size() == socketVariables.size());

        std::vector<cse_config_parser::ScalarConnection> variableConnections;

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
    const std::vector<cse_config_parser::ScalarConnection>& bondConnections,
    const std::unordered_map<std::string, slave_info>& slaves,
    cse::execution& execution,
    const std::unordered_map<std::string, extended_model_description>& emds)
{

    for (const auto& connection : bondConnections) {
        auto const& bondAPlugs = emds.at(connection.sourceSimulator).bonds.at(connection.sourceConnector).plugs;
        auto const& bondBSockets = emds.at(connection.targetSimulator).bonds.at(connection.targetConnector).sockets;
        assert(bondAPlugs.size() == bondBSockets.size());
        std::vector<cse_config_parser::ScalarConnection> plugSocketConnectionsA;
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
        std::vector<cse_config_parser::ScalarConnection> plugSocketConnectionsB;
        for (std::size_t i = 0; i < bondASockets.size(); ++i) {
            plugSocketConnectionsB.push_back({connection.targetSimulator,
                bondBPlugs.at(i),
                connection.sourceSimulator,
                bondASockets.at(i)});
        }
        connect_plugs_and_sockets(plugSocketConnectionsB, slaves, execution, emds);
    }
}

void connect_sum(
    const std::vector<cse_config_parser::SumConnection>& sumConnections,
    const std::unordered_map<std::string, slave_info>& slaves,
    cse::execution& execution)
{
    for (const auto& sumConnection : sumConnections) {
        std::vector<variable_id> sources;
        for (const auto& [sim, var] : sumConnection.sources) {
            sources.push_back({slaves.at(sim).index,
                slaves.at(sim).variables.at(var).type,
                slaves.at(sim).variables.at(var).index});
        }
        auto target = variable_id{slaves.at(sumConnection.target.first).index,
            slaves.at(sumConnection.target.first).variables.at(sumConnection.target.second).type,
            slaves.at(sumConnection.target.first).variables.at(sumConnection.target.second).index};
        auto c = std::make_shared<sum_connection>(sources, target);
        execution.add_connection(c);
    }
}

std::pair<execution, simulator_map> load_cse_config(
    cse::model_uri_resolver& resolver,
    const boost::filesystem::path& configPath,
    std::optional<cse::time_point> overrideStartTime)
{
    simulator_map simulatorMap;
    const auto configFile = boost::filesystem::is_regular_file(configPath)
        ? configPath
        : configPath / "SystemStructure.xml";
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
        const auto msmiFilePath = configFile.parent_path() / msmiFileName;
        emds.emplace(simulator.name, msmiFilePath);
    }

    connect_variables(parser.get_scalar_connections(), slaves, execution);
    connect_sum(parser.get_sum_connections(), slaves, execution);
    connect_plugs_and_sockets(parser.get_plug_socket_connections(), slaves, execution, emds);
    connect_bonds(parser.get_bond_connections(), slaves, execution, emds);

    return std::make_pair(std::move(execution), std::move(simulatorMap));
}

} // namespace cse
