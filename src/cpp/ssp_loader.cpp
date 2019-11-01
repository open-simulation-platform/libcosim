#include "cse/ssp_loader.hpp"

#include "cse/algorithm.hpp"
#include "cse/error.hpp"
#include "cse/exception.hpp"
#include "cse/fmi/fmu.hpp"
#include "cse/log/logger.hpp"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <string>
#include <variant>

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

    struct Parameter
    {
        std::string name;
        cse::variable_type type;
        std::variant<double, int, bool, std::string> value;
    };

    struct Component
    {
        std::string name;
        std::string source;
        std::vector<Connector> connectors;
        std::vector<Parameter> parameters;
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
        defaultExperiment_.startTime = get_attribute<double>(*defaultExperiment, "startTime", 0.0);
        defaultExperiment_.stopTime = get_optional_attribute<double>(*defaultExperiment, "stopTime");

        if (const auto annotations = defaultExperiment->get_child_optional("ssd:Annotations")) {
            for (const auto& annotation : *annotations) {
                const auto& annotationType = get_attribute<std::string>(annotation.second, "type");
                if (annotationType == "com.opensimulationplatform") {
                    for (const auto& algorithm : annotation.second.get_child("osp:Algorithm")) {
                        if (algorithm.first == "osp:FixedStepAlgorithm") {
                            double stepSize = get_attribute<double>(algorithm.second, "stepSize");
                            defaultExperiment_.algorithm = std::make_unique<cse::fixed_step_algorithm>(cse::to_duration(stepSize));
                        } else {
                            throw std::runtime_error("Unknown algorithm: " + algorithm.first);
                        }
                    }
                }
            }
        }
    }

    tmpTree = pt_.get_child(path + ".ssd:System");
    systemDescription_.systemName = get_attribute<std::string>(tmpTree, "name");
    systemDescription_.systemDescription = get_attribute<std::string>(tmpTree, "description");

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

        if (const auto parameterBindings = component.second.get_child_optional("ssd:ParameterBindings")) {
            for (const auto& parameterBinding : *parameterBindings) {
                if (const auto parameterValues = parameterBinding.second.get_child_optional("ssd:ParameterValues")) {
                    const auto parameterSet = parameterValues->get_child("ssv:ParameterSet");
                    const auto parameters = parameterSet.get_child("ssv:Parameters");
                    for (const auto& parameter : parameters) {
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


ssp_loader::ssp_loader()
    : modelResolver_(cse::default_model_uri_resolver())
{}

ssp_loader::ssp_loader(const std::shared_ptr<cse::model_uri_resolver>& modelResolver)
    : modelResolver_(modelResolver == nullptr ? cse::default_model_uri_resolver() : modelResolver)
{
}

void ssp_loader::set_start_time(cse::time_point timePoint)
{
    overrideStartTime_ = timePoint;
}

void ssp_loader::set_algorithm(std::shared_ptr<cse::algorithm> algorithm)
{
    overrideAlgorithm_ = algorithm;
}

std::pair<execution, simulator_map> ssp_loader::load(
    const boost::filesystem::path& configPath)
{
    simulator_map simulatorMap;
    const auto absolutePath = boost::filesystem::absolute(configPath);
    const auto configFile = boost::filesystem::is_regular_file(absolutePath)
        ? absolutePath
        : absolutePath / "SystemStructure.ssd";
    const auto baseURI = path_to_file_uri(configFile);
    const auto parser = ssp_parser(configFile);

    std::shared_ptr<cse::algorithm> algorithm;
    if (overrideAlgorithm_ != nullptr) {
        algorithm = overrideAlgorithm_;
    } else if (parser.get_default_experiment().algorithm != nullptr) {
        algorithm = parser.get_default_experiment().algorithm;
    } else {
        throw std::invalid_argument("SSP contains no default co-simulation algorithm, nor has one been explicitly specified!");
    }

    const auto startTime = overrideStartTime_ ? *overrideStartTime_ : get_default_start_time(parser);
    auto execution = cse::execution(startTime, algorithm);

    auto elements = parser.get_elements();

    std::map<std::string, slave_info> slaves;
    for (const auto& component : elements) {
        auto model = modelResolver_->lookup_model(baseURI, component.source);
        auto slave = model->instantiate(component.name);
        simulator_index index = slaves[component.name].index = execution.add_slave(slave, component.name);

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

        const auto c = std::make_shared<scalar_connection>(output, input);
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
