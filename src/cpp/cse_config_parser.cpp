#include "cse/cse_config_parser.hpp"

#include "cse_system_structure.hpp"

#include "cse/algorithm.hpp"
#include "cse/fmi/fmu.hpp"
#include "cse/function/linear_transformation.hpp"
#include "cse/function/vector_sum.hpp"
#include <cse/exception.hpp>
#include <cse/log/logger.hpp>
#include <cse/utility/utility.hpp>

#include <boost/lexical_cast.hpp>
#include <xercesc/dom/DOM.hpp>
#include <xercesc/framework/MemBufInputSource.hpp>
#include <xercesc/framework/Wrapper4InputSource.hpp>
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
        const boost::filesystem::path& configPath);
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

    struct InitialValue
    {
        std::string name;
        cse::variable_type type;
        scalar_value value;
    };

    struct Simulator
    {
        std::string name;
        std::string source;
        std::optional<double> stepSize;
        std::vector<InitialValue> initialValues;
    };

    const std::vector<Simulator>& get_elements() const;

    struct Signal
    {
        std::string name;
        cse::variable_type type;
        cse::variable_causality causality;
    };

    struct SignalGroup
    {
        std::string name;
        std::vector<std::string> signals;
    };

    struct LinearTransformationFunction
    {
        std::string name;
        double factor;
        double offset;
        Signal in;
        Signal out;
    };

    struct SumFunction
    {
        std::string name;
        int inputCount;
        std::vector<Signal> ins;
        Signal out;
    };

    struct VectorSumFunction
    {
        std::string name;
        int inputCount;
        int dimension;
        std::vector<Signal> ins;
        std::vector<Signal> outs;
        std::vector<SignalGroup> inGroups;
        SignalGroup outGroup;
    };

    const std::unordered_map<std::string, LinearTransformationFunction>& get_linear_transformation_functions() const;
    const std::unordered_map<std::string, SumFunction>& get_sum_functions() const;
    const std::unordered_map<std::string, VectorSumFunction>& get_vector_sum_functions() const;

    struct VariableEndpoint
    {
        std::string simulator;
        std::string name;
    };

    struct SignalEndpoint
    {
        std::string function;
        std::string name;
    };

    struct VariableConnection
    {
        VariableEndpoint variableA;
        VariableEndpoint variableB;
    };

    struct SignalConnection
    {
        SignalEndpoint signal;
        VariableEndpoint variable;
    };


    const std::vector<VariableConnection>& get_variable_connections() const;
    const std::vector<SignalConnection>& get_signal_connections() const;
    const std::vector<VariableConnection>& get_variable_group_connections() const;
    const std::vector<SignalConnection>& get_signal_group_connections() const;

private:
    SystemDescription systemDescription_;
    SimulationInformation simulationInformation_;
    std::vector<Simulator> simulators_;
    std::unordered_map<std::string, LinearTransformationFunction> linearTransformationFunctions_;
    std::unordered_map<std::string, SumFunction> sumFunctions_;
    std::unordered_map<std::string, VectorSumFunction> vectorSumFunctions_;
    std::vector<VariableConnection> variableConnections_;
    std::vector<VariableConnection> variableGroupConnections_;
    std::vector<SignalConnection> signalConnections_;
    std::vector<SignalConnection> signalGroupConnections_;
    static variable_type parse_variable_type(const std::string&);
    static bool parse_boolean_value(const std::string& s);
};

namespace
{

template<typename T>
T attribute_or(xercesc::DOMElement* el, const char* attributeName, T defaultValue)
{
    auto node = tc(el->getAttribute(tc(attributeName).get()));
    if (*node) {
        return boost::lexical_cast<T>(node);
    } else {
        return defaultValue;
    }
}

} // namespace

cse_config_parser::cse_config_parser(
    const boost::filesystem::path& configPath)
{
    // Root node
    xercesc::XMLPlatformUtils::Initialize();
    const auto xerces_cleanup = gsl::final_act([]() {
        xercesc::XMLPlatformUtils::Terminate();
    });

    const auto domImpl = xercesc::DOMImplementationRegistry::getDOMImplementation(tc("LS").get());
    const auto parser = static_cast<xercesc::DOMImplementationLS*>(domImpl)->createLSParser(
        xercesc::DOMImplementationLS::MODE_SYNCHRONOUS,
        tc("http://www.w3.org/2001/XMLSchema").get());

    error_handler errorHandler;

    std::string xsd_str = get_embedded_cse_config_xsd();

    xercesc::MemBufInputSource mis(
        reinterpret_cast<const XMLByte*>(xsd_str.c_str()),
        xsd_str.size(),
        "http://opensimulationplatform.com/MSMI/OSPSystemStructure");
    xercesc::Wrapper4InputSource wmis(&mis, false);

    parser->loadGrammar(&wmis, xercesc::Grammar::SchemaGrammarType, true);

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
        }

        std::vector<InitialValue> initialValues;
        const auto initValsElement = static_cast<xercesc::DOMElement*>(element->getElementsByTagName(tc("InitialValues").get())->item(0));
        if (initValsElement) {
            for (auto initValElement = initValsElement->getFirstElementChild(); initValElement != nullptr; initValElement = initValElement->getNextElementSibling()) {
                std::string varName = tc(initValElement->getAttribute(tc("variable").get())).get();
                variable_type varType = parse_variable_type(std::string(tc(initValElement->getFirstElementChild()->getNodeName()).get()));
                std::string varValue = tc(initValElement->getFirstElementChild()->getAttribute(tc("value").get())).get();
                switch (varType) {
                    case variable_type::real:
                        initialValues.push_back({varName, varType, boost::lexical_cast<double>(varValue)});
                        break;
                    case variable_type::integer:
                        initialValues.push_back({varName, varType, boost::lexical_cast<int>(varValue)});
                        break;
                    case variable_type::boolean:
                        initialValues.push_back({varName, varType, parse_boolean_value(varValue)});
                        break;
                    case variable_type::string:
                        initialValues.push_back({varName, varType, varValue});
                        break;
                    case variable_type::enumeration:
                        initialValues.push_back({varName, varType, boost::lexical_cast<int>(varValue)});
                        break;
                }
            }
        }
        simulators_.push_back({name, source, stepSize, initialValues});
    }

    const auto functionsElement = static_cast<xercesc::DOMElement*>(rootElement->getElementsByTagName(tc("Functions").get())->item(0));
    if (functionsElement != nullptr) {
        for (auto functionElement = functionsElement->getFirstElementChild(); functionElement != nullptr; functionElement = functionElement->getNextElementSibling()) {


            const auto functionNodeName = std::string(tc(functionElement->getNodeName()).get());
            const auto functionName = std::string(tc(functionElement->getAttribute(tc("name").get())).get());
            if ("LinearTransformation" == functionNodeName) {
                LinearTransformationFunction function;
                function.name = functionName;

                function.factor = attribute_or<double>(functionElement, "factor", 1.0);
                function.offset = attribute_or<double>(functionElement, "offset", 0.0);

                function.in = {"in", variable_type::real, variable_causality::input};
                function.out = {"out", variable_type::real, variable_causality::output};

                linearTransformationFunctions_[functionName] = function;

            } else if ("Sum" == functionNodeName) {
                SumFunction function;
                function.name = functionName;

                int inputCount = boost::lexical_cast<int>(tc(functionElement->getAttribute(tc("inputCount").get())));
                function.inputCount = inputCount;

                for (int i = 0; i < inputCount; i++) {
                    const std::string sigName = "in[" + std::to_string(i) + "]";
                    function.ins.push_back({sigName, variable_type::real, variable_causality::input});
                }
                function.out = {"out", variable_type::real, variable_causality::output};

                sumFunctions_[functionName] = function;

            } else if ("VectorSum" == functionNodeName) {
                VectorSumFunction function;
                function.name = functionName;

                int inputCount = boost::lexical_cast<int>(tc(functionElement->getAttribute(tc("inputCount").get())));
                function.inputCount = inputCount;
                int dimension = boost::lexical_cast<int>(tc(functionElement->getAttribute(tc("dimension").get())));
                function.dimension = inputCount;

                for (int i = 0; i < inputCount; i++) {
                    const std::string sgName = "in[" + std::to_string(i) + "]";
                    SignalGroup sg;
                    sg.name = sgName;
                    for (int j = 0; j < dimension; j++) {
                        const std::string sigName = sgName + "[" + std::to_string(j) + "]";
                        function.ins.push_back({sigName, variable_type::real, variable_causality::input});
                        sg.signals.emplace_back(sigName);
                    }
                    function.inGroups.push_back(std::move(sg));
                }
                SignalGroup sg;
                sg.name = "out";
                for (int j = 0; j < dimension; j++) {
                    const std::string sigName = "out[" + std::to_string(j) + "]";
                    function.outs.push_back({sigName, variable_type::real, variable_causality::output});
                    sg.signals.emplace_back(sigName);
                }
                function.outGroup = sg;

                vectorSumFunctions_[functionName] = function;

            } else {
                std::ostringstream oss;
                oss << "Found unknown function type " << functionNodeName << " with name " << functionName;
                throw std::runtime_error(oss.str());
            }
        }
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

    auto connectionsElement = static_cast<xercesc::DOMElement*>(rootElement->getElementsByTagName(tc("Connections").get())->item(0));
    if (connectionsElement) {
        auto variableConnectionsElement = connectionsElement->getElementsByTagName(tc("VariableConnection").get());
        for (size_t i = 0; i < variableConnectionsElement->getLength(); i++) {
            auto connectionElement = static_cast<xercesc::DOMElement*>(variableConnectionsElement->item(i));
            auto a = static_cast<xercesc::DOMElement*>(connectionElement->getElementsByTagName(tc("Variable").get())->item(0));
            auto b = static_cast<xercesc::DOMElement*>(connectionElement->getElementsByTagName(tc("Variable").get())->item(1));

            std::string simulatorA = tc(a->getAttribute(tc("simulator").get())).get();
            std::string nameA = tc(a->getAttribute(tc("name").get())).get();
            VariableEndpoint veA = {simulatorA, nameA};

            std::string simulatorB = tc(b->getAttribute(tc("simulator").get())).get();
            std::string nameB = tc(b->getAttribute(tc("name").get())).get();
            VariableEndpoint veB = {simulatorB, nameB};

            variableConnections_.push_back({veA, veB});
        }

        auto signalConnectionsElement = connectionsElement->getElementsByTagName(tc("SignalConnection").get());
        for (size_t i = 0; i < signalConnectionsElement->getLength(); i++) {
            auto connectionElement = static_cast<xercesc::DOMElement*>(signalConnectionsElement->item(i));
            auto signalElement = static_cast<xercesc::DOMElement*>(connectionElement->getElementsByTagName(tc("Signal").get())->item(0));
            auto variableElement = static_cast<xercesc::DOMElement*>(connectionElement->getElementsByTagName(tc("Variable").get())->item(0));

            std::string function = tc(signalElement->getAttribute(tc("function").get())).get();
            std::string sigName = tc(signalElement->getAttribute(tc("name").get())).get();
            SignalEndpoint signal = {function, sigName};

            std::string simulator = tc(variableElement->getAttribute(tc("simulator").get())).get();
            std::string varName = tc(variableElement->getAttribute(tc("name").get())).get();
            VariableEndpoint variable = {simulator, varName};

            signalConnections_.push_back({signal, variable});
        }

        auto variableGroupConnectionsElement = connectionsElement->getElementsByTagName(tc("VariableGroupConnection").get());
        for (size_t i = 0; i < variableGroupConnectionsElement->getLength(); i++) {
            auto connectionElement = static_cast<xercesc::DOMElement*>(variableGroupConnectionsElement->item(i));
            auto a = static_cast<xercesc::DOMElement*>(connectionElement->getElementsByTagName(tc("VariableGroup").get())->item(0));
            auto b = static_cast<xercesc::DOMElement*>(connectionElement->getElementsByTagName(tc("VariableGroup").get())->item(1));

            std::string simulatorA = tc(a->getAttribute(tc("simulator").get())).get();
            std::string nameA = tc(a->getAttribute(tc("name").get())).get();
            VariableEndpoint veA = {simulatorA, nameA};

            std::string simulatorB = tc(b->getAttribute(tc("simulator").get())).get();
            std::string nameB = tc(b->getAttribute(tc("name").get())).get();
            VariableEndpoint veB = {simulatorB, nameB};

            variableGroupConnections_.push_back({veA, veB});
        }

        auto signalGroupConnectionsElement = connectionsElement->getElementsByTagName(tc("SignalGroupConnection").get());
        for (size_t i = 0; i < signalGroupConnectionsElement->getLength(); i++) {
            auto connectionElement = static_cast<xercesc::DOMElement*>(signalGroupConnectionsElement->item(i));
            auto signalElement = static_cast<xercesc::DOMElement*>(connectionElement->getElementsByTagName(tc("SignalGroup").get())->item(0));
            auto variableElement = static_cast<xercesc::DOMElement*>(connectionElement->getElementsByTagName(tc("VariableGroup").get())->item(0));

            std::string function = tc(signalElement->getAttribute(tc("function").get())).get();
            std::string sigName = tc(signalElement->getAttribute(tc("name").get())).get();
            SignalEndpoint signal = {function, sigName};

            std::string simulator = tc(variableElement->getAttribute(tc("simulator").get())).get();
            std::string varName = tc(variableElement->getAttribute(tc("name").get())).get();
            VariableEndpoint variable = {simulator, varName};

            signalGroupConnections_.push_back({signal, variable});
        }
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

const std::unordered_map<std::string, cse_config_parser::LinearTransformationFunction>&
cse_config_parser::get_linear_transformation_functions() const
{
    return linearTransformationFunctions_;
}

const std::unordered_map<std::string, cse_config_parser::SumFunction>&
cse_config_parser::get_sum_functions() const
{
    return sumFunctions_;
}

const std::unordered_map<std::string, cse_config_parser::VectorSumFunction>&
cse_config_parser::get_vector_sum_functions() const
{
    return vectorSumFunctions_;
}

const std::vector<cse_config_parser::VariableConnection>& cse_config_parser::get_variable_connections() const
{
    return variableConnections_;
}

const std::vector<cse_config_parser::SignalConnection>& cse_config_parser::get_signal_connections() const
{
    return signalConnections_;
}

const std::vector<cse_config_parser::VariableConnection>& cse_config_parser::get_variable_group_connections() const
{
    return variableGroupConnections_;
}

const std::vector<cse_config_parser::SignalConnection>& cse_config_parser::get_signal_group_connections() const
{
    return signalGroupConnections_;
}

variable_type cse_config_parser::parse_variable_type(const std::string& str)
{
    if ("Real" == str) {
        return cse::variable_type::real;
    }
    if ("Integer" == str) {
        return cse::variable_type::integer;
    }
    if ("Boolean" == str) {
        return cse::variable_type::boolean;
    }
    if ("String" == str) {
        return cse::variable_type::string;
    }
    if ("Enumeration" == str) {
        return cse::variable_type::enumeration;
    }
    throw std::runtime_error("Failed to parse variable type: " + str);
}

bool cse_config_parser::parse_boolean_value(const std::string& s)
{
    bool bool_value;
    std::istringstream iss(s);
    iss >> bool_value;

    if (iss.fail()) {
        iss.clear();
        iss >> std::boolalpha >> bool_value;
        if (iss.fail()) {
            throw std::invalid_argument("Value: '" + s + "' is not convertable to bool");
        }
    }

    return bool_value;
}

struct slave_info
{
    simulator_index index;
    std::map<std::string, variable_description> variables;
};


struct variable_group_description
{
    std::string name;
    std::string type;
    std::vector<std::string> variables;
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

        const auto variableGroupsElement = static_cast<xercesc::DOMElement*>(rootElement->getElementsByTagName(tc("VariableGroups").get())->item(0));

        for (auto variableGroupElement = variableGroupsElement->getFirstElementChild(); variableGroupElement != nullptr; variableGroupElement = variableGroupElement->getNextElementSibling()) {
            variable_group_description vgd;
            vgd.name = tc(variableGroupElement->getAttribute(tc("name").get())).get();
            vgd.type = tc(variableGroupElement->getAttribute(tc("type").get())).get();

            auto variableElements = variableGroupElement->getElementsByTagName(tc("Variable").get());
            for (size_t i = 0; i < variableElements->getLength(); i++) {
                auto variableElement = static_cast<xercesc::DOMElement*>(variableElements->item(i));

                std::string variableName = tc(variableElement->getAttribute(tc("name").get())).get();
                vgd.variables.push_back(std::move(variableName));
            }
            variableGroups[vgd.name] = std::move(vgd);
        }
    }

    std::unordered_map<std::string, variable_group_description> variableGroups;
};

namespace
{

std::pair<variable_id, variable_causality> get_variable(
    const std::unordered_map<std::string, slave_info>& slaves,
    const std::string& element,
    const std::string& connector)
{
    auto slaveIt = slaves.find(element);
    if (slaveIt == slaves.end()) {
        std::ostringstream oss;
        oss << "Cannot find simulator: " << element;
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
    return {{slave.index, variable.type, variable.reference}, variable.causality};
}

cse_config_parser::SignalConnection find_signal_connection(
    const std::vector<cse_config_parser::SignalConnection>& signalConnections,
    const std::string& functionName,
    const std::string& signalName)
{
    auto it = std::find_if(
        signalConnections.begin(),
        signalConnections.end(),
        [&](const auto& conn) { return functionName == conn.signal.function && signalName == conn.signal.name; });
    if (it == signalConnections.end()) {
        std::ostringstream oss;
        oss << "Missing expected connection to " << functionName << ":" << signalName;
        throw std::out_of_range(oss.str());
    }
    return *it;
}

void verify_causality(variable_causality actual, variable_causality expected, const cse_config_parser::SignalConnection& conn)
{
    if (actual != expected) {
        std::ostringstream oss;
        oss << "Cannot create connection. There is a causality imbalance between "
            << conn.variable.simulator << ":" << conn.variable.name
            << " and "
            << conn.signal.function << ":" << conn.signal.name;
        throw std::runtime_error(oss.str());
    }
}

void verify_type(variable_type actual, variable_type expected, const cse_config_parser::SignalConnection& conn)
{
    if (actual != expected) {
        std::ostringstream oss;
        oss << "Cannot create connection. There is a type mismatch between "
            << conn.variable.simulator << ":" << conn.variable.name
            << " and "
            << conn.signal.function << ":" << conn.signal.name;
        throw std::runtime_error(oss.str());
    }
}

} // namespace

void connect_variables(
    const std::vector<cse_config_parser::VariableConnection>& variableConnections,
    const std::unordered_map<std::string, slave_info>& slaves,
    cse::execution& execution)
{
    for (const auto& connection : variableConnections) {
        auto [variableIdA, causalityA] =
            get_variable(slaves, connection.variableA.simulator, connection.variableA.name);
        auto [variableIdB, causalityB] =
            get_variable(slaves, connection.variableB.simulator, connection.variableB.name);

        variable_id output;
        variable_id input;
        if (variable_causality::input == causalityA && variable_causality::output == causalityB) {
            output = variableIdB;
            input = variableIdA;
        } else if (variable_causality::output == causalityA && variable_causality::input == causalityB) {
            output = variableIdA;
            input = variableIdB;
        } else {
            std::ostringstream oss;
            oss << "Cannot create connection. There is a causality imbalance between variables "
                << connection.variableA.simulator << ":" << connection.variableA.name
                << " (" << causalityA << ") and "
                << connection.variableB.simulator << ":" << connection.variableB.name
                << " (" << causalityB << ").";
            throw std::runtime_error(oss.str());
        }

        execution.connect_variables(output, input);
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

std::vector<std::string> get_variable_group_variables(
    const std::unordered_map<std::string, variable_group_description>& descriptions,
    const std::string& element,
    const std::string& connector)
{
    auto groupIt = descriptions.find(connector);
    if (groupIt == descriptions.end()) {
        std::ostringstream oss;
        oss << "Cannot find variable group description: " << element << ":" << connector;
        throw std::out_of_range(oss.str());
    }
    return groupIt->second.variables;
}

void connect_variable_groups(
    const std::vector<cse_config_parser::VariableConnection>& variableGroupConnections,
    const std::unordered_map<std::string, slave_info>& slaves,
    cse::execution& execution,
    const std::unordered_map<std::string, extended_model_description>& emds)
{

    for (const auto& connection : variableGroupConnections) {
        const auto& emdA = get_emd(emds, connection.variableA.simulator);
        const auto& variablesA =
            get_variable_group_variables(emdA.variableGroups, connection.variableA.simulator, connection.variableA.name);
        const auto& emdB = get_emd(emds, connection.variableB.simulator);
        const auto& variablesB =
            get_variable_group_variables(emdB.variableGroups, connection.variableB.simulator, connection.variableB.name);

        if (variablesA.size() != variablesB.size()) {
            std::ostringstream oss;
            oss << "Cannot create connection between variable groups. Variable group "
                << connection.variableA.simulator << ":" << connection.variableA.name
                << " has different size [" << variablesA.size() << "] than variable group "
                << connection.variableB.simulator << ":" << connection.variableB.name
                << " size [" << variablesB.size() << "]";
            throw std::runtime_error(oss.str());
        }

        std::vector<cse_config_parser::VariableConnection> variableConnections;

        // clang-format off
        for (std::size_t i = 0; i < variablesA.size(); ++i) {
            variableConnections.push_back({
                {connection.variableA.simulator, variablesA.at(i)},
                {connection.variableB.simulator, variablesB.at(i)}});
        }
        // clang-format on

        connect_variables(variableConnections, slaves, execution);
    }
}

void connect_linear_transformation_functions(
    const std::unordered_map<std::string, cse_config_parser::LinearTransformationFunction>& functions,
    const std::vector<cse_config_parser::SignalConnection>& signalConnections,
    const std::unordered_map<std::string, slave_info>& slaves,
    cse::execution& execution)
{
    for (const auto& [functionName, function] : functions) {
        const auto inConn = find_signal_connection(signalConnections, functionName, function.in.name);
        const auto [sourceVar, sourceVarCausality] = get_variable(slaves, inConn.variable.simulator, inConn.variable.name);
        verify_causality(sourceVarCausality, variable_causality::output, inConn);
        verify_type(sourceVar.type, variable_type::real, inConn);

        const auto outConn = find_signal_connection(signalConnections, functionName, function.out.name);
        const auto [targetVar, targetVarCausality] = get_variable(slaves, outConn.variable.simulator, outConn.variable.name);
        verify_causality(targetVarCausality, variable_causality::input, outConn);
        verify_type(targetVar.type, variable_type::real, inConn);

        const auto fn = execution.add_function(
            std::make_shared<linear_transformation_function>(function.offset, function.factor));
        execution.connect_variables(
            sourceVar,
            function_io_id{fn, variable_type::real, linear_transformation_function::in_io_reference});
        execution.connect_variables(
            function_io_id{fn, variable_type::real, linear_transformation_function::out_io_reference},
            targetVar);
    }
}

void connect_sum_functions(
    const std::unordered_map<std::string, cse_config_parser::SumFunction>& functions,
    const std::vector<cse_config_parser::SignalConnection>& signalConnections,
    const std::unordered_map<std::string, slave_info>& slaves,
    cse::execution& execution)
{
    for (const auto& [functionName, function] : functions) {
        std::vector<variable_id> sourceVars;
        for (int i = 0; i < function.inputCount; i++) {
            const auto inConn = find_signal_connection(signalConnections, functionName, function.ins.at(i).name);
            const auto [sourceVar, sourceVarCausality] = get_variable(slaves, inConn.variable.simulator, inConn.variable.name);
            verify_causality(sourceVarCausality, variable_causality::output, inConn);
            verify_type(sourceVar.type, variable_type::real, inConn);
            sourceVars.push_back(sourceVar);
        }
        const auto outConn = find_signal_connection(signalConnections, functionName, function.out.name);
        const auto [targetVar, targetVarCausality] = get_variable(slaves, outConn.variable.simulator, outConn.variable.name);
        verify_causality(targetVarCausality, variable_causality::input, outConn);
        verify_type(targetVar.type, variable_type::real, outConn);

        const auto fn = execution.add_function(
            std::make_shared<vector_sum_function<double>>(function.inputCount, 1));
        for (int i = 0; i < function.inputCount; ++i) {
            execution.connect_variables(
                sourceVars.at(i),
                function_io_id{fn, variable_type::real, vector_sum_function<double>::in_io_reference(i, 0)});
        }
        execution.connect_variables(
            function_io_id{fn, variable_type::real, vector_sum_function<double>::out_io_reference(0)},
            targetVar);
    }
}

void connect_vector_sum_functions(
    const std::unordered_map<std::string, cse_config_parser::VectorSumFunction>& functions,
    const std::vector<cse_config_parser::SignalConnection>& signalGroupConnections,
    const std::unordered_map<std::string, slave_info>& slaves,
    cse::execution& execution,
    const std::unordered_map<std::string, extended_model_description>& emds)
{
    for (const auto& [functionName, function] : functions) {
        const auto fn = execution.add_function(
            std::make_shared<vector_sum_function<double>>(function.inputCount, function.dimension));

        for (int i = 0; i < function.dimension; i++) {

            const auto outGroupConn = find_signal_connection(signalGroupConnections, functionName, function.outGroup.name);
            const auto& emd = emds.at(outGroupConn.variable.simulator);
            const auto& variableGroup = emd.variableGroups.at(outGroupConn.variable.name);
            const auto& variableName = variableGroup.variables.at(i);
            const auto [targetVar, targetVarCausality] = get_variable(slaves, outGroupConn.variable.simulator, variableName);
            verify_causality(targetVarCausality, variable_causality::input, outGroupConn);
            verify_type(targetVar.type, variable_type::real, outGroupConn);
            variable_id targetVariable = targetVar;

            std::vector<variable_id> sourceVariables;
            for (int j = 0; j < function.inputCount; j++) {
                const auto inGroupConn = find_signal_connection(signalGroupConnections, functionName, function.inGroups.at(j).name);
                const auto& inEmd = emds.at(inGroupConn.variable.simulator);
                const auto& inVariableGroup = inEmd.variableGroups.at(inGroupConn.variable.name);
                const auto& inVariableName = inVariableGroup.variables.at(i);
                const auto [sourceVar, sourceVarCausality] = get_variable(slaves, inGroupConn.variable.simulator, inVariableName);
                verify_causality(sourceVarCausality, variable_causality::output, inGroupConn);
                verify_type(sourceVar.type, variable_type::real, inGroupConn);
                sourceVariables.push_back(sourceVar);
            }

            for (int j = 0; j < function.inputCount; ++j) {
                execution.connect_variables(
                    sourceVariables.at(j),
                    function_io_id{fn, variable_type::real, vector_sum_function<double>::in_io_reference(j, i)});
            }
            execution.connect_variables(
                function_io_id{fn, variable_type::real, vector_sum_function<double>::out_io_reference(i)},
                targetVariable);
            }
    }
}

} // namespace

std::pair<execution, simulator_map> load_cse_config(
    model_uri_resolver& resolver,
    const boost::filesystem::path& configPath,
    std::optional<time_point> overrideStartTime)
{
    simulator_map simulatorMap;
    const auto absolutePath = boost::filesystem::absolute(configPath);
    const auto configFile = boost::filesystem::is_regular_file(absolutePath)
        ? absolutePath
        : absolutePath / "OspSystemStructure.xml";
    const auto baseURI = path_to_file_uri(configFile);

    const auto parser = cse_config_parser(configFile);

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
    auto exec = execution(startTime, std::make_shared<fixed_step_algorithm>(stepSize));

    std::unordered_map<std::string, slave_info> slaves;
    std::unordered_map<std::string, extended_model_description> emds;
    for (const auto& simulator : simulators) {
        auto model = resolver.lookup_model(baseURI, simulator.source);
        auto slave = model->instantiate(simulator.name);
        simulator_index index = slaves[simulator.name].index = exec.add_slave(
            slave,
            simulator.name,
            simulator.stepSize ? to_duration(*simulator.stepSize) : duration::zero());
        simulatorMap[simulator.name] = simulator_map_entry{index, *model->description()};

        for (const auto& v : model->description()->variables) {
            slaves[simulator.name].variables[v.name] = v;
        }

        for (const auto& p : simulator.initialValues) {
            auto reference = find_variable(*model->description(), p.name).reference;
            BOOST_LOG_SEV(log::logger(), log::info)
                << "Initializing variable " << simulator.name << ":" << p.name << " with value " << streamer{p.value};
            switch (p.type) {
                case variable_type::real:
                    exec.set_real_initial_value(index, reference, std::get<double>(p.value));
                    break;
                case variable_type::integer:
                    exec.set_integer_initial_value(index, reference, std::get<int>(p.value));
                    break;
                case variable_type::boolean:
                    exec.set_boolean_initial_value(index, reference, std::get<bool>(p.value));
                    break;
                case variable_type::string:
                    exec.set_string_initial_value(index, reference, std::get<std::string>(p.value));
                    break;
                default:
                    throw error(make_error_code(errc::unsupported_feature), "Variable type not supported yet");
            }
        }

        std::string msmiFileName = model->description()->name + "_OspModelDescription.xml";
        const auto msmiFilePath = configFile.parent_path() / msmiFileName;
        if (boost::filesystem::exists(msmiFilePath)) {
            emds.emplace(simulator.name, msmiFilePath);
        }
    }

    connect_variables(parser.get_variable_connections(), slaves, exec);
    connect_variable_groups(parser.get_variable_group_connections(), slaves, exec, emds);
    connect_linear_transformation_functions(parser.get_linear_transformation_functions(), parser.get_signal_connections(), slaves, exec);
    connect_sum_functions(parser.get_sum_functions(), parser.get_signal_connections(), slaves, exec);
    connect_vector_sum_functions(parser.get_vector_sum_functions(), parser.get_signal_group_connections(), slaves, exec, emds);

    return std::make_pair(std::move(exec), std::move(simulatorMap));
}

} // namespace cse
