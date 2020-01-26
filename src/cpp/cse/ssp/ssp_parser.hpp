#ifndef CSECORE_SSP_PARSER_HPP
#define CSECORE_SSP_PARSER_HPP

#include "cse/algorithm.hpp"
#include "cse/error.hpp"

#include <boost/filesystem/path.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <optional>
#include <string>

namespace cse
{

namespace
{

template<typename T>
T get_attribute(const boost::property_tree::ptree& tree, const std::string& key, const std::optional<T> defaultValue = {})
{
    return !defaultValue ? tree.get<T>("<xmlattr>." + key) : tree.get<T>("<xmlattr>." + key, *defaultValue);
}

template<typename T>
std::optional<T> get_optional_attribute(const boost::property_tree::ptree& tree, const std::string& key)
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
        CSE_PANIC_M("Don't know how to handle Enumeration type!");
    } else if (tree.get_child_optional("ssc:Binary")) {
        CSE_PANIC_M("Don't know how to handle Binary type!");
    } else {
        throw std::invalid_argument("A valid connector type was not found!");
    }
}

} // namespace

class ssp_parser
{

public:
    explicit ssp_parser(const boost::filesystem::path& xmlPath);
    ~ssp_parser() noexcept;

    struct DefaultExperiment
    {
        double startTime = 0.0;
        std::optional<double> stopTime;
        std::shared_ptr<cse::algorithm> algorithm;
    };

    const DefaultExperiment& get_default_experiment() const;

    struct System
    {
        std::string name;
        std::optional<std::string> description;
    };

    struct SystemDescription
    {
        std::string name;
        std::string version;
        System system;
    };

    struct Connector
    {
        std::string name;
        std::string kind;
        cse::variable_type type;
    };

    struct Parameter
    {
        std::string name;
        cse::variable_type type;
        scalar_value value;
    };

    struct ParameterSet
    {
        std::string name;
        std::vector<Parameter> parameters;
    };

    struct Component
    {
        std::string name;
        std::string source;
        std::optional<double> stepSizeHint;
        std::vector<Connector> connectors;
        std::vector<ParameterSet> parameterSets;

        std::optional<ParameterSet> get_parameter_set(std::optional<std::string> parameterSetName) const
        {
            if (parameterSetName) {
                for (const auto& set : parameterSets) {
                    if (set.name == *parameterSetName) {
                       return set;
                    }
                }
            } else {
                if (!parameterSets.empty()) {
                    return parameterSets[0];
                }
            }
            return std::nullopt;
        }
    };

    const std::vector<Component>& get_elements() const;

    struct LinearTransformation
    {
        double offset;
        double factor;
    };

    struct Connection
    {
        std::string startElement;
        std::string startConnector;
        std::string endElement;
        std::string endConnector;
        std::optional<LinearTransformation> linearTransformation;
    };

    const std::vector<Connection>& get_connections() const;

private:
    SystemDescription systemDescription_;
    DefaultExperiment defaultExperiment_;

    std::vector<Component> elements_;
    std::vector<Connection> connections_;

    static ParameterSet parse_parameter_set(const boost::property_tree::ptree& parameterSetNode)
    {
        ParameterSet parameterSet;
        parameterSet.name = get_attribute<std::string>(parameterSetNode, "name");
        for (const auto& parameter : parameterSetNode.get_child("ssv:Parameters")) {
            const auto name = get_attribute<std::string>(parameter.second, "name");
            if (const auto realParameter = parameter.second.get_child_optional("ssv:Real")) {
                const auto value = get_attribute<double>(*realParameter, "value");
                parameterSet.parameters.push_back({name, variable_type::real, value});
            } else if (const auto intParameter = parameter.second.get_child_optional("ssv:Integer")) {
                const auto value = get_attribute<int>(*intParameter, "value");
                parameterSet.parameters.push_back({name, variable_type::integer, value});
            } else if (const auto boolParameter = parameter.second.get_child_optional("ssv:Boolean")) {
                const auto value = get_attribute<bool>(*boolParameter, "value");
                parameterSet.parameters.push_back({name, variable_type::boolean, value});
            } else if (const auto stringParameter = parameter.second.get_child_optional("ssv:String")) {
                const auto value = get_attribute<std::string>(*stringParameter, "value");
                parameterSet.parameters.push_back({name, variable_type::string, value});
            } else {
                CSE_PANIC();
            }
        }
        return parameterSet;
    }
};

ssp_parser::ssp_parser(const boost::filesystem::path& ssdPath)
{
    boost::property_tree::ptree root;
    boost::property_tree::read_xml(ssdPath.string(), root,
        boost::property_tree::xml_parser::no_comments | boost::property_tree::xml_parser::trim_whitespace);

    boost::property_tree::ptree ssd = root.get_child("ssd:SystemStructureDescription");
    systemDescription_.name = get_attribute<std::string>(ssd, "name");
    systemDescription_.version = get_attribute<std::string>(ssd, "version");

    if (const auto defaultExperiment = ssd.get_child_optional("ssd:DefaultExperiment")) {
        defaultExperiment_.startTime = get_attribute<double>(*defaultExperiment, "startTime", 0.0);
        defaultExperiment_.stopTime = get_optional_attribute<double>(*defaultExperiment, "stopTime");

        if (const auto annotations = defaultExperiment->get_child_optional("ssd:Annotations")) {
            for (const auto& annotation : *annotations) {
                const auto& annotationType = get_attribute<std::string>(annotation.second, "type");
                if (annotationType == "com.opensimulationplatform") {
                    for (const auto& algorithm : annotation.second.get_child("osp:Algorithm")) {
                        if (algorithm.first == "osp:FixedStepAlgorithm") {
                            auto baseStepSize = get_attribute<double>(algorithm.second, "baseStepSize");
                            defaultExperiment_.algorithm = std::make_unique<cse::fixed_step_algorithm>(cse::to_duration(baseStepSize));
                        } else {
                            throw std::runtime_error("Unknown algorithm: " + algorithm.first);
                        }
                    }
                }
            }
        }
    }

    boost::property_tree::ptree system = ssd.get_child("ssd:System");
    systemDescription_.system.name = get_attribute<std::string>(system, "name");
    systemDescription_.system.description = get_optional_attribute<std::string>(system, "description");

    for (const auto& component : system.get_child("ssd:Elements")) {

        auto& e = elements_.emplace_back();
        e.name = get_attribute<std::string>(component.second, "name");
        e.source = get_attribute<std::string>(component.second, "source");

        if (component.second.get_child_optional("ssd:Connectors")) {
            for (const auto& connector : component.second.get_child("ssd:Connectors")) {
                if (connector.first == "ssd:Connector") {
                    auto& c = e.connectors.emplace_back();
                    c.name = get_attribute<std::string>(connector.second, "name");
                    c.kind = get_attribute<std::string>(connector.second, "kind");
                    c.type = parse_connector_type(connector.second);
                }
            }
        }

        if (const auto parameterBindings = component.second.get_child_optional("ssd:ParameterBindings")) {
            for (const auto& parameterBinding : *parameterBindings) {
                if (const auto& source = get_optional_attribute<std::string>(parameterBinding.second, "source")) {
                    boost::property_tree::ptree binding;
                    boost::filesystem::path ssvPath = ssdPath.parent_path() / *source;
                    if (!boost::filesystem::exists(ssvPath)) {
                        std::ostringstream oss;
                        oss << "File '" << ssvPath << "' does not exists!";
                        throw std::runtime_error(oss.str());
                    }
                    boost::property_tree::read_xml(ssvPath.string(), binding,
                        boost::property_tree::xml_parser::no_comments | boost::property_tree::xml_parser::trim_whitespace);
                    const auto parameterSet = parse_parameter_set(binding.get_child("ssv:ParameterSet"));
                    e.parameterSets.push_back(parameterSet);
                } else {
                    if (const auto parameterValues = parameterBinding.second.get_child_optional("ssd:ParameterValues")) {
                        const auto parameterSet = parse_parameter_set(parameterValues->get_child("ssv:ParameterSet"));
                        e.parameterSets.push_back(parameterSet);
                    }
                }
            }
        }

        if (const auto annotations = component.second.get_child_optional("ssd:Annotations")) {
            for (const auto& annotation : *annotations) {
                const auto& annotationType = get_attribute<std::string>(annotation.second, "type");
                if (annotationType == "com.opensimulationplatform") {
                    if (const auto& stepSizeHint = annotation.second.get_child_optional("osp:StepSizeHint")) {
                        e.stepSizeHint = get_attribute<double>(*stepSizeHint, "value");
                    }
                }
            }
        }
    }

    if (const auto connections = system.get_child_optional("ssd:Connections")) {
        for (const auto& connection : *connections) {
            auto& c = connections_.emplace_back();
            c.startElement = get_attribute<std::string>(connection.second, "startElement");
            c.startConnector = get_attribute<std::string>(connection.second, "startConnector");
            c.endElement = get_attribute<std::string>(connection.second, "endElement");
            c.endConnector = get_attribute<std::string>(connection.second, "endConnector");
            if (const auto l = connection.second.get_child_optional("ssc:LinearTransformation")) {
                auto offset = get_attribute<double>(*l, "offset", 0);
                auto factor = get_attribute<double>(*l, "factor", 1.0);
                c.linearTransformation = {offset, factor};
            }
        }
    }
}

ssp_parser::~ssp_parser() noexcept = default;

const std::vector<ssp_parser::Component>& ssp_parser::get_elements() const
{
    return elements_;
}

const std::vector<ssp_parser::Connection>& ssp_parser::get_connections() const
{
    return connections_;
}

const ssp_parser::DefaultExperiment& ssp_parser::get_default_experiment() const
{
    return defaultExperiment_;
}


struct slave_info
{
    cse::simulator_index index;
    std::map<std::string, cse::variable_description> variables;
};

template<class T>
struct streamer
{
    const T& val;
};

template<class T>
streamer(T)->streamer<T>;

template<class T>
std::ostream& operator<<(std::ostream& os, streamer<T> s)
{
    os << s.val;
    return os;
}

template<class... Ts>
std::ostream& operator<<(std::ostream& os, streamer<std::variant<Ts...>> sv)
{
    std::visit([&os](const auto& v) { os << streamer{v}; }, sv.val);
    return os;
}

cse::time_point get_default_start_time(const ssp_parser& parser)
{
    return cse::to_time_point(parser.get_default_experiment().startTime);
}

cse::variable_id get_variable(
    const std::map<std::string, slave_info>& slaves,
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

} // namespace cse

#endif
