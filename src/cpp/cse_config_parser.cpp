#include "cse/cse_config_parser.hpp"

#include "cse_system_structure.hpp"

#include "cse/algorithm.hpp"
#include "cse/fmi/fmu.hpp"
#include <cse/log/logger.hpp>

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

class error_handler : public xercesc::DOMErrorHandler
{
public:
    error_handler()
        : failed_(false)
    {}

    bool failed() const { return failed_; }

    bool handleError(const xercesc::DOMError& e) override
    {
        bool warn(e.getSeverity() == xercesc::DOMError::DOM_SEVERITY_WARNING);

        if (!warn)
            failed_ = true;

        xercesc::DOMLocator* loc(e.getLocation());

        BOOST_LOG_SEV(log::logger(), warn ? log::warning : log::error)
            << tc(loc->getURI()).get() << ":"
            << loc->getLineNumber() << ":" << loc->getColumnNumber() << " " << tc(e.getMessage()).get();

        return true;
    }

private:
    bool failed_;
};

class cse_config_parser
{

public:
    cse_config_parser(
        const boost::filesystem::path& configPath,
        const boost::filesystem::path& schemaPath);
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
        std::optional<double> stepSize;
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

cse_config_parser::cse_config_parser(
    const boost::filesystem::path& configPath,
    const boost::filesystem::path& schemaPath)
{
    // Root node
    xercesc::XMLPlatformUtils::Initialize();
    const auto xerces_cleanup = gsl::final_act([]() {
        xercesc::XMLPlatformUtils::Terminate();
    });

    const auto domImpl = xercesc::DOMImplementationRegistry::getDOMImplementation(tc("LS").get());
    const auto parser = static_cast<xercesc::DOMImplementationLS*>(domImpl)->createLSParser(xercesc::DOMImplementationLS::MODE_SYNCHRONOUS, tc("http://www.w3.org/2001/XMLSchema").get());

    error_handler errorHandler;

    parser->loadGrammar(schemaPath.string().c_str(), xercesc::Grammar::SchemaGrammarType, true);
    parser->loadGrammar(schemaPath.string().c_str(), xercesc::Grammar::SchemaGrammarType, true);

    parser->getDomConfig()->setParameter(xercesc::XMLUni::fgDOMErrorHandler, &errorHandler);
    parser->getDomConfig()->setParameter(xercesc::XMLUni::fgXercesUseCachedGrammarInParse, true);
    parser->getDomConfig()->setParameter(xercesc::XMLUni::fgDOMComments, false);
    parser->getDomConfig()->setParameter(xercesc::XMLUni::fgDOMDatatypeNormalization, true);
    parser->getDomConfig()->setParameter(xercesc::XMLUni::fgDOMEntities, false);
    parser->getDomConfig()->setParameter(xercesc::XMLUni::fgDOMNamespaces, true);
    parser->getDomConfig()->setParameter(xercesc::XMLUni::fgDOMElementContentWhitespace, false);
    parser->getDomConfig()->setParameter(xercesc::XMLUni::fgDOMValidate, true);
    parser->getDomConfig()->setParameter(xercesc::XMLUni::fgXercesSchema, true);
    parser->getDomConfig()->setParameter(xercesc::XMLUni::fgXercesSchemaFullChecking, true);
    parser->getDomConfig()->setParameter(xercesc::XMLUni::fgXercesLoadSchema, false);

    const auto doc = parser->parseURI(configPath.string().c_str());

    if (errorHandler.failed()) {
        std::ostringstream oss;
        oss << "Validation of " << configPath.string() << " failed.";
        BOOST_LOG_SEV(log::logger(), log::error) << oss.str();
        throw std::runtime_error(oss.str());
    }

    const auto rootElement = doc->getDocumentElement();

    const auto simulators = static_cast<xercesc::DOMElement*>(rootElement->getElementsByTagName(tc("Simulators").get())->item(0));
    for (auto element = simulators->getFirstElementChild(); element != nullptr; element = element->getNextElementSibling()) {
        std::string name = tc(element->getAttribute(tc("name").get())).get();
        std::string source = tc(element->getAttribute(tc("source").get())).get();

        std::optional<double> stepSize = std::nullopt;
        auto stepSizeNode = tc(element->getAttribute(tc("stepSize").get()));
        if (*stepSizeNode) {
            stepSize = boost::lexical_cast<double>(stepSizeNode);
            //TODO: Possibly obsolete with schema?
        }
        //TODO: Initial values
        simulators_.push_back({name, source, stepSize});
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
    parser->release();
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
    simulator_index index;
    std::map<std::string, variable_description> variables;
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
        const auto xerces_cleanup = gsl::final_act([]() {
            xercesc::XMLPlatformUtils::Terminate();
        });
        const auto domImpl = xercesc::DOMImplementationRegistry::getDOMImplementation(tc("LS").get());
        const auto parser = static_cast<xercesc::DOMImplementationLS*>(domImpl)->createLSParser(xercesc::DOMImplementationLS::MODE_SYNCHRONOUS, tc("http://www.w3.org/2001/XMLSchema").get());
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

variable_id get_variable(
    const std::unordered_map<std::string, slave_info>& slaves,
    const std::string& element,
    const std::string& connector)
{
    auto slaveIt = slaves.find(element);
    if (slaveIt == slaves.end()) {
        std::ostringstream oss;
        oss << "Cannot find slave: " << element;
        throw std::out_of_range(oss.str());
    }
    auto slave = slaveIt->second;
    auto vdIt = slave.variables.find(connector);
    if (vdIt == slave.variables.end()) {
        std::ostringstream oss;
        oss << "Cannot find variable: " << element << ":" << connector;
        throw std::out_of_range(oss.str());
    }
    auto variable = vdIt->second;
    return {slave.index, variable.type, variable.reference};
}


void connect_variables(
    const std::vector<cse_config_parser::ScalarConnection>& variableConnections,
    const std::unordered_map<std::string, slave_info>& slaves,
    cse::execution& execution)
{

    for (const auto& connection : variableConnections) {
        cse::variable_id output = get_variable(slaves, connection.sourceSimulator, connection.sourceConnector);
        cse::variable_id input = get_variable(slaves, connection.targetSimulator, connection.targetConnector);

        auto c = std::make_shared<cse::scalar_connection>(output, input);
        execution.add_connection(c);
    }
}

extended_model_description get_emd(
    const std::unordered_map<std::string, extended_model_description>& emds,
    const std::string& element)
{
    auto emdIt = emds.find(element);
    if (emdIt == emds.end()) {
        std::ostringstream oss;
        oss << "Cannot find extended model description for " << element;
        throw std::out_of_range(oss.str());
    }
    return emdIt->second;
}

std::vector<std::string> get_plug_socket_variables(
    const std::unordered_map<std::string, plug_socket_description>& descriptions,
    const std::string& element,
    const std::string& connector)
{
    auto psdIt = descriptions.find(connector);
    if (psdIt == descriptions.end()) {
        std::ostringstream oss;
        oss << "Cannot find plug/socket description: " << element << ":" << connector;
        throw std::out_of_range(oss.str());
    }
    return psdIt->second.variables;
}

void connect_plugs_and_sockets(
    const std::vector<cse_config_parser::ScalarConnection>& plugSocketConnections,
    const std::unordered_map<std::string, slave_info>& slaves,
    cse::execution& execution,
    const std::unordered_map<std::string, extended_model_description>& emds)
{

    for (const auto& connection : plugSocketConnections) {
        const auto& sourceEmd = get_emd(emds, connection.sourceSimulator);
        const auto& plugVariables = get_plug_socket_variables(sourceEmd.plugs, connection.sourceSimulator, connection.sourceConnector);
        const auto& targetEmd = get_emd(emds, connection.targetSimulator);
        const auto& socketVariables = get_plug_socket_variables(targetEmd.sockets, connection.targetSimulator, connection.targetConnector);

        if (plugVariables.size() != socketVariables.size()) {
            std::ostringstream oss;
            oss << "Plug " << connection.sourceSimulator << ":" << connection.sourceConnector
                << " has different size [" << plugVariables.size() << "] than the targeted socket "
                << connection.targetSimulator << ":" << connection.targetConnector
                << " size [" << socketVariables.size() << "]";
            throw std::runtime_error(oss.str());
        }

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

const bond_description& get_bond_description(
    const std::unordered_map<std::string, bond_description>& descriptions,
    const std::string& element,
    const std::string& bondName)
{
    auto bondIt = descriptions.find(bondName);
    if (bondIt == descriptions.end()) {
        std::ostringstream oss;
        oss << "Cannot find bond description: " << element << ":" << bondName;
        throw std::out_of_range(oss.str());
    }
    return bondIt->second;
}

void connect_bonds(
    const std::vector<cse_config_parser::ScalarConnection>& bondConnections,
    const std::unordered_map<std::string, slave_info>& slaves,
    cse::execution& execution,
    const std::unordered_map<std::string, extended_model_description>& emds)
{

    for (const auto& connection : bondConnections) {
        const auto& bondAEmd = get_emd(emds, connection.sourceSimulator);
        auto const& bondAPlugs = get_bond_description(bondAEmd.bonds, connection.sourceSimulator, connection.sourceConnector).plugs;
        const auto& bondBEmd = get_emd(emds, connection.targetSimulator);
        auto const& bondBSockets = get_bond_description(bondBEmd.bonds, connection.targetSimulator, connection.targetConnector).sockets;

        if (bondAPlugs.size() != bondBSockets.size()) {
            std::ostringstream oss;
            oss << "Bond " << connection.sourceSimulator << ":" << connection.sourceConnector
                << " has different number of plugs [" << bondAPlugs.size() << "] "
                << " than the number of sockets [" << bondBSockets.size() << "] for targeted bond "
                << connection.targetSimulator << ":" << connection.targetConnector;
            throw std::runtime_error(oss.str());
        }

        std::vector<cse_config_parser::ScalarConnection> plugSocketConnectionsA;
        for (std::size_t i = 0; i < bondAPlugs.size(); ++i) {
            plugSocketConnectionsA.push_back({connection.sourceSimulator,
                bondAPlugs.at(i),
                connection.targetSimulator,
                bondBSockets.at(i)});
        }
        connect_plugs_and_sockets(plugSocketConnectionsA, slaves, execution, emds);

        const auto& bondASockets = get_bond_description(bondAEmd.bonds, connection.sourceSimulator, connection.sourceConnector).sockets;
        const auto& bondBPlugs = get_bond_description(bondBEmd.bonds, connection.targetSimulator, connection.targetConnector).plugs;

        if (bondASockets.size() != bondBPlugs.size()) {
            std::ostringstream oss;
            oss << "Bond " << connection.sourceSimulator << ":" << connection.sourceConnector
                << " has different number of sockets [" << bondASockets.size() << "] "
                << " than the number of plugs [" << bondBPlugs.size() << "] for targeted bond "
                << connection.targetSimulator << ":" << connection.targetConnector;
            throw std::runtime_error(oss.str());
        }
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
            sources.push_back(get_variable(slaves, sim, var));
        }
        auto target = get_variable(slaves, sumConnection.target.first, sumConnection.target.second);
        auto c = std::make_shared<sum_connection>(sources, target);
        execution.add_connection(c);
    }
}

int calculate_decimation_factor(const std::string& name, duration baseStepSize, double modelStepSize)
{
    const auto slaveStepSize = to_duration(modelStepSize);
    const auto result = std::div(slaveStepSize.count(), baseStepSize.count());
    const int factor = std::max<int>(1, static_cast<int>(result.quot));
    if (result.rem > 0 || result.quot < 1) {
        duration actualStepSize = baseStepSize * factor;
        const auto startTime = time_point();
        BOOST_LOG_SEV(log::logger(), log::warning)
            << "Effective step size for " << name
            << " will be " << to_double_duration(actualStepSize, startTime) << " s"
            << " instead of configured value " << modelStepSize << " s";
    }
    return factor;
}

} // namespace

std::pair<execution, simulator_map> load_cse_config(
    model_uri_resolver& resolver,
    const boost::filesystem::path& configPath,
    const boost::filesystem::path& schemaPath,
    std::optional<time_point> overrideStartTime)
{
    simulator_map simulatorMap;
    const auto configFile = boost::filesystem::is_regular_file(configPath)
        ? configPath
        : configPath / "OspSystemStructure.xml";
    const auto schemaFile = boost::filesystem::is_regular_file(schemaPath)
        ? schemaPath
        : schemaPath / "OspSystemStructure.xsd";
    const auto baseURI = path_to_file_uri(configFile);

    if (!boost::filesystem::exists(schemaFile) || schemaFile.extension() != ".xsd") {
        std::ostringstream oss;
        oss << "Specified schema file does not exist or has wrong extension (must be '.xsd'): "
            << schemaFile;
        BOOST_LOG_SEV(log::logger(), log::error) << oss.str();
        throw std::runtime_error(oss.str());
    }
    const auto parser = cse_config_parser(configFile, schemaFile);

    const auto& simInfo = parser.get_simulation_information();
    if (simInfo.stepSize <= 0.0) {
        std::ostringstream oss;
        oss << "Configured base step size [" << simInfo.stepSize << "] must be nonzero and positive";
        BOOST_LOG_SEV(log::logger(), log::error) << oss.str();
        throw std::invalid_argument(oss.str());
    }
    const duration stepSize = to_duration(simInfo.stepSize);

    auto simulators = parser.get_elements();

    const auto startTime = overrideStartTime ? *overrideStartTime : to_time_point(simInfo.startTime);

    auto algo = std::make_shared<fixed_step_algorithm>(stepSize);
    auto exec = execution(startTime, algo);

    std::unordered_map<std::string, slave_info> slaves;
    std::unordered_map<std::string, extended_model_description> emds;
    for (const auto& simulator : simulators) {
        auto model = resolver.lookup_model(baseURI, simulator.source);
        auto slave = model->instantiate(simulator.name);
        simulator_index index = slaves[simulator.name].index = exec.add_slave(slave, simulator.name);
        if (simulator.stepSize) {
            algo->set_stepsize_decimation_factor(index, calculate_decimation_factor(simulator.name, stepSize, *simulator.stepSize));
        }
        simulatorMap[simulator.name] = simulator_map_entry{index, simulator.source, *model->description()};

        for (const auto& v : model->description()->variables) {
            slaves[simulator.name].variables[v.name] = v;
        }

        std::string msmiFileName = model->description()->name + "_OspModelDescription.xml";
        const auto msmiFilePath = configFile.parent_path() / msmiFileName;
        if (boost::filesystem::exists(msmiFilePath)) {
            emds.emplace(simulator.name, msmiFilePath);
        }
    }

    connect_variables(parser.get_scalar_connections(), slaves, exec);
    connect_sum(parser.get_sum_connections(), slaves, exec);
    connect_plugs_and_sockets(parser.get_plug_socket_connections(), slaves, exec, emds);
    connect_bonds(parser.get_bond_connections(), slaves, exec, emds);

    return std::make_pair(std::move(exec), std::move(simulatorMap));
}

} // namespace cse
