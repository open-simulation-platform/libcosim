/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/osp_config_parser.hpp"

#include "osp_system_structure_schema.hpp"

#include "cosim/function/linear_transformation.hpp"
#include "cosim/function/vector_sum.hpp"
#include "cosim/log/logger.hpp"
#include "cosim/uri.hpp"
#include "cosim/algorithm.hpp"

#include <boost/lexical_cast.hpp>
#include <xercesc/dom/DOM.hpp>
#include <xercesc/framework/MemBufInputSource.hpp>
#include <xercesc/framework/Wrapper4InputSource.hpp>
#include <xercesc/util/PlatformUtils.hpp>
#include <xercesc/util/XMLString.hpp>

#include <ios>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <iostream>
#include <optional>


namespace cosim
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
    { }

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

// The following class was copied from Microsoft Guidelines Support Library
// https://github.com/microsoft/GSL
// final_action allows you to ensure something gets run at the end of a scope
template<class F>
class final_action
{
public:
    explicit final_action(F f) noexcept
        : f_(std::move(f))
    { }

    final_action(final_action&& other) noexcept
        : f_(std::move(other.f_))
        , invoke_(std::exchange(other.invoke_, false))
    { }

    final_action(const final_action&) = delete;
    final_action& operator=(const final_action&) = delete;
    final_action& operator=(final_action&&) = delete;

    ~final_action() noexcept
    {
        if (invoke_) f_();
    }

private:
    F f_;
    bool invoke_{true};
};

class osp_config_parser
{

public:
    osp_config_parser(
        const cosim::filesystem::path& configPath);
    ~osp_config_parser() noexcept;

    struct SimulationInformation
    {
        std::string description;
        std::string algorithm;
        double stepSize = 0.1;
        double startTime = 0.0;
        std::optional<double> endTime;
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
        cosim::variable_type type;
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
        cosim::variable_type type;
        cosim::variable_causality causality;
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

    [[nodiscard]] const std::unordered_map<std::string, LinearTransformationFunction>& get_linear_transformation_functions() const;
    [[nodiscard]] const std::unordered_map<std::string, SumFunction>& get_sum_functions() const;
    [[nodiscard]] const std::unordered_map<std::string, VectorSumFunction>& get_vector_sum_functions() const;

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


    [[nodiscard]] const std::vector<VariableConnection>& get_variable_connections() const;
    [[nodiscard]] const std::vector<SignalConnection>& get_signal_connections() const;
    [[nodiscard]] const std::vector<VariableConnection>& get_variable_group_connections() const;
    [[nodiscard]] const std::vector<SignalConnection>& get_signal_group_connections() const;

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

osp_config_parser::osp_config_parser(
    const cosim::filesystem::path& configPath)
{
    // Root node
    xercesc::XMLPlatformUtils::Initialize();
    const auto xerces_cleanup = final_action([]() {
        xercesc::XMLPlatformUtils::Terminate();
    });

    const auto domImpl = xercesc::DOMImplementationRegistry::getDOMImplementation(tc("LS").get());
    const auto parser = static_cast<xercesc::DOMImplementationLS*>(domImpl)->createLSParser(
        xercesc::DOMImplementationLS::MODE_SYNCHRONOUS,
        tc("http://www.w3.org/2001/XMLSchema").get());

    error_handler errorHandler;

    std::string xsd_str = osp_xsd;

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
                function.dimension = dimension;

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

    auto etNodes = rootElement->getElementsByTagName(tc("EndTime").get());
    if (etNodes->getLength() > 0) {
        simulationInformation_.endTime = boost::lexical_cast<double>(tc(etNodes->item(0)->getTextContent()).get());
    }

    auto saNodes = rootElement->getElementsByTagName(tc("Algorithm").get());
    if (saNodes->getLength() > 0) {
        simulationInformation_.algorithm = std::string(tc(saNodes->item(0)->getTextContent()).get());
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

osp_config_parser::~osp_config_parser() noexcept = default;

const osp_config_parser::SimulationInformation& osp_config_parser::get_simulation_information() const
{
    return simulationInformation_;
}

const std::vector<osp_config_parser::Simulator>& osp_config_parser::get_elements() const
{
    return simulators_;
}

const std::unordered_map<std::string, osp_config_parser::LinearTransformationFunction>&
osp_config_parser::get_linear_transformation_functions() const
{
    return linearTransformationFunctions_;
}

const std::unordered_map<std::string, osp_config_parser::SumFunction>&
osp_config_parser::get_sum_functions() const
{
    return sumFunctions_;
}

const std::unordered_map<std::string, osp_config_parser::VectorSumFunction>&
osp_config_parser::get_vector_sum_functions() const
{
    return vectorSumFunctions_;
}

const std::vector<osp_config_parser::VariableConnection>& osp_config_parser::get_variable_connections() const
{
    return variableConnections_;
}

const std::vector<osp_config_parser::SignalConnection>& osp_config_parser::get_signal_connections() const
{
    return signalConnections_;
}

const std::vector<osp_config_parser::VariableConnection>& osp_config_parser::get_variable_group_connections() const
{
    return variableGroupConnections_;
}

const std::vector<osp_config_parser::SignalConnection>& osp_config_parser::get_signal_group_connections() const
{
    return signalGroupConnections_;
}

variable_type osp_config_parser::parse_variable_type(const std::string& str)
{
    if ("Real" == str) {
        return cosim::variable_type::real;
    }
    if ("Integer" == str) {
        return cosim::variable_type::integer;
    }
    if ("Boolean" == str) {
        return cosim::variable_type::boolean;
    }
    if ("String" == str) {
        return cosim::variable_type::string;
    }
    if ("Enumeration" == str) {
        return cosim::variable_type::enumeration;
    }
    throw std::runtime_error("Failed to parse variable type: " + str);
}

bool osp_config_parser::parse_boolean_value(const std::string& s)
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

struct variable_group_description
{
    std::string name;
    std::string type;
    std::vector<std::string> variables;
    std::vector<variable_group_description> variable_group_descriptions;
};

struct extended_model_description
{
    explicit extended_model_description(const cosim::filesystem::path& ospModelDescription)
    {
        xercesc::XMLPlatformUtils::Initialize();
        const auto xerces_cleanup = final_action([]() {
            xercesc::XMLPlatformUtils::Terminate();
        });

        const auto domImpl = xercesc::DOMImplementationRegistry::getDOMImplementation(tc("LS").get());
        const auto parser = static_cast<xercesc::DOMImplementationLS*>(domImpl)->createLSParser(xercesc::DOMImplementationLS::MODE_SYNCHRONOUS, tc("http://www.w3.org/2001/XMLSchema").get());

        const auto doc = parser->parseURI(ospModelDescription.string().c_str());

        if (doc == nullptr) {
            std::ostringstream oss;
            oss << "Validation of " << ospModelDescription.string() << " failed.";
            BOOST_LOG_SEV(log::logger(), log::error) << oss.str();
            throw std::runtime_error(oss.str());
        }

        const auto rootElement = doc->getDocumentElement();

        const auto variableGroupsElement = static_cast<xercesc::DOMElement*>(rootElement->getElementsByTagName(tc("VariableGroups").get())->item(0));

        for (auto variableGroupElement = variableGroupsElement->getFirstElementChild(); variableGroupElement != nullptr; variableGroupElement = variableGroupElement->getNextElementSibling()) {
            variable_group_description vgd = create_variable_group_description(variableGroupElement);
            variableGroups[vgd.name] = std::move(vgd);
        }
    }

    static variable_group_description create_variable_group_description(xercesc::DOMElement* variableGroupElement)
    {
        variable_group_description variableGroupDescription;

        variableGroupDescription.name = tc(variableGroupElement->getAttribute(tc("name").get())).get();
        variableGroupDescription.type = tc(variableGroupElement->getTagName()).get();

        for (auto variableGroupChildElement = variableGroupElement->getFirstElementChild(); variableGroupChildElement != nullptr; variableGroupChildElement = variableGroupChildElement->getNextElementSibling()) {
            std::string tagName(tc(variableGroupChildElement->getTagName()).get());
            if (tagName == "Variable") {
                std::string variableName = tc(variableGroupChildElement->getAttribute(tc("ref").get())).get();
                variableGroupDescription.variables.push_back(std::move(variableName));
            } else {
                variableGroupDescription.variable_group_descriptions.push_back(create_variable_group_description(variableGroupChildElement));
            }
        }

        return variableGroupDescription;
    }

    std::unordered_map<std::string, variable_group_description> variableGroups;
};

namespace
{

osp_config_parser::SignalConnection find_signal_connection(
    const std::vector<osp_config_parser::SignalConnection>& signalConnections,
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

} // namespace

void connect_variables(
    const std::vector<osp_config_parser::VariableConnection>& variableConnections,
    system_structure& systemStructure)
{
    for (const auto& connection : variableConnections) {
        const auto variableA = full_variable_name(
            connection.variableA.simulator,
            connection.variableA.name);
        const auto variableB = full_variable_name(
            connection.variableB.simulator,
            connection.variableB.name);

        const auto causalityA =
            systemStructure.get_variable_description(variableA).causality;
        if (causalityA == variable_causality::output ||
            causalityA == variable_causality::calculated_parameter) {
            systemStructure.connect_variables(variableA, variableB);
        } else {
            systemStructure.connect_variables(variableB, variableA);
        }
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
    const osp_config_parser::VariableEndpoint& endpoint)
{
    std::vector<std::string> groupVariables;
    auto groupIt = descriptions.find(endpoint.name);
    if (groupIt == descriptions.end()) {
        for (const auto& description : descriptions) {
            if (description.second.variable_group_descriptions.size() != 0) {
                std::unordered_map<std::string, variable_group_description> nestedDescriptions;
                for (const auto& variableGroupDescr : description.second.variable_group_descriptions) {
                    nestedDescriptions.insert({variableGroupDescr.name, variableGroupDescr});
                }
                groupVariables = get_variable_group_variables(nestedDescriptions, endpoint);
                if (groupVariables.size() > 0) {
                    break;
                }
            }
        }
    } else {
        groupVariables = groupIt->second.variables;
    }
    return groupVariables;
}

std::vector<cosim::variable_group_description> get_variable_groups(
    const std::unordered_map<std::string, variable_group_description>& descriptions,
    const std::string& connector)
{
    auto groupIt = descriptions.find(connector);
    if (groupIt == descriptions.end()) {
        return {};
    }
    return groupIt->second.variable_group_descriptions;
}


void connect_variable_groups(
    const std::vector<osp_config_parser::VariableConnection>& variableGroupConnections,
    system_structure& systemStructure,
    const std::unordered_map<std::string, extended_model_description>& emds)
{

    for (const auto& connection : variableGroupConnections) {
        const auto& emdA = get_emd(emds, connection.variableA.simulator);
        const auto& emdB = get_emd(emds, connection.variableB.simulator);
        const auto& variablesA = get_variable_group_variables(emdA.variableGroups, connection.variableA);
        const auto& variablesB = get_variable_group_variables(emdB.variableGroups, connection.variableB);
        const auto& variableGroupsA = get_variable_groups(emdA.variableGroups, connection.variableA.name);
        const auto& variableGroupsB = get_variable_groups(emdB.variableGroups, connection.variableB.name);

        if (variableGroupsA.size() != variableGroupsB.size()) {
            std::ostringstream oss;
            oss << "Cannot create connection between variable groups. Variable group "
                << connection.variableA.simulator << ":" << connection.variableA.name
                << " has different size [" << variableGroupsA.size() << "] than variable group "
                << connection.variableB.simulator << ":" << connection.variableB.name
                << " size [" << variableGroupsB.size() << "]";
            throw std::runtime_error(oss.str());
        }

        std::vector<osp_config_parser::VariableConnection> nestedVariableGroupConnections;
        // clang-format off
        for (std::size_t i = 0; i < variableGroupsA.size(); ++i) {
            nestedVariableGroupConnections.push_back({
                {connection.variableA.simulator, variableGroupsA.at(i).name},
                {connection.variableB.simulator, variableGroupsB.at(i).name}});
        }
        // clang-format on
        connect_variable_groups(nestedVariableGroupConnections, systemStructure, emds);

        if (variablesA.size() != variablesB.size()) {
            std::ostringstream oss;
            oss << "Cannot create connection between variable groups. Variable group "
                << connection.variableA.simulator << ":" << connection.variableA.name
                << " has different size [" << variablesA.size() << "] than variable group "
                << connection.variableB.simulator << ":" << connection.variableB.name
                << " size [" << variablesB.size() << "]";
            throw std::runtime_error(oss.str());
        }

        std::vector<osp_config_parser::VariableConnection> variableConnections;

        // clang-format off
        for (std::size_t i = 0; i < variablesA.size(); ++i) {
            variableConnections.push_back({
                {connection.variableA.simulator, variablesA.at(i)},
                {connection.variableB.simulator, variablesB.at(i)}});
        }
        // clang-format on

        connect_variables(variableConnections, systemStructure);
    }
}

void connect_linear_transformation_functions(
    const std::unordered_map<std::string, osp_config_parser::LinearTransformationFunction>& functions,
    const std::vector<osp_config_parser::SignalConnection>& signalConnections,
    system_structure& systemStructure)
{
    for (const auto& [functionName, function] : functions) {
        function_parameter_value_map functionParams;
        functionParams[linear_transformation_function_type::offset_parameter_index] = function.offset;
        functionParams[linear_transformation_function_type::factor_parameter_index] = function.factor;
        systemStructure.add_entity(
            functionName,
            std::make_shared<linear_transformation_function_type>(),
            functionParams);

        const auto inConn = find_signal_connection(signalConnections, functionName, function.in.name);
        const auto outConn = find_signal_connection(signalConnections, functionName, function.out.name);
        systemStructure.connect_variables(
            full_variable_name(inConn.variable.simulator, inConn.variable.name),
            full_variable_name(functionName, "in", ""));
        systemStructure.connect_variables(
            full_variable_name(functionName, "out", ""),
            full_variable_name(outConn.variable.simulator, outConn.variable.name));
    }
}

void connect_sum_functions(
    const std::unordered_map<std::string, osp_config_parser::SumFunction>& functions,
    const std::vector<osp_config_parser::SignalConnection>& signalConnections,
    system_structure& systemStructure)
{
    for (const auto& [functionName, function] : functions) {
        function_parameter_value_map functionParams;
        functionParams[vector_sum_function_type::inputCount_parameter_index] = function.inputCount;
        systemStructure.add_entity(
            functionName,
            std::make_shared<vector_sum_function_type>(),
            functionParams);

        for (int i = 0; i < function.inputCount; i++) {
            const auto inConn = find_signal_connection(signalConnections, functionName, function.ins.at(i).name);
            systemStructure.connect_variables(
                full_variable_name(inConn.variable.simulator, inConn.variable.name),
                full_variable_name(functionName, "in", i, "", 0));
        }
        const auto outConn = find_signal_connection(signalConnections, functionName, function.out.name);
        systemStructure.connect_variables(
            full_variable_name(functionName, "out", ""),
            full_variable_name(outConn.variable.simulator, outConn.variable.name));
    }
}

void connect_vector_sum_functions(
    const std::unordered_map<std::string, osp_config_parser::VectorSumFunction>& functions,
    const std::vector<osp_config_parser::SignalConnection>& signalGroupConnections,
    system_structure& systemStructure,
    const std::unordered_map<std::string, extended_model_description>& emds)
{
    for (const auto& [functionName, function] : functions) {
        function_parameter_value_map functionParams;
        functionParams[vector_sum_function_type::inputCount_parameter_index] = function.inputCount;
        functionParams[vector_sum_function_type::dimension_parameter_index] = function.dimension;
        systemStructure.add_entity(
            functionName,
            std::make_shared<vector_sum_function_type>(),
            functionParams);

        for (int i = 0; i < function.inputCount; ++i) {
            const auto inGroupConn = find_signal_connection(
                signalGroupConnections, functionName, function.inGroups.at(i).name);
            const auto& inEmd = emds.at(inGroupConn.variable.simulator);
            const auto& inVariableGroup = inEmd.variableGroups.at(inGroupConn.variable.name);

            for (int j = 0; j < function.dimension; ++j) {
                const auto& inVariableName = inVariableGroup.variables.at(j);
                systemStructure.connect_variables(
                    full_variable_name(inGroupConn.variable.simulator, inVariableName),
                    full_variable_name(functionName, "in", i, "", j));
            }
        }

        const auto outGroupConn = find_signal_connection(
            signalGroupConnections, functionName, function.outGroup.name);
        const auto& outEmd = emds.at(outGroupConn.variable.simulator);
        const auto& outVariableGroup = outEmd.variableGroups.at(outGroupConn.variable.name);
        for (int j = 0; j < function.dimension; ++j) {
            const auto& outVariableName = outVariableGroup.variables.at(j);
            systemStructure.connect_variables(
                full_variable_name(functionName, "out", 0, "", j),
                full_variable_name(outGroupConn.variable.simulator, outVariableName));
        }
    }
}

cosim::filesystem::path file_query_uri_to_path(
    const cosim::uri& baseParentUri,
    const cosim::uri& queryUri)
{
    const auto query = queryUri.query();
    if (query && query->find("file=") < query->size()) {
        if (query->find("file=file:///") < query->size()) {
            return file_uri_to_path(std::string(query->substr(5)));
        }
        return file_uri_to_path(std::string(baseParentUri.view()) + "/" + std::string(query->substr(5)));
    }
    return file_uri_to_path(baseParentUri);
}

} // namespace

osp_config load_osp_config(
    const cosim::filesystem::path& configPath,
    model_uri_resolver& resolver)
{
    const auto absolutePath = cosim::filesystem::absolute(configPath);
    const auto configFile = cosim::filesystem::is_regular_file(absolutePath)
        ? absolutePath
        : absolutePath / "OspSystemStructure.xml";

    const auto baseURI = path_to_file_uri(configFile);
    const auto parser = osp_config_parser(configFile);
    const auto& simInfo = parser.get_simulation_information();

    if (simInfo.stepSize <= 0.0) {
        std::ostringstream oss;
        oss << "Configured base step size [" << simInfo.stepSize << "] must be nonzero and positive";
        BOOST_LOG_SEV(log::logger(), log::error) << oss.str();
        throw std::invalid_argument(oss.str());
    }

    if (simInfo.endTime.has_value() && simInfo.startTime > simInfo.endTime.value()) {
        std::ostringstream oss;
        oss << "Configured start time [" << simInfo.startTime << "] is larger than configured end time [" << simInfo.endTime.value() << "]";
        BOOST_LOG_SEV(log::logger(), log::error) << oss.str();
        throw std::invalid_argument(oss.str());
    }

    osp_config config;
    config.start_time = to_time_point(simInfo.startTime);
    config.step_size = to_duration(simInfo.stepSize);
    if (simInfo.endTime.has_value()) {
        config.end_time = to_time_point(simInfo.endTime.value());
    }

    auto simulators = parser.get_elements();

    std::unordered_map<std::string, extended_model_description> emds;
    for (const auto& simulator : simulators) {
        auto model = resolver.lookup_model(baseURI, simulator.source);
        config.system_structure.add_entity(
            simulator.name,
            model,
            simulator.stepSize ? to_duration(*simulator.stepSize) : duration::zero());

        for (const auto& p : simulator.initialValues) {
            add_variable_value(
                config.initial_values,
                config.system_structure,
                full_variable_name(simulator.name, p.name),
                p.value);
        }

        std::string msmiFileName = model->description()->name + "_OspModelDescription.xml";
        const auto modelUri = resolve_reference(baseURI, simulator.source);
        cosim::filesystem::path msmiFilePath;

        msmiFilePath = (modelUri.scheme() == "file")
            ? file_uri_to_path(modelUri).remove_filename() / msmiFileName
            : file_query_uri_to_path(cosim::path_to_file_uri(cosim::file_uri_to_path(baseURI).parent_path()), modelUri).remove_filename() / msmiFileName;
        if (cosim::filesystem::exists(msmiFilePath)) {
            emds.emplace(simulator.name, msmiFilePath);
        } else {
            msmiFilePath = configFile.parent_path() / msmiFileName;
            if (cosim::filesystem::exists(msmiFilePath)) {
                emds.emplace(simulator.name, msmiFilePath);
            }
        }
    }

    connect_variables(parser.get_variable_connections(), config.system_structure);
    connect_variable_groups(parser.get_variable_group_connections(), config.system_structure, emds);
    connect_linear_transformation_functions(parser.get_linear_transformation_functions(), parser.get_signal_connections(), config.system_structure);
    connect_sum_functions(parser.get_sum_functions(), parser.get_signal_connections(), config.system_structure);
    connect_vector_sum_functions(parser.get_vector_sum_functions(), parser.get_signal_group_connections(), config.system_structure, emds);

    return config;
}

} // namespace cosim
