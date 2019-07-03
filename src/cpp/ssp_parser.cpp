#include "cse/ssp_parser.hpp"

#include "cse/algorithm.hpp"
#include "cse/exception.hpp"
#include "cse/fmi/fmu.hpp"
#include "cse/log.hpp"
#include "cse/log/logger.hpp"
#include <cse/error.hpp>

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
    };

    const DefaultExperiment& get_default_experiment() const;

    struct SimulationInformation
    {
        std::string description;
        double stepSize;
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
    SimulationInformation simulationInformation_;
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
    }


    tmpTree = pt_.get_child(path + ".ssd:System");
    systemDescription_.systemName = get_attribute<std::string>(tmpTree, "name");
    systemDescription_.systemDescription = get_attribute<std::string>(tmpTree, "description");

    if (const auto annotations = tmpTree.get_child_optional("ssd:Annotations")) {
        for (const auto& annotation : *annotations) {
            const auto& annotationType = get_attribute<std::string>(annotation.second, "type");
            if (annotationType == "org.open-simulation-platform") {
                for (const auto& infos : annotation.second.get_child("osp:SimulationInformation")) {
                    if (infos.first == "osp:FixedStepMaster") {
                        simulationInformation_.description = get_attribute<std::string>(infos.second, "description");
                        simulationInformation_.stepSize = get_attribute<double>(infos.second, "stepSize");
                    }
                }
            }
        }
    }

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

const ssp_parser::SimulationInformation& ssp_parser::get_simulation_information() const
{
    return simulationInformation_;
}

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

} // namespace


std::pair<execution, simulator_map> load_ssp(
    cse::model_uri_resolver& resolver,
    const boost::filesystem::path& sspDir,
    std::optional<cse::time_point> overrideStartTime)
{
    simulator_map simulatorMap;
    const auto ssdPath = sspDir / "SystemStructure.ssd";
    const auto baseURI = path_to_file_uri(ssdPath);
    const auto parser = ssp_parser(ssdPath);

    const auto& simInfo = parser.get_simulation_information();
    const cse::duration stepSize = cse::to_duration(simInfo.stepSize);

    auto elements = parser.get_elements();

    const auto startTime = overrideStartTime ? *overrideStartTime : cse::to_time_point(parser.get_default_experiment().startTime);

    auto execution = cse::execution(
        startTime,
        std::make_unique<cse::fixed_step_algorithm>(stepSize));

    std::map<std::string, slave_info> slaves;
    for (const auto& component : elements) {
        auto model = resolver.lookup_model(baseURI, component.source);
        auto slave = model->instantiate(component.name);
        simulator_index index = slaves[component.name].index = execution.add_slave(slave, component.name);

        simulatorMap[component.name] = simulator_map_entry{index, component.source, *model->description()};

        for (const auto& v : model->description()->variables) {
            slaves[component.name].variables[v.name] = v;
        }

        for (const auto& p : component.parameters) {
            auto varIndex = find_variable(*model->description(), p.name).index;
            BOOST_LOG_SEV(log::logger(), log::level::info)
                << "Initializing variable " << component.name << ":" << p.name << " with value " << streamer{p.value};
            switch (p.type) {
                case variable_type::real:
                    execution.set_real_initial_value(index, varIndex, std::get<double>(p.value));
                    break;
                case variable_type::integer:
                    execution.set_integer_initial_value(index, varIndex, std::get<int>(p.value));
                    break;
                case variable_type::boolean:
                    execution.set_boolean_initial_value(index, varIndex, std::get<bool>(p.value));
                    break;
                case variable_type::string:
                    execution.set_string_initial_value(index, varIndex, std::get<std::string>(p.value));
                    break;
                default:
                    throw error(make_error_code(errc::unsupported_feature), "Variable type not supported yet");
            }
        }
    }

    for (const auto& connection : parser.get_connections()) {
        cse::variable_id output = {slaves[connection.startElement].index,
            slaves[connection.startElement].variables[connection.startConnector].type,
            slaves[connection.startElement].variables[connection.startConnector].index};

        cse::variable_id input = {slaves[connection.endElement].index,
            slaves[connection.endElement].variables[connection.endConnector].type,
            slaves[connection.endElement].variables[connection.endConnector].index};

        execution.connect_variables(output, input);
    }

    return std::make_pair(std::move(execution), std::move(simulatorMap));
}

} // namespace cse
