#include "cse/ssp_parser.hpp"

#include "cse/algorithm.hpp"
#include "cse/error.hpp"
#include "cse/exception.hpp"
#include "cse/fmi/fmu.hpp"
#include "cse/log/logger.hpp"
#include <cse/utility/filesystem.hpp>
#include <cse/utility/zip.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <string>
#include <variant>
#include <optional>

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


class ssp_parser
{

public:
    explicit ssp_parser(const boost::filesystem::path& ssdPath);
    ~ssp_parser() noexcept;

    struct DefaultExperiment
    {
        double startTime = 0.0;
        std::optional<double> stopTime;
        std::shared_ptr<cse::algorithm> algorithm;
    };

    const DefaultExperiment& get_default_experiment() const;

    struct System {
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

    struct Component
    {
        std::string name;
        std::string source;
        std::optional<double> stepSizeHint;
        std::vector<Connector> connectors;
        std::vector<Parameter> parameters;
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

    static void parse_parameter_set(Component& e, const boost::property_tree::ptree& parameterSet)
    {
        for (const auto& parameter : parameterSet.get_child("ssv:Parameters")) {
            const auto name = get_attribute<std::string>(parameter.second, "name");
            if (const auto realParameter = parameter.second.get_child_optional("ssv:Real")) {
                const auto value = get_attribute<double>(*realParameter, "value");
                e.parameters.push_back({name, variable_type::real, value});
            } else if (const auto intParameter = parameter.second.get_child_optional("ssv:Integer")) {
                const auto value = get_attribute<int>(*intParameter, "value");
                e.parameters.push_back({name, variable_type::integer, value});
            } else if (const auto boolParameter = parameter.second.get_child_optional("ssv:Boolean")) {
                const auto value = get_attribute<bool>(*boolParameter, "value");
                e.parameters.push_back({name, variable_type::boolean, value});
            } else if (const auto stringParameter = parameter.second.get_child_optional("ssv:String")) {
                const auto value = get_attribute<std::string>(*stringParameter, "value");
                e.parameters.push_back({name, variable_type::string, value});
            } else {
                CSE_PANIC();
            }
        }
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
                    parse_parameter_set(e, binding.get_child("ssv:ParameterSet"));
                } else {
                    if (const auto parameterValues = parameterBinding.second.get_child_optional("ssd:ParameterValues")) {
                        parse_parameter_set(e, parameterValues->get_child("ssv:ParameterSet"));
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

} // namespace


std::pair<execution, simulator_map> load_ssp(
    cse::model_uri_resolver& resolver,
    const boost::filesystem::path& configPath,
    std::optional<cse::time_point> overrideStartTime)
{
    return load_ssp(resolver, configPath, nullptr, overrideStartTime);
}

std::pair<execution, simulator_map> load_ssp(
    cse::model_uri_resolver& resolver,
    const boost::filesystem::path& configPath,
    std::shared_ptr<cse::algorithm> overrideAlgorithm,
    std::optional<cse::time_point> overrideStartTime)
{
    auto sspFile = configPath;
    std::optional<cse::utility::temp_dir> temp_ssp_dir;
    if (sspFile.extension() == ".ssp") {
        temp_ssp_dir = cse::utility::temp_dir();
        auto archive = cse::utility::zip::archive(sspFile);
        archive.extract_all(temp_ssp_dir->path());
        sspFile = temp_ssp_dir->path();
    }

    simulator_map simulatorMap;
    const auto absolutePath = boost::filesystem::absolute(sspFile);
    const auto configFile = boost::filesystem::is_regular_file(absolutePath)
        ? absolutePath
        : absolutePath / "SystemStructure.ssd";
    const auto baseURI = path_to_file_uri(configFile);
    const auto parser = ssp_parser(configFile);

    std::shared_ptr<cse::algorithm> algorithm;
    if (overrideAlgorithm != nullptr) {
        algorithm = overrideAlgorithm;
    } else if (parser.get_default_experiment().algorithm != nullptr) {
        algorithm = parser.get_default_experiment().algorithm;
    } else {
        throw std::invalid_argument("SSP contains no default co-simulation algorithm, nor has one been explicitly specified!");
    }

    const auto startTime = overrideStartTime ? *overrideStartTime : get_default_start_time(parser);
    auto execution = cse::execution(startTime, algorithm);

    auto elements = parser.get_elements();

    std::map<std::string, slave_info> slaves;
    for (const auto& component : elements) {
        auto model = resolver.lookup_model(baseURI, component.source);
        auto slave = model->instantiate(component.name);
        auto stepSizeHint = cse::to_duration(component.stepSizeHint.value_or(0));
        simulator_index index = slaves[component.name].index = execution.add_slave(slave, component.name, stepSizeHint);

        simulatorMap[component.name] = simulator_map_entry{index, component.source, *model->description()};

        for (const auto& v : model->description()->variables) {
            slaves[component.name].variables[v.name] = v;
        }

        for (const auto& p : component.parameters) {
            auto reference = find_variable(*model->description(), p.name).reference;
            BOOST_LOG_SEV(log::logger(), log::info)
                << "Initializing variable " << component.name << ":" << p.name << " with value " << streamer{p.value};
            switch (p.type) {
                case variable_type::real:
                    execution.set_real_initial_value(index, reference, std::get<double>(p.value));
                    break;
                case variable_type::integer:
                    execution.set_integer_initial_value(index, reference, std::get<int>(p.value));
                    break;
                case variable_type::boolean:
                    execution.set_boolean_initial_value(index, reference, std::get<bool>(p.value));
                    break;
                case variable_type::string:
                    execution.set_string_initial_value(index, reference, std::get<std::string>(p.value));
                    break;
                default:
                    throw error(make_error_code(errc::unsupported_feature), "Variable type not supported yet");
            }
        }
    }

    for (const auto& connection : parser.get_connections()) {

        cse::variable_id output = get_variable(slaves, connection.startElement, connection.startConnector);
        cse::variable_id input = get_variable(slaves, connection.endElement, connection.endConnector);

        std::shared_ptr<cse::scalar_connection> c;
        if (const auto& l = connection.linearTransformation) {
            c = std::make_shared<linear_transformation_connection>(output, input, l->offset, l->factor);
        } else {
            c = std::make_shared<scalar_connection>(output, input);
        }
        try {
            execution.add_connection(c);
        } catch (const std::exception& e) {
            std::ostringstream oss;
            oss << "Encountered error while adding connection from "
                << connection.startElement << ":" << connection.startConnector << " to "
                << connection.endElement << ":" << connection.endConnector
                << ": " << e.what();

            BOOST_LOG_SEV(log::logger(), log::error) << oss.str();
            throw std::runtime_error(oss.str());
        }
    }

    return std::make_pair(std::move(execution), std::move(simulatorMap));
}

} // namespace cse
