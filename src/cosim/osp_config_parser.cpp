/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/osp_config_parser.hpp"

#include "osp_system_structure_schema.hpp"
#include "osp_system_structure_schema_020.hpp"

#include "cosim/algorithm/ecco_algorithm.hpp"
#include "cosim/algorithm/fixed_step_algorithm.hpp"
#include "cosim/exception.hpp"
#include "cosim/function/description.hpp"
#include "cosim/function/linear_transformation.hpp"
#include "cosim/function/vector_sum.hpp"
#include "cosim/log/logger.hpp"
#include "cosim/model_description.hpp"
#include "cosim/orchestration.hpp"
#include "cosim/osp_clock.hpp"
#include "cosim/system_structure.hpp"
#include "cosim/time.hpp"
#include "cosim/uri.hpp"
#include "cosim/xml_clock.hpp"
#include "cosim/xml_value.hpp"

#include <boost/algorithm/cxx11/copy_if.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/sources/severity_feature.hpp>
#include <boost/range/algorithm/find_if.hpp>
#include <boost/range/algorithm/sort.hpp>
#include <boost/range/algorithm/transform.hpp>
#include <boost/range/algorithm/unique.hpp>
#include <xercesc/dom/DOM.hpp> // IWYU pragma: keep
#include <xercesc/dom/DOMElement.hpp>
#include <xercesc/dom/DOMError.hpp>
#include <xercesc/dom/DOMErrorHandler.hpp>
#include <xercesc/dom/DOMImplementationRegistry.hpp>
#include <xercesc/dom/DOMLSParser.hpp>
#include <xercesc/dom/DOMLocator.hpp>
#include <xercesc/dom/DOMNodeList.hpp>
#include <xercesc/framework/MemBufInputSource.hpp>
#include <xercesc/framework/Wrapper4InputSource.hpp>
#include <xercesc/util/PlatformUtils.hpp>
#include <xercesc/util/XMLString.hpp>
#include <xercesc/util/XMLUni.hpp>
#include <xercesc/util/XercesDefs.hpp>
#include <xercesc/validators/common/Grammar.hpp>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cosim
{

namespace
{
std::shared_ptr<XMLCh> tc(const char* str) // NOLINT(misc-include-cleaner)
{
    return {xercesc::XMLString::transcode(str),
        [](XMLCh* ptr) { xercesc::XMLString::release(&ptr); }};
}
std::shared_ptr<char> tc(const XMLCh* str)
{
    return {xercesc::XMLString::transcode(str),
        [](char* ptr) { xercesc::XMLString::release(&ptr); }};
}

class error_handler : public xercesc::DOMErrorHandler
{
public:
    [[nodiscard]] bool failed() const { return failed_; }

    bool handleError(const xercesc::DOMError& domError) override
    {
        bool const warn(domError.getSeverity() ==
            xercesc::DOMError::DOM_SEVERITY_WARNING);

        if (!warn) {
            failed_ = true;
        }

        xercesc::DOMLocator const* loc(domError.getLocation());

        BOOST_LOG_SEV(log::logger(), warn ? log::warning : log::error)
            << tc(loc->getURI()).get() << ":" << loc->getLineNumber() << ":"
            << loc->getColumnNumber() << " " << tc(domError.getMessage()).get();

        return true;
    }

private:
    bool failed_ = false;
};

struct dom_parser_deleter
{
    void operator()(xercesc::DOMLSParser* parser) const noexcept
    {
        parser->release();
    }
};

using dom_parser_ptr =
    std::unique_ptr<xercesc::DOMLSParser, dom_parser_deleter>;

constexpr auto osp_namespace =
    "http://opensimulationplatform.com/MSMI/OSPSystemStructure";

auto osp_elements(const xercesc::DOMElement* parent, const char* localName)
{
    return parent->getElementsByTagNameNS(tc(osp_namespace).get(),
        tc(localName).get());
}

xercesc::DOMElement* tagged_element(const xercesc::DOMNodeList* elements, std::size_t index)
{
    // getElementsByTagName and getElementsByTagNameNS return only elements.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    return static_cast<xercesc::DOMElement*>(elements->item(index));
}

dom_parser_ptr make_dom_parser(error_handler& errors)
{
    auto* const domImpl =
        xercesc::DOMImplementationRegistry::getDOMImplementation(tc("LS").get());
    dom_parser_ptr parser(
        static_cast<xercesc::DOMImplementationLS*>(domImpl)->createLSParser(
            xercesc::DOMImplementationLS::MODE_SYNCHRONOUS,
            tc("http://www.w3.org/2001/XMLSchema").get()));

    auto& config = *parser->getDomConfig();
    config.setParameter(static_cast<const XMLCh*>(xercesc::XMLUni::fgDOMErrorHandler), &errors);
    config.setParameter(static_cast<const XMLCh*>(xercesc::XMLUni::fgDOMNamespaces), true);
    config.setParameter(static_cast<const XMLCh*>(xercesc::XMLUni::fgXercesLoadExternalDTD), false);
    return parser;
}

dom_parser_ptr make_schema_validating_parser(std::string_view schema,
    error_handler& errors)
{
    auto parser = make_dom_parser(errors);
    // Xerces consumes the schema as a byte buffer.
    xercesc::MemBufInputSource source(
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        reinterpret_cast<const XMLByte*>(schema.data()), schema.size(),
        osp_namespace);
    xercesc::Wrapper4InputSource input(&source, false);
    parser->loadGrammar(&input, xercesc::Grammar::SchemaGrammarType, true);

    auto& config = *parser->getDomConfig();
    config.setParameter(static_cast<const XMLCh*>(xercesc::XMLUni::fgXercesUseCachedGrammarInParse), true);
    config.setParameter(static_cast<const XMLCh*>(xercesc::XMLUni::fgDOMComments), false);
    config.setParameter(static_cast<const XMLCh*>(xercesc::XMLUni::fgDOMDatatypeNormalization), true);
    config.setParameter(static_cast<const XMLCh*>(xercesc::XMLUni::fgDOMEntities), false);
    config.setParameter(static_cast<const XMLCh*>(xercesc::XMLUni::fgDOMElementContentWhitespace), false);
    config.setParameter(static_cast<const XMLCh*>(xercesc::XMLUni::fgDOMValidate), true);
    config.setParameter(static_cast<const XMLCh*>(xercesc::XMLUni::fgXercesSchema), true);
    config.setParameter(static_cast<const XMLCh*>(xercesc::XMLUni::fgXercesSchemaFullChecking), true);
    config.setParameter(static_cast<const XMLCh*>(xercesc::XMLUni::fgXercesLoadSchema), false);
    return parser;
}

// The following class was copied from Microsoft Guidelines Support Library
// https://github.com/microsoft/GSL
// final_action allows you to ensure something gets run at the end of a scope
template<class F>
class final_action
{
public:
    explicit final_action(F action) noexcept
        : f_(std::move(action))
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
        if (invoke_) {
            f_();
        }
    }

private:
    F f_;
    bool invoke_{true};
};

class osp_config_parser
{

public:
    explicit osp_config_parser(const cosim::filesystem::path& configPath);
    osp_config_parser(const osp_config_parser&) = delete;
    osp_config_parser& operator=(const osp_config_parser&) = delete;
    osp_config_parser(osp_config_parser&&) = delete;
    osp_config_parser& operator=(osp_config_parser&&) = delete;
    ~osp_config_parser() noexcept;

    struct SimulationInformation
    {
        std::string description;
        std::string algorithm = "fixedStep";
        double stepSize = 0.1;
        double startTime = 0.0;
        std::optional<double> endTime;
        std::optional<cosim::ecco_algorithm_params> eccoConfiguration;
    };

    [[nodiscard]] const SimulationInformation& get_simulation_information() const;

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
        variable_value value;
    };

    struct Simulator
    {
        std::string name;
        std::string source;
        std::optional<double> stepSize;
        std::vector<InitialValue> initialValues;
    };

    [[nodiscard]] const std::vector<Simulator>& get_elements() const;

    [[nodiscard]] const auto& get_clocks() const noexcept { return clocks_; }

    struct Signal
    {
        std::string name;
        cosim::variable_type type{};
        cosim::variable_causality causality{};
    };

    struct SignalGroup
    {
        std::string name;
        std::vector<std::string> signals;
    };

    struct LinearTransformationFunction
    {
        std::string name;
        double factor{};
        double offset{};
        Signal in;
        Signal out;
    };

    struct SumFunction
    {
        std::string name;
        int inputCount{};
        std::vector<Signal> ins;
        Signal out;
    };

    struct VectorSumFunction
    {
        std::string name;
        int inputCount{};
        int dimension{};
        std::vector<Signal> ins;
        std::vector<Signal> outs;
        std::vector<SignalGroup> inGroups;
        SignalGroup outGroup;
    };

    [[nodiscard]] const std::unordered_map<std::string,
        LinearTransformationFunction>&
    get_linear_transformation_functions() const;
    [[nodiscard]] const std::unordered_map<std::string, SumFunction>&
    get_sum_functions() const;
    [[nodiscard]] const std::unordered_map<std::string, VectorSumFunction>&
    get_vector_sum_functions() const;

    struct VariableEndpoint
    {
        std::string simulator;
        std::string name;
        std::optional<std::string>
            causality{}; // NOLINT(readability-redundant-member-init)
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

    struct PowerBondConnection
    {
        std::string name;
        VariableConnection connection;
    };

    struct PowerBond
    {
        std::string name;
        PowerBondConnection connA;
        PowerBondConnection connB;
    };

    [[nodiscard]] const std::vector<VariableConnection>&
    get_variable_connections() const;
    [[nodiscard]] const std::vector<SignalConnection>&
    get_signal_connections() const;
    [[nodiscard]] const std::vector<VariableConnection>&
    get_variable_group_connections() const;
    [[nodiscard]] const std::vector<SignalConnection>&
    get_signal_group_connections() const;
    [[nodiscard]] const std::vector<PowerBondConnection>&
    get_power_bond_connections() const;

private:
    void read_simulators(const xercesc::DOMElement* root, bool exactTypes);
    static std::vector<InitialValue> read_initial_values(
        const xercesc::DOMElement* simulator, const std::string& name, bool exactTypes);
    static void read_initial_value(
        const xercesc::DOMElement* element, const std::string& name, bool exactTypes,
        std::vector<InitialValue>& initialValues);
    void read_clocks(const xercesc::DOMElement* root);
    void read_functions(const xercesc::DOMElement* root);
    void read_simulation_information(const xercesc::DOMElement* root);
    void read_connections(const xercesc::DOMElement* root);

    SystemDescription systemDescription_;
    SimulationInformation simulationInformation_;
    std::vector<Simulator> simulators_;
    std::vector<std::pair<std::string, clock_description>> clocks_;
    std::unordered_map<std::string, LinearTransformationFunction>
        linearTransformationFunctions_;
    std::unordered_map<std::string, SumFunction> sumFunctions_;
    std::unordered_map<std::string, VectorSumFunction> vectorSumFunctions_;
    std::vector<VariableConnection> variableConnections_;
    std::vector<VariableConnection> variableGroupConnections_;
    std::vector<SignalConnection> signalConnections_;
    std::vector<SignalConnection> signalGroupConnections_;
    std::vector<PowerBondConnection> powerBondConnections_;
    std::vector<PowerBond> powerBonds_;
};

namespace
{

template<typename T>
T attribute_or(xercesc::DOMElement* element, const char* attributeName,
    T defaultValue)
{
    auto const node = tc(element->getAttribute(tc(attributeName).get()));
    if (*node) {
        return boost::lexical_cast<T>(node);
    } else {
        return defaultValue;
    }
}

} // namespace

[[maybe_unused]] std::ostream&
operator<<(std::ostream& output,
    const osp_config_parser::VariableEndpoint& var)
{
    const auto causality = var.causality.has_value() ? var.causality.value() : "";
    return output << var.simulator << ", " << var.name << ", " << causality
                  // NOLINTNEXTLINE(performance-avoid-endl): Preserve the stream flush.
                  << std::endl;
}

[[maybe_unused]] std::ostream&
operator<<(std::ostream& output,
    const osp_config_parser::VariableConnection& var)
{
    return output << "Variable connection [simulator, variable name, ecco]:"
                  // NOLINTNEXTLINE(performance-avoid-endl): Preserve the stream flush.
                  << std::endl
                  << var.variableA << var.variableB;
}

[[maybe_unused]] std::ostream&
operator<<(std::ostream& output,
    const osp_config_parser::PowerBondConnection& bond)
{
    return output << "Powerbond " << bond.name << " has the connection"
                  // NOLINTNEXTLINE(performance-avoid-endl): Preserve the stream flush.
                  << std::endl
                  << bond.connection;
}

clock_description parse_osp_clock(const xercesc::DOMElement& element)
{
    const auto get = [&](const char* name) -> std::optional<std::string> {
        const auto key = tc(name);
        if (!element.hasAttribute(key.get())) {
            return std::nullopt;
        }
        return tc(element.getAttribute(key.get())).get();
    };
    using detail::xml_clock_attribute;
    using V = clock_interval_variability;
    clock_description schedule;
    schedule.interval_variability =
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): Missing required attributes retain value()'s exception.
        xml_clock_attribute<V>(get, "intervalVariability").value();
    schedule.interval_decimal =
        xml_clock_attribute<double>(get, "intervalDecimal");
    schedule.shift_decimal = xml_clock_attribute<double>(get, "shiftDecimal");
    schedule.interval_counter =
        xml_clock_attribute<std::uint64_t>(get, "intervalCounter");
    schedule.shift_counter =
        xml_clock_attribute<std::uint64_t>(get, "shiftCounter");
    schedule.resolution = xml_clock_attribute<std::uint64_t>(get, "resolution");
    schedule.priority = xml_clock_attribute<std::uint32_t>(get, "priority");
    schedule.supports_fraction =
        xml_clock_attribute<bool>(get, "supportsFraction").value_or(false);
    schedule.can_be_deactivated =
        xml_clock_attribute<bool>(get, "canBeDeactivated").value_or(false);
    const bool decimal = schedule.interval_decimal || schedule.shift_decimal;
    const bool fractional = schedule.interval_counter || schedule.shift_counter ||
        schedule.resolution;

    switch (schedule.interval_variability) {
        case V::triggered:
            if (decimal || fractional || schedule.supports_fraction) {
                throw std::invalid_argument(
                    "A triggered Clock cannot declare an interval or shift");
            }
            return schedule;
        case V::constant:
        case V::fixed:
        case V::tunable:
            break;
        default:
            throw std::invalid_argument(
                "changing and countdown require an FMU-provided runtime interval");
    }

    if (schedule.supports_fraction) {
        if (decimal || !schedule.interval_counter || !schedule.resolution ||
            *schedule.interval_counter == 0 || *schedule.resolution == 0) {
            throw std::invalid_argument(
                "A fractional Clock requires positive intervalCounter and "
                "resolution, with optional shiftCounter only");
        }
        constexpr auto ticksPerSecond =
            static_cast<std::uint64_t>(duration::period::den);
        const auto resolution = *schedule.resolution;
        // Use the exact ratio for the lower bound without multiplying two counters.
        const auto minCounter =
            (resolution / ticksPerSecond) +
            static_cast<unsigned long long>(resolution % ticksPerSecond != 0);
        if (*schedule.interval_counter < minCounter) {
            throw std::invalid_argument(
                "Clock interval must be finite and at least one simulation tick");
        }
        // Keep the decimal projection for consumers of the common Clock metadata.
        schedule.interval_decimal =
            static_cast<double>(*schedule.interval_counter) /
            static_cast<double>(resolution);
    } else if (fractional || !schedule.interval_decimal) {
        throw std::invalid_argument("A decimal Clock requires intervalDecimal; "
                                    "fractional fields require supportsFraction");
    } else if (!std::isfinite(*schedule.interval_decimal) ||
        *schedule.interval_decimal < 1.0 / duration::period::den) {
        throw std::invalid_argument(
            "Clock interval must be finite and at least one simulation tick");
    }
    if (schedule.shift_decimal && (!std::isfinite(*schedule.shift_decimal) || *schedule.shift_decimal < 0)) {
        throw std::invalid_argument("Clock shift must be finite and nonnegative");
    }
    return schedule;
}

osp_config_parser::osp_config_parser(
    const cosim::filesystem::path& configPath)
{
    xercesc::XMLPlatformUtils::Initialize();
    const auto xerces_cleanup =
        final_action([] { xercesc::XMLPlatformUtils::Terminate(); });

    error_handler errorHandler;
    const auto version = [&] {
        const auto unvalidated = make_dom_parser(errorHandler);
        auto const* const doc = unvalidated->parseURI(configPath.string().c_str());
        auto const* const root = doc ? doc->getDocumentElement() : nullptr;
        if (errorHandler.failed() || !root || !root->getNamespaceURI() ||
            std::string_view(tc(root->getNamespaceURI()).get()) != osp_namespace ||
            std::string_view(tc(root->getLocalName()).get()) !=
                "OspSystemStructure") {
            throw std::runtime_error("Validation of " + configPath.string() +
                " failed: invalid OspSystemStructure root.");
        }
        return std::string(tc(root->getAttribute(tc("version").get())).get());
    }();
    if (version != "0.1" && version != "0.2.0") {
        throw std::runtime_error(
            "Validation of " + configPath.string() + " failed: " +
            (version.empty()
                    ? "missing OspSystemStructure version."
                    : "unsupported OspSystemStructure version: " + version));
    }
    const bool exactTypes = version == "0.2.0";
    const auto parser = make_schema_validating_parser(
        exactTypes ? osp_xsd_020 : osp_xsd_01, errorHandler);
    auto const* const doc = parser->parseURI(configPath.string().c_str());

    if (errorHandler.failed() || (doc == nullptr)) {
        std::ostringstream oss;
        oss << "Validation of " << configPath.string() << " failed.";
        BOOST_LOG_SEV(log::logger(), log::error) << oss.str();
        throw std::runtime_error(oss.str());
    }

    auto const* const rootElement = doc->getDocumentElement();

    read_simulators(rootElement, exactTypes);
    read_clocks(rootElement);
    read_functions(rootElement);
    read_simulation_information(rootElement);
    read_connections(rootElement);
}

void osp_config_parser::read_simulators(const xercesc::DOMElement* rootElement, bool exactTypes)
{
    auto const* const simulators = tagged_element(osp_elements(rootElement, "Simulators"), 0);
    for (auto const* element = simulators->getFirstElementChild();
        element != nullptr; element = element->getNextElementSibling()) {
        std::string const name = tc(element->getAttribute(tc("name").get())).get();
        std::string const source =
            tc(element->getAttribute(tc("source").get())).get();

        std::optional<double> stepSize = std::nullopt;
        auto const stepSizeNode = tc(element->getAttribute(tc("stepSize").get()));
        if ((*stepSizeNode) != 0) {
            stepSize = boost::lexical_cast<double>(stepSizeNode);
        }

        const auto initialValues = read_initial_values(element, name, exactTypes);
        simulators_.push_back({name, source, stepSize, initialValues});
    }
}

std::vector<osp_config_parser::InitialValue> osp_config_parser::read_initial_values(
    const xercesc::DOMElement* simulator, const std::string& name, bool exactTypes)
{
    std::vector<InitialValue> initialValues;
    auto const* const initValsElement = tagged_element(osp_elements(simulator, "InitialValues"), 0);
    if (initValsElement != nullptr) {
        for (auto const* initValElement = initValsElement->getFirstElementChild();
            initValElement != nullptr;
            initValElement = initValElement->getNextElementSibling()) {
            read_initial_value(initValElement, name, exactTypes, initialValues);
        }
    }
    return initialValues;
}

void osp_config_parser::read_initial_value(
    const xercesc::DOMElement* initValElement, const std::string& name, bool exactTypes,
    std::vector<InitialValue>& initialValues)
{
    const std::string varName =
        tc(initValElement->getAttribute(tc("variable").get())).get();
    auto const* const typeElement = initValElement->getFirstElementChild();
    const auto typeName = tc(typeElement->getLocalName());
    const auto parsedType = detail::xml_variable_type(typeName.get());
    if (!parsedType) {
        std::ostringstream message;
        message << "Invalid OSP initial value for '" << name << '.' << varName
                << "': unsupported type " << typeName.get();
        throw std::runtime_error(message.str());
    }
    const auto type = *parsedType;
    const auto valueAttribute = tc("value");
    const auto text = tc(typeElement->getAttribute(valueAttribute.get()));
    try {
        if (!exactTypes) {
            const auto scalar = [&] {
                if (type == variable_type::real) {
                    return scalar_value{boost::lexical_cast<double>(text.get())};
                }
                if (type == variable_type::integer) {
                    return scalar_value{boost::lexical_cast<int>(text.get())};
                }
                return detail::parse_xml_scalar_value(type, text.get());
            }();
            initialValues.push_back({varName, variable_value(scalar)});
            return;
        }
        std::vector<std::uint64_t> shape;
        for (auto const* dim = typeElement->getNextElementSibling();
            dim != nullptr; dim = dim->getNextElementSibling()) {
            shape.push_back(detail::parse_xml_integer<std::uint64_t>(
                tc(dim->getAttribute(tc("size").get())).get()));
        }
        std::vector<std::shared_ptr<char>> values;
        std::vector<std::string_view> items;
        for (auto const* item = typeElement->getFirstElementChild();
            item != nullptr; item = item->getNextElementSibling()) {
            values.push_back(tc(item->getAttribute(valueAttribute.get())));
            items.emplace_back(values.back().get());
        }
        const auto attribute =
            typeElement->hasAttribute(valueAttribute.get())
            ? std::optional<std::string_view>(text.get())
            : std::nullopt;
        initialValues.push_back({
            varName,
            detail::parse_xml_value(type, attribute, std::move(items),
                std::move(shape)),
        });
    } catch (const std::logic_error& e) {
        std::ostringstream message;
        message << "Invalid OSP initial value for '" << name << '.' << varName
                << "': " << e.what();
        throw std::runtime_error(message.str());
    } catch (const std::overflow_error& e) {
        std::ostringstream message;
        message << "Invalid OSP initial value for '" << name << '.' << varName
                << "': " << e.what();
        throw std::runtime_error(message.str());
    }
}

void osp_config_parser::read_clocks(const xercesc::DOMElement* rootElement)
{
    if (auto const* const clocks = tagged_element(osp_elements(rootElement, "Clocks"), 0)) {
        for (auto const* clock = clocks->getFirstElementChild(); clock != nullptr;
            clock = clock->getNextElementSibling()) {
            const std::string name = tc(clock->getAttribute(tc("name").get())).get();
            try {
                clocks_.emplace_back(name, parse_osp_clock(*clock));
            } catch (const std::logic_error& e) {
                throw std::runtime_error("Invalid OSP Clock '" + name +
                    "': " + e.what());
            }
        }
    }
}

void osp_config_parser::read_functions(const xercesc::DOMElement* rootElement)
{
    auto const* const functionsElement = tagged_element(osp_elements(rootElement, "Functions"), 0);
    if (functionsElement == nullptr) {
        return;
    }
    for (auto* functionElement = functionsElement->getFirstElementChild();
        functionElement != nullptr;
        functionElement = functionElement->getNextElementSibling()) {

        const auto functionNodeName =
            std::string(tc(functionElement->getLocalName()).get());
        const auto functionName = std::string(
            tc(functionElement->getAttribute(tc("name").get())).get());
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

            int const inputCount = boost::lexical_cast<int>(
                tc(functionElement->getAttribute(tc("inputCount").get())));
            function.inputCount = inputCount;

            for (int i = 0; i < inputCount; i++) {
                const std::string sigName = "in[" + std::to_string(i) + "]";
                function.ins.push_back(
                    {sigName, variable_type::real, variable_causality::input});
            }
            function.out = {"out", variable_type::real, variable_causality::output};

            sumFunctions_[functionName] = function;

        } else if ("VectorSum" == functionNodeName) {
            VectorSumFunction function;
            function.name = functionName;

            int const inputCount = boost::lexical_cast<int>(
                tc(functionElement->getAttribute(tc("inputCount").get())));
            function.inputCount = inputCount;
            int const dimension = boost::lexical_cast<int>(
                tc(functionElement->getAttribute(tc("dimension").get())));
            function.dimension = dimension;

            for (int i = 0; i < inputCount; i++) {
                const std::string sgName = "in[" + std::to_string(i) + "]";
                SignalGroup group;
                group.name = sgName;
                for (int j = 0; j < dimension; j++) {
                    const std::string sigName = sgName + "[" + std::to_string(j) + "]";
                    function.ins.push_back(
                        {sigName, variable_type::real, variable_causality::input});
                    group.signals.emplace_back(sigName);
                }
                function.inGroups.push_back(std::move(group));
            }
            SignalGroup group;
            group.name = "out";
            for (int j = 0; j < dimension; j++) {
                const std::string sigName = "out[" + std::to_string(j) + "]";
                function.outs.push_back(
                    {sigName, variable_type::real, variable_causality::output});
                group.signals.emplace_back(sigName);
            }
            function.outGroup = group;

            vectorSumFunctions_[functionName] = function;

        } else {
            std::ostringstream oss;
            oss << "Found unknown function type " << functionNodeName
                << " with name " << functionName;
            throw std::runtime_error(oss.str());
        }
    }
}

void osp_config_parser::read_simulation_information(const xercesc::DOMElement* rootElement)
{
    auto const* descNodes = osp_elements(rootElement, "Description");
    if (descNodes->getLength() > 0) {
        simulationInformation_.description =
            tc(descNodes->item(0)->getTextContent()).get();
    }

    auto const* ssNodes = osp_elements(rootElement, "BaseStepSize");
    if (ssNodes->getLength() > 0) {
        simulationInformation_.stepSize =
            boost::lexical_cast<double>(tc(ssNodes->item(0)->getTextContent()));
    }

    auto const* stNodes = osp_elements(rootElement, "StartTime");
    if (stNodes->getLength() > 0) {
        simulationInformation_.startTime = boost::lexical_cast<double>(
            tc(stNodes->item(0)->getTextContent()).get());
    }

    auto const* etNodes = osp_elements(rootElement, "EndTime");
    if (etNodes->getLength() > 0) {
        simulationInformation_.endTime = boost::lexical_cast<double>(
            tc(etNodes->item(0)->getTextContent()).get());
    }

    auto const* saNodes = osp_elements(rootElement, "Algorithm");
    if (saNodes->getLength() > 0) {
        simulationInformation_.algorithm =
            std::string(tc(saNodes->item(0)->getTextContent()).get());
    }

    auto const* eccoConfigurationElement = tagged_element(osp_elements(rootElement, "EccoConfiguration"), 0);
    if (eccoConfigurationElement != nullptr) {
        const auto* const safetyFactor =
            osp_elements(eccoConfigurationElement, "SafetyFactor")
                ->item(0)
                ->getTextContent();
        const auto* const stepSize =
            osp_elements(eccoConfigurationElement, "StepSize")
                ->item(0)
                ->getTextContent();
        const auto* const minimumStepSize =
            osp_elements(eccoConfigurationElement, "MinimumStepSize")
                ->item(0)
                ->getTextContent();
        const auto* const maximumStepSize =
            osp_elements(eccoConfigurationElement, "MaximumStepSize")
                ->item(0)
                ->getTextContent();
        const auto* const minimumChangeRate =
            osp_elements(eccoConfigurationElement, "MinimumChangeRate")
                ->item(0)
                ->getTextContent();
        const auto* const maximumChangeRate =
            osp_elements(eccoConfigurationElement, "MaximumChangeRate")
                ->item(0)
                ->getTextContent();
        const auto* const proportionalGain =
            osp_elements(eccoConfigurationElement, "ProportionalGain")
                ->item(0)
                ->getTextContent();
        const auto* const integralGain =
            osp_elements(eccoConfigurationElement, "IntegralGain")
                ->item(0)
                ->getTextContent();
        const auto* const relativeTolerance =
            osp_elements(eccoConfigurationElement, "RelativeTolerance")
                ->item(0)
                ->getTextContent();
        const auto* const absoluteTolerance =
            osp_elements(eccoConfigurationElement, "AbsoluteTolerance")
                ->item(0)
                ->getTextContent();

        cosim::ecco_algorithm_params eccoConfig{};

        eccoConfig.safety_factor = boost::lexical_cast<double>(tc(safetyFactor));
        eccoConfig.step_size =
            cosim::to_duration(boost::lexical_cast<double>(tc(stepSize)));
        eccoConfig.min_step_size =
            cosim::to_duration(boost::lexical_cast<double>(tc(minimumStepSize)));
        eccoConfig.max_step_size =
            cosim::to_duration(boost::lexical_cast<double>(tc(maximumStepSize)));
        eccoConfig.min_change_rate =
            boost::lexical_cast<double>(tc(minimumChangeRate));
        eccoConfig.max_change_rate =
            boost::lexical_cast<double>(tc(maximumChangeRate));
        eccoConfig.p_gain = boost::lexical_cast<double>(tc(proportionalGain));
        eccoConfig.i_gain = boost::lexical_cast<double>(tc(integralGain));
        eccoConfig.rel_tolerance =
            boost::lexical_cast<double>(tc(relativeTolerance));
        eccoConfig.abs_tolerance =
            boost::lexical_cast<double>(tc(absoluteTolerance));

        simulationInformation_.eccoConfiguration = eccoConfig;
    }
}

void osp_config_parser::read_connections(const xercesc::DOMElement* rootElement)
{
    auto const* connectionsElement = tagged_element(osp_elements(rootElement, "Connections"), 0);
    if (connectionsElement != nullptr) {
        auto const* variableConnectionsElement =
            osp_elements(connectionsElement, "VariableConnection");
        for (size_t i = 0; i < variableConnectionsElement->getLength(); i++) {
            auto const* connectionElement = tagged_element(variableConnectionsElement, i);

            auto const* first = tagged_element(osp_elements(connectionElement, "Variable"), 0);
            auto const* second = tagged_element(osp_elements(connectionElement, "Variable"), 1);

            std::string const simulatorA =
                tc(first->getAttribute(tc("simulator").get())).get();
            std::string const nameA = tc(first->getAttribute(tc("name").get())).get();
            std::string varA = tc(first->getAttribute(tc("causality").get())).get();
            VariableEndpoint const veA = {simulatorA, nameA, varA};

            std::string const simulatorB =
                tc(second->getAttribute(tc("simulator").get())).get();
            std::string const nameB =
                tc(second->getAttribute(tc("name").get())).get();
            std::string varB = tc(second->getAttribute(tc("causality").get())).get();
            VariableEndpoint const veB = {simulatorB, nameB, varB};

            VariableConnection const connection = {veA, veB};

            auto const isPowerbond =
                connectionElement->hasAttribute(tc("powerBond").get());
            if (isPowerbond) {
                std::string const powerbondName =
                    tc(connectionElement->getAttribute(tc("powerBond").get())).get();
                PowerBondConnection const pbc = {powerbondName, connection};
                powerBondConnections_.emplace_back(pbc);
            }

            variableConnections_.push_back(connection);
        }

        auto const* signalConnectionsElement =
            osp_elements(connectionsElement, "SignalConnection");
        for (size_t i = 0; i < signalConnectionsElement->getLength(); i++) {
            auto const* connectionElement = tagged_element(signalConnectionsElement, i);
            auto const* signalElement = tagged_element(osp_elements(connectionElement, "Signal"), 0);
            auto const* variableElement = tagged_element(osp_elements(connectionElement, "Variable"), 0);

            std::string const function =
                tc(signalElement->getAttribute(tc("function").get())).get();
            std::string const sigName =
                tc(signalElement->getAttribute(tc("name").get())).get();
            SignalEndpoint const signal = {function, sigName};

            std::string const simulator =
                tc(variableElement->getAttribute(tc("simulator").get())).get();
            std::string const varName =
                tc(variableElement->getAttribute(tc("name").get())).get();
            VariableEndpoint const variable = {simulator, varName};

            signalConnections_.push_back({signal, variable});
        }

        auto const* variableGroupConnectionsElement =
            osp_elements(connectionsElement, "VariableGroupConnection");
        for (size_t i = 0; i < variableGroupConnectionsElement->getLength(); i++) {
            auto const* connectionElement = tagged_element(variableGroupConnectionsElement, i);
            auto const* first = tagged_element(osp_elements(connectionElement, "VariableGroup"), 0);
            auto const* second = tagged_element(osp_elements(connectionElement, "VariableGroup"), 1);

            std::string const simulatorA =
                tc(first->getAttribute(tc("simulator").get())).get();
            std::string const nameA = tc(first->getAttribute(tc("name").get())).get();
            VariableEndpoint const veA = {simulatorA, nameA};

            std::string const simulatorB =
                tc(second->getAttribute(tc("simulator").get())).get();
            std::string const nameB =
                tc(second->getAttribute(tc("name").get())).get();
            VariableEndpoint const veB = {simulatorB, nameB};

            VariableConnection const connection = {veA, veB};

            auto const isPowerbond =
                connectionElement->hasAttribute(tc("powerBond").get());
            if (isPowerbond) {
                std::string const powerbondName =
                    tc(connectionElement->getAttribute(tc("powerBond").get())).get();
                PowerBondConnection const pbc = {powerbondName, connection};
                powerBondConnections_.emplace_back(pbc);
            }

            variableGroupConnections_.push_back({veA, veB});
        }

        auto const* signalGroupConnectionsElement =
            osp_elements(connectionsElement, "SignalGroupConnection");
        for (size_t i = 0; i < signalGroupConnectionsElement->getLength(); i++) {
            auto const* connectionElement = tagged_element(signalGroupConnectionsElement, i);
            auto const* signalElement = tagged_element(osp_elements(connectionElement, "SignalGroup"), 0);
            auto const* variableElement = tagged_element(osp_elements(connectionElement, "VariableGroup"), 0);

            std::string const function =
                tc(signalElement->getAttribute(tc("function").get())).get();
            std::string const sigName =
                tc(signalElement->getAttribute(tc("name").get())).get();
            SignalEndpoint const signal = {function, sigName};

            std::string const simulator =
                tc(variableElement->getAttribute(tc("simulator").get())).get();
            std::string const varName =
                tc(variableElement->getAttribute(tc("name").get())).get();
            VariableEndpoint const variable = {simulator, varName};

            signalGroupConnections_.push_back({signal, variable});
        }
    }
}

osp_config_parser::~osp_config_parser() noexcept = default;

const osp_config_parser::SimulationInformation&
osp_config_parser::get_simulation_information() const
{
    return simulationInformation_;
}

const std::vector<osp_config_parser::Simulator>&
osp_config_parser::get_elements() const
{
    return simulators_;
}

const std::unordered_map<std::string,
    osp_config_parser::LinearTransformationFunction>&
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

const std::vector<osp_config_parser::VariableConnection>&
osp_config_parser::get_variable_connections() const
{
    return variableConnections_;
}

const std::vector<osp_config_parser::SignalConnection>&
osp_config_parser::get_signal_connections() const
{
    return signalConnections_;
}

const std::vector<osp_config_parser::VariableConnection>&
osp_config_parser::get_variable_group_connections() const
{
    return variableGroupConnections_;
}

const std::vector<osp_config_parser::SignalConnection>&
osp_config_parser::get_signal_group_connections() const
{
    return signalGroupConnections_;
}

const std::vector<osp_config_parser::PowerBondConnection>&
osp_config_parser::get_power_bond_connections() const
{
    return powerBondConnections_;
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
    explicit extended_model_description(
        const cosim::filesystem::path& ospModelDescription)
    {
        xercesc::XMLPlatformUtils::Initialize();
        const auto xerces_cleanup =
            final_action([] { xercesc::XMLPlatformUtils::Terminate(); });

        auto* const domImpl =
            xercesc::DOMImplementationRegistry::getDOMImplementation(
                tc("LS").get());
        const dom_parser_ptr parser(
            static_cast<xercesc::DOMImplementationLS*>(domImpl)->createLSParser(
                xercesc::DOMImplementationLS::MODE_SYNCHRONOUS,
                tc("http://www.w3.org/2001/XMLSchema").get()));

        auto const* const doc =
            parser->parseURI(ospModelDescription.string().c_str());

        if (doc == nullptr) {
            std::ostringstream oss;
            oss << "Validation of " << ospModelDescription.string() << " failed.";
            BOOST_LOG_SEV(log::logger(), log::error) << oss.str();
            throw std::runtime_error(oss.str());
        }

        auto const* const rootElement = doc->getDocumentElement();

        auto const* const variableGroupsElement =
            tagged_element(rootElement->getElementsByTagName(tc("VariableGroups").get()), 0);

        for (auto* variableGroupElement =
                 variableGroupsElement->getFirstElementChild();
            variableGroupElement != nullptr;
            variableGroupElement = variableGroupElement->getNextElementSibling()) {
            variable_group_description vgd =
                create_variable_group_description(variableGroupElement);
            variableGroups[vgd.name] = std::move(vgd);
        }
    }

    static variable_group_description
    create_variable_group_description(xercesc::DOMElement* variableGroupElement)
    {
        variable_group_description variableGroupDescription;

        variableGroupDescription.name =
            tc(variableGroupElement->getAttribute(tc("name").get())).get();
        variableGroupDescription.type =
            tc(variableGroupElement->getTagName()).get();

        for (auto* variableGroupChildElement =
                 variableGroupElement->getFirstElementChild();
            variableGroupChildElement != nullptr;
            variableGroupChildElement =
                variableGroupChildElement->getNextElementSibling()) {
            const std::string tagName(
                tc(variableGroupChildElement->getTagName()).get());
            if (tagName == "Variable") {
                std::string variableName =
                    tc(variableGroupChildElement->getAttribute(tc("ref").get())).get();
                variableGroupDescription.variables.push_back(std::move(variableName));
            } else {
                variableGroupDescription.variable_group_descriptions.push_back(
                    create_variable_group_description(variableGroupChildElement));
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
    const std::string& functionName, const std::string& signalName)
{
    auto const found =
        boost::range::find_if(signalConnections, [&](const auto& conn) {
            return functionName == conn.signal.function &&
                signalName == conn.signal.name;
        });
    if (found == signalConnections.end()) {
        std::ostringstream oss;
        oss << "Missing expected connection to " << functionName << ":"
            << signalName;
        throw std::runtime_error(oss.str());
    }
    return *found;
}

} // namespace

constexpr uint64_t str_hash(std::string_view str)
{
    constexpr uint64_t hash_multiplier = 131;
    uint64_t hash = 0;
    for (const char character : str) {
        hash = (hash * hash_multiplier) + character;
    }
    return hash;
}

constexpr uint64_t operator""_hash(const char* str, size_t len)
{
    return str_hash(std::string_view(str, len));
}

void add_power_bonds(
    const std::vector<osp_config_parser::PowerBondConnection>& pbConnections,
    system_structure& systemStructure)
{
    std::vector<std::string> powerBondNames;
    std::set<std::string> uniquePowerBondNames;
    for (const auto& pbConnection : pbConnections) {
        powerBondNames.emplace_back(pbConnection.name);
        uniquePowerBondNames.insert(pbConnection.name);
    }

    for (const auto& pbName : uniquePowerBondNames) {
        std::vector<osp_config_parser::PowerBondConnection> aPowerBond;
        boost::algorithm::copy_if(
            pbConnections, std::back_inserter(aPowerBond),
            [pbName](const osp_config_parser::PowerBondConnection& connection) {
                return pbName == connection.name;
            });

        if (aPowerBond.size() != 2) {
            std::ostringstream oss;
            oss << "The powerbond " << pbName
                << " does not have 2 connections! Number of connections found was "
                << aPowerBond.size();
            throw std::runtime_error(oss.str());
        }

        const auto& connectionA = aPowerBond.at(0);
        const auto& connectionB = aPowerBond.at(1);

        assert(connectionA.name == connectionB.name);

        const auto variableA =
            cosim::full_variable_name{connectionA.connection.variableA.simulator,
                connectionA.connection.variableA.name};
        const auto variableB =
            cosim::full_variable_name{connectionA.connection.variableB.simulator,
                connectionA.connection.variableB.name};
        const auto variableC =
            cosim::full_variable_name{connectionB.connection.variableA.simulator,
                connectionB.connection.variableA.name};
        const auto variableD =
            cosim::full_variable_name{connectionB.connection.variableB.simulator,
                connectionB.connection.variableB.name};

        auto connAVariables = std::vector<osp_config_parser::VariableEndpoint>{
            connectionA.connection.variableA,
            connectionA.connection.variableB,
        };
        auto connBVariables = std::vector<osp_config_parser::VariableEndpoint>{
            connectionB.connection.variableA,
            connectionB.connection.variableB,
        };

        const auto varA = connectionA.connection.variableA.causality;
        const auto varB = connectionA.connection.variableB.causality;
        const auto varC = connectionB.connection.variableA.causality;
        const auto varD = connectionB.connection.variableB.causality;

        if ((varA.has_value() && !varB.has_value()) ||
            (!varA.has_value() && varB.has_value())) {
            std::ostringstream oss;
            oss << "Missing causality for powerbond connection "
                << connectionA.connection.variableA.name << " <-> "
                << connectionA.connection.variableB.name
                << ". Both variables in a powerbond must have an input and output "
                   "causality specified as attribute.";
            throw std::runtime_error(oss.str());
        }

        if ((varC.has_value() && !varD.has_value()) ||
            (!varC.has_value() && varD.has_value())) {
            std::ostringstream oss;
            oss << "Missing causality for powerbond connection "
                << connectionB.connection.variableA.name << " <-> "
                << connectionB.connection.variableB.name
                << ". Both variables in a powerbond must have an input and output "
                   "causality specified as attribute.";
            throw std::runtime_error(oss.str());
        }

        auto powerbond = cosim::system_structure::power_bond{};

        for (auto& var : connAVariables) {
            const auto variable = cosim::full_variable_name{var.simulator, var.name};
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access): Preserve value()'s exception when both causalities are absent.
            const auto& causality = var.causality.value();
            switch (str_hash(causality)) {
                case "input"_hash:
                    powerbond.input_a = variable;
                    break;
                case "output"_hash:
                    powerbond.output_b = variable;
                    break;
                default:
                    std::ostringstream oss;
                    oss << "Invalid causality value for variable " << var.name << ": "
                        << causality << ". Accepted values are input, output.";
                    throw std::runtime_error(oss.str());
                    break;
            }
        }

        for (auto& var : connBVariables) {
            const auto variable = cosim::full_variable_name{var.simulator, var.name};
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access): Preserve value()'s exception when both causalities are absent.
            const auto& causality = var.causality.value();
            switch (str_hash(causality)) {
                case "input"_hash:
                    powerbond.input_b = variable;
                    break;
                case "output"_hash:
                    powerbond.output_a = variable;
                    break;
                default:
                    std::ostringstream oss;
                    oss << "Invalid causality value for variable " << var.name << ": "
                        << causality << ". Accepted values are input, output.";
                    throw std::runtime_error(oss.str());
                    break;
            }
        }

        systemStructure.add_power_bond(pbName, powerbond);
    }

    // Check that the number of unique power bond names is equal to the number of
    // power bonds. This must be verified after all bonds have been added;
    // checking inside the loop would compare a partial count against the full
    // unique-name count and spuriously throw when more than one bond is
    // configured.
    boost::range::sort(powerBondNames);
    auto const uniqueCount = static_cast<int>(
        boost::range::unique(powerBondNames).end() - powerBondNames.begin());
    auto const numPowerBonds =
        static_cast<int>(systemStructure.get_power_bonds().size());

    if (uniqueCount != numPowerBonds) {
        std::ostringstream oss;
        oss << "The number of powerbonds (" << numPowerBonds
            << ") is not equal to the number of unique power bond names ("
            << uniqueCount
            << ") found in the configured system. Power bond names must be unique "
               "pr. bond, that is found on only and exactly the two "
               "VariableConnections that form the bond.";
        throw std::runtime_error(oss.str());
    }
}

void connect_variables(const std::vector<osp_config_parser::VariableConnection>& variableConnections,
    system_structure& systemStructure)
{
    for (const auto& connection : variableConnections) {
        const auto variableA = full_variable_name(connection.variableA.simulator,
            connection.variableA.name);
        const auto variableB = full_variable_name(connection.variableB.simulator,
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

extended_model_description
get_emd(const std::unordered_map<std::string, extended_model_description>& emds,
    const std::string& element)
{
    auto const emdIt = emds.find(element);
    if (emdIt == emds.end()) {
        std::ostringstream oss;
        oss << "Cannot find extended model description for " << element;
        throw std::runtime_error(oss.str());
    }
    return emdIt->second;
}

std::vector<std::string> get_variable_group_variables(
    const std::unordered_map<std::string, variable_group_description>& descriptions,
    const osp_config_parser::VariableEndpoint& endpoint)
{
    std::vector<std::string> groupVariables;
    auto const groupIt = descriptions.find(endpoint.name);
    if (groupIt == descriptions.end()) {
        for (const auto& description : descriptions) {
            if (!description.second.variable_group_descriptions.empty()) {
                std::unordered_map<std::string, variable_group_description>
                    nestedDescriptions;
                for (const auto& variableGroupDescr :
                    description.second.variable_group_descriptions) {
                    nestedDescriptions.insert(
                        {variableGroupDescr.name, variableGroupDescr});
                }
                groupVariables =
                    get_variable_group_variables(nestedDescriptions, endpoint);
                if (!groupVariables.empty()) {
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
    auto const groupIt = descriptions.find(connector);
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
        const auto& variablesA =
            get_variable_group_variables(emdA.variableGroups, connection.variableA);
        const auto& variablesB =
            get_variable_group_variables(emdB.variableGroups, connection.variableB);
        const auto& variableGroupsA =
            get_variable_groups(emdA.variableGroups, connection.variableA.name);
        const auto& variableGroupsB =
            get_variable_groups(emdB.variableGroups, connection.variableB.name);

        if (variableGroupsA.size() != variableGroupsB.size()) {
            std::ostringstream oss;
            oss << "Cannot create connection between variable groups. Variable group "
                << connection.variableA.simulator << ":" << connection.variableA.name
                << " has different size [" << variableGroupsA.size()
                << "] than variable group " << connection.variableB.simulator << ":"
                << connection.variableB.name << " size [" << variableGroupsB.size()
                << "]";
            throw std::runtime_error(oss.str());
        }

        std::vector<osp_config_parser::VariableConnection>
            nestedVariableGroupConnections;
        nestedVariableGroupConnections.reserve(variableGroupsA.size());
        // clang-format off
        for (std::size_t i = 0; i < variableGroupsA.size(); ++i) {
            nestedVariableGroupConnections.push_back({
                {connection.variableA.simulator, variableGroupsA.at(i).name},
                {connection.variableB.simulator, variableGroupsB.at(i).name},});
        }
        // clang-format on
        connect_variable_groups(nestedVariableGroupConnections, systemStructure,
            emds);

        if (variablesA.size() != variablesB.size()) {
            std::ostringstream oss;
            oss << "Cannot create connection between variable groups. Variable group "
                << connection.variableA.simulator << ":" << connection.variableA.name
                << " has different size [" << variablesA.size()
                << "] than variable group " << connection.variableB.simulator << ":"
                << connection.variableB.name << " size [" << variablesB.size() << "]";
            throw std::runtime_error(oss.str());
        }

        std::vector<osp_config_parser::VariableConnection> variableConnections;
        variableConnections.reserve(variablesA.size());

        // clang-format off
        for (std::size_t i = 0; i < variablesA.size(); ++i) {
            variableConnections.push_back({
                {connection.variableA.simulator, variablesA.at(i)},
                {connection.variableB.simulator, variablesB.at(i)},});
        }
        // clang-format on

        connect_variables(variableConnections, systemStructure);
    }
}

void connect_linear_transformation_functions(
    const std::unordered_map<std::string,
        osp_config_parser::LinearTransformationFunction>& functions,
    const std::vector<osp_config_parser::SignalConnection>& signalConnections,
    system_structure& systemStructure)
{
    for (const auto& [functionName, function] : functions) {
        function_parameter_value_map functionParams;
        functionParams
            [linear_transformation_function_type::offset_parameter_index] =
                function.offset;
        functionParams
            [linear_transformation_function_type::factor_parameter_index] =
                function.factor;
        systemStructure.add_entity(
            functionName, std::make_shared<linear_transformation_function_type>(),
            functionParams);

        const auto inConn = find_signal_connection(signalConnections, functionName,
            function.in.name);
        const auto outConn = find_signal_connection(signalConnections, functionName,
            function.out.name);
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
        functionParams[vector_sum_function_type::inputCount_parameter_index] =
            function.inputCount;
        systemStructure.add_entity(functionName,
            std::make_shared<vector_sum_function_type>(),
            functionParams);

        for (int i = 0; i < function.inputCount; i++) {
            const auto inConn = find_signal_connection(
                signalConnections, functionName, function.ins.at(i).name);
            systemStructure.connect_variables(
                full_variable_name(inConn.variable.simulator, inConn.variable.name),
                full_variable_name(functionName, "in", i, "", 0));
        }
        const auto outConn = find_signal_connection(signalConnections, functionName,
            function.out.name);
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
        functionParams[vector_sum_function_type::inputCount_parameter_index] =
            function.inputCount;
        functionParams[vector_sum_function_type::dimension_parameter_index] =
            function.dimension;
        systemStructure.add_entity(functionName,
            std::make_shared<vector_sum_function_type>(),
            functionParams);

        for (int i = 0; i < function.inputCount; ++i) {
            const auto inGroupConn = find_signal_connection(
                signalGroupConnections, functionName, function.inGroups.at(i).name);
            const auto& inEmd = emds.at(inGroupConn.variable.simulator);
            const auto& inVariableGroup =
                inEmd.variableGroups.at(inGroupConn.variable.name);

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
        const auto& outVariableGroup =
            outEmd.variableGroups.at(outGroupConn.variable.name);
        for (int j = 0; j < function.dimension; ++j) {
            const auto& outVariableName = outVariableGroup.variables.at(j);
            systemStructure.connect_variables(
                full_variable_name(functionName, "out", 0, "", j),
                full_variable_name(outGroupConn.variable.simulator, outVariableName));
        }
    }
}

cosim::filesystem::path file_query_uri_to_path(const cosim::filesystem::path& baseParentPath,
    const cosim::uri& queryUri)
{
    const auto baseParentUri = cosim::path_to_file_uri(baseParentPath);
    constexpr std::string_view file_parameter_prefix = "file=";
    const auto query = queryUri.query();
    if (query && query->find(file_parameter_prefix) < query->size()) {
        if (query->find("file=file:///") < query->size()) {
            return file_uri_to_path(
                std::string(query->substr(file_parameter_prefix.size())));
        }
        return file_uri_to_path(
            std::string(baseParentUri.view()) + "/" +
            std::string(query->substr(file_parameter_prefix.size())));
    }
    return file_uri_to_path(baseParentUri);
}

void configure_osp_algorithm(const osp_config_parser::SimulationInformation& simInfo, osp_config& config)
{
    auto algorithm = simInfo.algorithm;
    boost::range::transform(
        algorithm, algorithm.begin(),
        [](unsigned char character) { return std::tolower(character); });

    if (algorithm == "ecco") {
        if (simInfo.eccoConfiguration.has_value()) {
            BOOST_LOG_SEV(log::logger(), log::info) << "Configuring algorithm: ecco";
            config.algorithm_configuration = simInfo.eccoConfiguration.value();
        } else {
            throw std::runtime_error(
                "No configuration parameters found for ecco algorithm.");
        }
    } else if (algorithm == "fixedstep") {
        BOOST_LOG_SEV(log::logger(), log::info)
            << "Configuring algorithm: fixedStep";
        config.algorithm_configuration =
            cosim::fixed_step_algorithm_params{to_duration(simInfo.stepSize)};
    } else {
        throw std::runtime_error(
            "Invalid algorithm choice. Allowed values are fixedStep, ecco.");
    }
}

void add_osp_simulators(const std::vector<osp_config_parser::Simulator>& simulators,
    model_uri_resolver& resolver, const cosim::uri& baseURI,
    const cosim::filesystem::path& configFile, osp_config& config,
    std::unordered_map<std::string, extended_model_description>& emds)
{
    for (const auto& simulator : simulators) {
        const auto model = resolver.lookup_model(baseURI, simulator.source);
        config.system_structure.add_entity(simulator.name, model,
            simulator.stepSize
                ? to_duration(*simulator.stepSize)
                : duration::zero());

        for (const auto& initial : simulator.initialValues) {
            add_variable_value(config.initial_values, config.system_structure,
                full_variable_name(simulator.name, initial.name),
                initial.value);
        }

        const auto description = model->description();
        if (!description) {
            throw error(make_error_code(errc::invalid_system_structure),
                "Model description is unavailable for simulator " +
                    simulator.name);
        }
        const std::string msmiFileName =
            description->name + "_OspModelDescription.xml";
        const auto modelUri = resolve_reference(baseURI, simulator.source);
        cosim::filesystem::path msmiFilePath;

        msmiFilePath =
            (modelUri.scheme() == "file")
            ? file_uri_to_path(modelUri).remove_filename() / msmiFileName
            : file_query_uri_to_path(
                  cosim::file_uri_to_path(baseURI).parent_path(),
                  modelUri)
                    .remove_filename() /
                msmiFileName;
        if (cosim::filesystem::exists(msmiFilePath)) {
            emds.emplace(simulator.name, msmiFilePath);
        } else {
            msmiFilePath = configFile.parent_path() / msmiFileName;
            if (cosim::filesystem::exists(msmiFilePath)) {
                emds.emplace(simulator.name, msmiFilePath);
            }
        }
    }
}

} // namespace

osp_config load_osp_config(const cosim::filesystem::path& configPath,
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
        oss << "Configured base step size [" << simInfo.stepSize
            << "] must be nonzero and positive";
        BOOST_LOG_SEV(log::logger(), log::error) << oss.str();
        throw std::runtime_error(oss.str());
    }

    if (simInfo.endTime.has_value() &&
        simInfo.startTime > simInfo.endTime.value()) {
        std::ostringstream oss;
        oss << "Configured start time [" << simInfo.startTime
            << "] is larger than configured end time [" << simInfo.endTime.value()
            << "]";
        BOOST_LOG_SEV(log::logger(), log::error) << oss.str();
        throw std::runtime_error(oss.str());
    }

    osp_config config;
    config.start_time = to_time_point(simInfo.startTime);
    if (simInfo.endTime.has_value()) {
        config.end_time = to_time_point(simInfo.endTime.value());
    }

    configure_osp_algorithm(simInfo, config);
    const auto& simulators = parser.get_elements();
    std::unordered_map<std::string, extended_model_description> emds;
    add_osp_simulators(simulators, resolver, baseURI, configFile, config, emds);

    for (const auto& [name, schedule] : parser.get_clocks()) {
        config.system_structure.add_entity(name,
            detail::make_osp_clock_model(schedule));
    }
    connect_variables(parser.get_variable_connections(), config.system_structure);
    connect_variable_groups(parser.get_variable_group_connections(),
        config.system_structure, emds);
    connect_linear_transformation_functions(
        parser.get_linear_transformation_functions(),
        parser.get_signal_connections(), config.system_structure);
    connect_sum_functions(parser.get_sum_functions(),
        parser.get_signal_connections(),
        config.system_structure);
    connect_vector_sum_functions(parser.get_vector_sum_functions(),
        parser.get_signal_group_connections(),
        config.system_structure, emds);
    add_power_bonds(parser.get_power_bond_connections(), config.system_structure);

    return config;
}

} // namespace cosim
