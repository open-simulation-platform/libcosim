/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/ssp/ssp_parser.hpp"

#include "cosim/algorithm/fixed_step_algorithm.hpp"
#include "cosim/log/logger.hpp"
#include "cosim/model_description.hpp"
#include "cosim/time.hpp"
#include "cosim/xml_value.hpp"

#include <boost/log/sources/severity_feature.hpp>
#include <boost/property_tree/exceptions.hpp>
#include <boost/property_tree/ptree_fwd.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/range/algorithm/find_if.hpp>

#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
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

using boost::property_tree::ptree;

constexpr std::string_view xmlns_prefix = "xmlns:";

std::string
canonical_name(const std::string& key,
    const std::unordered_map<std::string, std::string>& scope)
{
    const auto colon = key.find(':');
    const auto prefix =
        colon == std::string::npos ? std::string{} : key.substr(0, colon);
    const auto uri = scope.find(prefix);
    if (uri == scope.end()) {
        if (!prefix.empty()) {
            throw std::invalid_argument("Unbound SSP XML namespace prefix: " +
                prefix);
        }
        return key;
    }

    const auto local = key.substr(colon == std::string::npos ? 0 : colon + 1);
    constexpr std::array<std::pair<std::string_view, std::string_view>, 4> known{
        {
            {"http://ssp-standard.org/SSP1/SystemStructureDescription", "ssd"},
            {"http://ssp-standard.org/SSP1/SystemStructureCommon", "ssc"},
            {
                "http://ssp-standard.org/SSP1/SystemStructureParameterValues",
                "ssv",
            },
            {"http://opensimulationplatform.com/SSP/OSPAnnotations", "osp"},
        },
    };
    auto name = "{" + uri->second + "}" + local;
    for (const auto& [namespaceUri, canonical] : known) {
        if (uri->second == namespaceUri) {
            name = std::string(canonical) + ":" + local;
            break;
        }
    }
    return name;
}

// Keep the legacy prefix-based reader for SSP 1.0. For 2.0, canonicalize
// qualified names using in-scope namespace declarations, not prefix spelling.
void canonicalize_names(
    ptree& tree,
    const std::unordered_map<std::string, std::string>& namespaces = {})
{
    ptree renamed;
    renamed.data() = std::move(tree.data());
    const auto append = [&](const std::string& name, ptree& node) {
        renamed.push_back({name, {}});
        renamed.back().second.swap(node);
    };
    for (auto& [key, node] : tree) {
        if (key == "<xmlattr>") {
            append(key, node);
            continue;
        }
        auto scope = namespaces;
        if (const auto attributes = node.get_child_optional("<xmlattr>")) {
            for (const auto& [attribute, value] : *attributes) {
                if (attribute == "xmlns") {
                    scope[""] = value.data();
                }
                if (attribute.rfind(xmlns_prefix, 0) == 0) {
                    scope[attribute.substr(xmlns_prefix.size())] = value.data();
                }
            }
        }
        const auto name = canonical_name(key, scope);
        canonicalize_names(node, scope);
        append(name, node);
    }
    tree.swap(renamed);
}

template<typename T>
T get_attribute(const boost::property_tree::ptree& tree, const std::string& key,
    const std::optional<T>& defaultValue = {})
{
    return !defaultValue ? tree.get<T>("<xmlattr>." + key)
                         : tree.get<T>("<xmlattr>." + key, *defaultValue);
}

template<typename T>
std::optional<T> get_optional_attribute(const boost::property_tree::ptree& tree,
    const std::string& key)
{
    const auto result = tree.get_optional<T>("<xmlattr>." + key);
    return result ? *result : std::optional<T>();
}

variable_type parse_connector_type(const boost::property_tree::ptree& tree)
{
    if (tree.get_child_optional("ssc:Real")) {
        return variable_type::real;
    } else if (tree.get_child_optional("ssc:Integer")) {
        return variable_type::integer;
    } else if (tree.get_child_optional("ssc:Boolean")) {
        return variable_type::boolean;
    } else if (tree.get_child_optional("ssc:String")) {
        return variable_type::string;
    } else if (tree.get_child_optional("ssc:Enumeration")) {
        throw std::invalid_argument("Unsupported SSP connector type: Enumeration");
    } else if (tree.get_child_optional("ssc:Binary")) {
        throw std::invalid_argument("Unsupported SSP connector type: Binary");
    } else {
        throw std::invalid_argument("A valid connector type was not found!");
    }
}

ssp_parser::ParameterSet
parse_parameter_set(const boost::property_tree::ptree& parameterSetNode)
{
    ssp_parser::ParameterSet parameterSet{};
    parameterSet.name = get_attribute<std::string>(parameterSetNode, "name");
    for (const auto& parameter : parameterSetNode.get_child("ssv:Parameters")) {
        const auto name = get_attribute<std::string>(parameter.second, "name");
        if (const auto realParameter =
                parameter.second.get_child_optional("ssv:Real")) {
            const auto value = get_attribute<double>(*realParameter, "value");
            parameterSet.parameters.push_back(
                {name, variable_value(scalar_value{value})});
        } else if (const auto intParameter =
                       parameter.second.get_child_optional("ssv:Integer")) {
            const auto value = get_attribute<int>(*intParameter, "value");
            parameterSet.parameters.push_back(
                {name, variable_value(scalar_value{value})});
        } else if (const auto boolParameter =
                       parameter.second.get_child_optional("ssv:Boolean")) {
            const auto value = get_attribute<bool>(*boolParameter, "value");
            parameterSet.parameters.push_back(
                {name, variable_value(scalar_value{value})});
        } else if (const auto stringParameter =
                       parameter.second.get_child_optional("ssv:String")) {
            const auto value = get_attribute<std::string>(*stringParameter, "value");
            parameterSet.parameters.push_back(
                {name, variable_value(scalar_value{value})});
        } else {
            throw std::invalid_argument(
                "Unsupported or missing SSP parameter type for '" + name + "'");
        }
    }
    return parameterSet;
}

std::vector<std::uint64_t> parse_dimensions(const ptree& node)
{
    std::vector<std::uint64_t> shape;
    for (const auto& [tag, dimension] : node) {
        if (tag != "ssc:Dimension") {
            continue;
        }
        if (get_optional_attribute<std::string>(dimension, "sizeConnector")) {
            throw std::invalid_argument(
                "SSP sizeConnector dimensions are not supported");
        }
        const auto size = get_optional_attribute<std::string>(dimension, "size");
        if (!size) {
            throw std::invalid_argument("SSP dimensions require a fixed size");
        }
        shape.push_back(detail::parse_xml_integer<std::uint64_t>(
            detail::trim_xml_whitespace(*size)));
    }
    return shape;
}

std::pair<std::optional<variable_type>, const ptree*>
find_exact_type(const ptree& node, std::string_view prefix)
{
    std::pair<std::optional<variable_type>, const ptree*> result{};
    for (const auto& [tag, element] : node) {
        if (tag.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }
        const auto type =
            detail::xml_variable_type(std::string_view(tag).substr(prefix.size()));
        if (!type) {
            continue;
        }
        if (result.first) {
            throw std::invalid_argument("Multiple SSP type declarations");
        }
        result = {type, &element};
    }
    return result;
}

void add_exact_parameter(
    ssp_parser::ParameterSet& result,
    const ptree& parameter,
    const std::string& name,
    std::vector<std::uint64_t>& shape)
{
    const auto [type, element] = find_exact_type(parameter, "ssv:");
    if (!type || *type == variable_type::clock) {
        throw std::invalid_argument(
            "Unsupported or missing SSP parameter type");
    }
    auto attribute = get_optional_attribute<std::string>(*element, "value");
    std::vector<std::string> values;
    for (const auto& [child, item] : *element) {
        if (child == "<xmlattr>") {
            continue;
        }
        if (child != "ssv:Value" || !detail::xml_value_has_elements(*type)) {
            throw std::invalid_argument("Unexpected child of SSP parameter type");
        }
        values.push_back(get_attribute<std::string>(item, "value"));
    }
    if (*type == variable_type::enumeration) {
        result.parameters.push_back(
            {
                name,
                ssp_parser::EnumerationParameter{
                    std::move(attribute),
                    std::move(values),
                    std::move(shape),
                },
            });
    } else {
        std::vector<std::string_view> items(values.begin(), values.end());
        result.parameters.push_back(
            {
                name,
                detail::parse_xml_value(*type, attribute, std::move(items),
                    std::move(shape)),
            });
    }
}

ssp_parser::ParameterSet
parse_exact_parameter_set(const ptree& parameterSetNode)
{
    const auto version = get_attribute<std::string>(parameterSetNode, "version");
    if (version == "1.0") {
        return parse_parameter_set(parameterSetNode);
    }
    if (version != "2.0") {
        throw std::invalid_argument("Unsupported SSV version: " + version);
    }
    ssp_parser::ParameterSet result;
    result.name = get_attribute<std::string>(parameterSetNode, "name");
    const auto parameters = parameterSetNode.get_child_optional("ssv:Parameters");
    if (!parameters) {
        return result;
    }
    for (const auto& [tag, parameter] : *parameters) {
        if (tag != "ssv:Parameter") {
            continue;
        }
        const auto name = get_attribute<std::string>(parameter, "name");
        // Variable-sized declarations are unsupported, not bad assignments.
        auto shape = parse_dimensions(parameter);
        const auto log_invalid = [&](const std::exception& exception) {
            BOOST_LOG_SEV(log::logger(), log::warning)
                << "SSP parameter set " << result.name << ", parameter " << name
                << ": " << exception.what();
        };
        try {
            add_exact_parameter(result, parameter, name, shape);
        } catch (const std::logic_error& e) {
            log_invalid(e);
        } catch (const std::overflow_error& e) {
            log_invalid(e);
        } catch (const boost::property_tree::ptree_error& e) {
            log_invalid(e);
        }
    }
    return result;
}

void reject_mapping_transformations(const ptree& connection)
{
    for (const auto* name : {
             "BooleanMappingTransformation",
             "IntegerMappingTransformation",
             "EnumerationMappingTransformation",
         }) {
        if (connection.get_child_optional(std::string("ssc:") + name)) {
            throw std::invalid_argument(
                std::string("Unsupported SSP transformation: ") + name);
        }
    }
}

ssp_parser::Endpoint parse_endpoint(
    const boost::property_tree::ptree& connection, const char* elementAttribute, // NOLINT(bugprone-easily-swappable-parameters): The names identify distinct SSP endpoint attributes.
    const char* connectorAttribute,
    const std::unordered_map<std::string, ssp_parser::Component>& elements)
{
    auto componentName = get_attribute<std::string>(connection, elementAttribute);
    const auto component = elements.find(componentName);
    if (component == elements.end()) {
        throw std::runtime_error("No component named: '" + componentName + "'!");
    }
    auto connectorName =
        get_attribute<std::string>(connection, connectorAttribute);
    if (component->second.connectors.find(connectorName) ==
        component->second.connectors.end()) {
        throw std::runtime_error("No connector named: '" + connectorName +
            "' in Component '" + componentName + "'!");
    }
    return {std::move(componentName), std::move(connectorName)};
}

void parse_default_experiment(
    const ptree& ssd, bool exactTypes, ssp_parser::DefaultExperiment& result)
{
    const auto defaultExperiment = ssd.get_child_optional("ssd:DefaultExperiment");
    if (!defaultExperiment) {
        return;
    }
    result.startTime =
        get_attribute<double>(*defaultExperiment, "startTime", 0.0);
    result.stopTime =
        get_optional_attribute<double>(*defaultExperiment, "stopTime");

    const auto annotations = defaultExperiment->get_child_optional("ssd:Annotations");
    if (!annotations) {
        return;
    }
    for (const auto& annotation : *annotations) {
        if (exactTypes && annotation.first != "ssc:Annotation") {
            continue;
        }
        const auto& annotationType =
            get_attribute<std::string>(annotation.second, "type");
        if (annotationType != "com.opensimulationplatform") {
            continue;
        }
        for (const auto& algorithm :
            annotation.second.get_child("osp:Algorithm")) {
            if (algorithm.first == "osp:FixedStepAlgorithm") {
                auto const baseStepSize =
                    get_attribute<double>(algorithm.second, "baseStepSize");
                const auto numWorkerThreads =
                    get_optional_attribute<unsigned int>(algorithm.second,
                        "numWorkerThreads");
                result.algorithm =
                    std::make_unique<cosim::fixed_step_algorithm>(
                        cosim::to_duration(baseStepSize), numWorkerThreads);
            } else {
                throw std::runtime_error("Unknown algorithm: " + algorithm.first);
            }
        }
    }
}

void parse_connectors(const ptree& component, bool exactTypes, ssp_parser::Component& comp)
{
    const auto connectors = component.get_child_optional("ssd:Connectors");
    if (!connectors) {
        return;
    }
    for (const auto& connector : *connectors) {
        if (connector.first != "ssd:Connector") {
            continue;
        }
        ssp_parser::Connector parsedConnector;
        parsedConnector.name =
            get_attribute<std::string>(connector.second, "name");
        parsedConnector.kind =
            get_attribute<std::string>(connector.second, "kind");
        if (exactTypes) {
            try {
                const auto [type, element] =
                    find_exact_type(connector.second, "ssc:");
                parsedConnector.type = type;
                parsedConnector.shape = parse_dimensions(connector.second);
                if (type == variable_type::clock) {
                    if (const auto attrs =
                            element->get_child_optional("<xmlattr>")) {
                        for (const auto& [key, node] : *attrs) {
                            parsedConnector.clockAttributes.emplace(key, node.data());
                        }
                    }
                }
            } catch (const std::exception& e) {
                throw std::invalid_argument(
                    "Invalid SSP connector '" + comp.name + "." +
                    parsedConnector.name + "': " + e.what());
            }
        } else {
            parsedConnector.type = parse_connector_type(connector.second);
        }
        auto name = parsedConnector.name;
        comp.connectors.insert_or_assign(std::move(name),
            std::move(parsedConnector));
    }
}

void parse_parameter_bindings(
    const ptree& component, const cosim::filesystem::path& ssdPath,
    bool exactTypes, ssp_parser::Component& comp)
{
    const auto parameterBindings = component.get_child_optional("ssd:ParameterBindings");
    if (!parameterBindings) {
        return;
    }
    for (const auto& parameterBinding : *parameterBindings) {
        if (exactTypes && parameterBinding.first != "ssd:ParameterBinding") {
            continue;
        }
        if (exactTypes && parameterBinding.second.get_child_optional("ssd:ParameterMapping")) {
            throw std::invalid_argument(
                "SSP parameter mappings are not supported");
        }
        if (const auto& source = get_optional_attribute<std::string>(
                parameterBinding.second, "source")) {
            boost::property_tree::ptree binding;
            const cosim::filesystem::path ssvPath =
                ssdPath.parent_path() / *source;
            if (!cosim::filesystem::exists(ssvPath)) {
                std::ostringstream oss;
                oss << "File '" << ssvPath << "' does not exists!";
                throw std::runtime_error(oss.str());
            }
            boost::property_tree::read_xml(
                ssvPath.string(), binding,
                boost::property_tree::xml_parser::no_comments | // NOLINT(misc-include-cleaner, bugprone-signed-bitwise): Boost XML parser flags are combined with bitwise OR.
                    boost::property_tree::xml_parser::
                        trim_whitespace); // NOLINT(misc-include-cleaner)
            if (exactTypes) {
                canonicalize_names(binding);
            }
            const auto& set = binding.get_child("ssv:ParameterSet");
            comp.parameterSets.push_back(exactTypes
                    ? parse_exact_parameter_set(set)
                    : parse_parameter_set(set));
        } else if (const auto parameterValues =
                       parameterBinding.second.get_child_optional(
                           "ssd:ParameterValues")) {
            const auto& set = parameterValues->get_child("ssv:ParameterSet");
            comp.parameterSets.push_back(exactTypes
                    ? parse_exact_parameter_set(set)
                    : parse_parameter_set(set));
        }
    }
}

void parse_component_annotations(
    const ptree& component, bool exactTypes, ssp_parser::Component& comp)
{
    if (const auto annotations =
            component.get_child_optional("ssd:Annotations")) {
        for (const auto& annotation : *annotations) {
            if (exactTypes && annotation.first != "ssc:Annotation") {
                continue;
            }
            const auto& annotationType =
                get_attribute<std::string>(annotation.second, "type");
            if (annotationType == "com.opensimulationplatform") {
                if (const auto& stepSizeHint =
                        annotation.second.get_child_optional("osp:StepSizeHint")) {
                    comp.stepSizeHint = get_attribute<double>(*stepSizeHint, "value");
                }
            }
        }
    }
}

void parse_components(const ptree& system, const cosim::filesystem::path& ssdPath,
    bool exactTypes, std::unordered_map<std::string, ssp_parser::Component>& elements)
{
    for (const auto& component : system.get_child("ssd:Elements")) {
        if (exactTypes && component.first == "<xmlattr>") {
            continue;
        }
        if (exactTypes && component.first != "ssd:Component") {
            throw std::invalid_argument("Unsupported SSP element: " +
                component.first);
        }

        ssp_parser::Component comp;
        comp.name = get_attribute<std::string>(component.second, "name");
        comp.source = get_attribute<std::string>(component.second, "source");
        parse_connectors(component.second, exactTypes, comp);
        parse_parameter_bindings(component.second, ssdPath, exactTypes, comp);
        parse_component_annotations(component.second, exactTypes, comp);
        auto name = comp.name;
        elements.insert_or_assign(std::move(name), std::move(comp));
    }
}

void parse_connections(const ptree& system, bool exactTypes,
    const std::unordered_map<std::string, ssp_parser::Component>& elements,
    std::vector<ssp_parser::Connection>& result)
{
    if (const auto connections = system.get_child_optional("ssd:Connections")) {
        for (const auto& connection : *connections) {
            if (exactTypes && connection.first != "ssd:Connection") {
                continue;
            }
            if (exactTypes) {
                reject_mapping_transformations(connection.second);
            }
            ssp_parser::Connection parsedConnection{
                parse_endpoint(connection.second, "startElement", "startConnector",
                    elements),
                parse_endpoint(connection.second, "endElement", "endConnector",
                    elements),
                std::nullopt,
            };

            if (const auto transformation = connection.second.get_child_optional(
                    "ssc:LinearTransformation")) {
                auto const offset = get_attribute<double>(*transformation, "offset", 0);
                auto const factor =
                    get_attribute<double>(*transformation, "factor", 1.0);
                parsedConnection.linearTransformation = {offset, factor};
            }
            result.push_back(std::move(parsedConnection));
        }
    }
}

} // namespace

ssp_parser::ssp_parser(const cosim::filesystem::path& ssdPath)
{
    boost::property_tree::ptree root;
    boost::property_tree::read_xml(
        ssdPath.string(), root,
        boost::property_tree::xml_parser::no_comments | // NOLINT(misc-include-cleaner, bugprone-signed-bitwise): Boost XML parser flags are combined with bitwise OR.
            boost::property_tree::xml_parser::
                trim_whitespace); // NOLINT(misc-include-cleaner)

    const auto first = boost::range::find_if(
        root, [](const auto& entry) { return entry.first != "<xmlattr>"; });
    if (first == root.end()) {
        throw std::invalid_argument("Missing SSP system description");
    }
    const auto version = get_attribute<std::string>(first->second, "version");
    if (version == "2.0") {
        canonicalize_names(root);
    } else if (version != "1.0") {
        throw std::invalid_argument("Unsupported SSD version: " + version);
    }
    const auto& ssd = root.get_child("ssd:SystemStructureDescription");
    systemDescription_.name = get_attribute<std::string>(ssd, "name");
    systemDescription_.version = version;
    const bool exactTypes = has_exact_types();
    parse_default_experiment(ssd, exactTypes, defaultExperiment_);

    const auto& system = ssd.get_child("ssd:System");
    systemDescription_.system.name = get_attribute<std::string>(system, "name");
    systemDescription_.system.description =
        get_optional_attribute<std::string>(system, "description");
    parse_components(system, ssdPath, exactTypes, elements_);
    parse_connections(system, exactTypes, elements_, connections_);
}

ssp_parser::~ssp_parser() noexcept = default;

const std::unordered_map<std::string, ssp_parser::Component>&
ssp_parser::get_elements() const
{
    return elements_;
}

const std::vector<ssp_parser::Connection>& ssp_parser::get_connections() const
{
    return connections_;
}

const ssp_parser::DefaultExperiment&
ssp_parser::get_default_experiment() const
{
    return defaultExperiment_;
}

cosim::time_point get_default_start_time(const ssp_parser& parser)
{
    return cosim::to_time_point(parser.get_default_experiment().startTime);
}

} // namespace cosim
