#include "cse/cse_config_parser.hpp"

#include "cse/algorithm.hpp"
#include "cse/fmi/fmu.hpp"

#include <nlohmann/json.hpp>
#include <string>


namespace cse
{

namespace
{

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
    };

    const std::vector<Simulator>& get_elements() const;

    struct Connection
    {
        std::string sourceSimulator;
        std::string sourceConnector;
        std::string targetSimulator;
        std::string targetConnector;
    };

    const std::vector<Connection>& get_variable_connections() const;
    const std::vector<Connection>& get_plug_socket_connections() const;
    const std::vector<Connection>& get_bond_connections() const;

private:
    SystemDescription systemDescription_;
    SimulationInformation simulationInformation_;
    std::vector<Simulator> simulators_;
    std::vector<Connection> variableConnections_;
    std::vector<Connection> plugSocketConnections_;
    std::vector<Connection> bondConnections_;
};

cse_config_parser::cse_config_parser(const boost::filesystem::path& configPath)
{
    // Root node
    boost::filesystem::ifstream i(configPath);
    nlohmann::json j;
    i >> j;

    for (const auto& simulator : j.at("simulators")) {
        const auto name = simulator.at("name").get<std::string>();
        const auto source = simulator.at("source").get<std::string>();
        simulators_.push_back({name, source});
    }
    if (j.count("description")) {
        simulationInformation_.description = j.at("description").get<std::string>();
    }
    simulationInformation_.stepSize = j.at("baseStepSize").get<double>();
    if (j.count("startTime")) {
        simulationInformation_.startTime = j.at("startTime").get<double>();
    }

    if (j.count("variableConnections")) {
        for (const auto& connection : j.at("variableConnections")) {
            variableConnections_.push_back({connection.at("sourceSimulator").get<std::string>(),
                                            connection.at("sourceVariable").get<std::string>(),
                                            connection.at("targetSimulator").get<std::string>(),
                                            connection.at("targetVariable").get<std::string>()});
        }
    }

    if (j.count("plugSocketConnections")) {
        for (const auto& plugSocketConnection : j.at("plugSocketConnections")) {
            plugSocketConnections_.push_back({plugSocketConnection.at("sourceSimulator").get<std::string>(),
                                              plugSocketConnection.at("plug").get<std::string>(),
                                              plugSocketConnection.at("targetSimulator").get<std::string>(),
                                              plugSocketConnection.at("socket").get<std::string>()});
        }
    }

    if (j.count("bondConnections")) {
        for (const auto &bondConnection : j.at("bondConnections")) {
            bondConnections_.push_back({bondConnection.at("simulatorA").get<std::string>(),
                                        bondConnection.at("bondA").get<std::string>(),
                                        bondConnection.at("simulatorB").get<std::string>(),
                                        bondConnection.at("bondB").get<std::string>()});
        }
    }
}

cse_config_parser::~cse_config_parser() noexcept = default;

const cse_config_parser::SimulationInformation&cse_config_parser::get_simulation_information() const
{
    return simulationInformation_;
}

const std::vector<cse_config_parser::Simulator>&cse_config_parser::get_elements() const
{
    return simulators_;
}

const std::vector<cse_config_parser::Connection>&cse_config_parser::get_variable_connections() const
{
    return variableConnections_;
}

const std::vector<cse_config_parser::Connection>&cse_config_parser::get_plug_socket_connections() const
{
    return plugSocketConnections_;
}

const std::vector<cse_config_parser::Connection>&cse_config_parser::get_bond_connections() const
{
    return bondConnections_;
}

std::vector<std::string> parse_string_vector(const nlohmann::json& element) {
    std::vector<std::string> v;
    for (const auto& child : element) {
        v.emplace_back(child.get<std::string>());
    }

    return v;
}

struct slave_info
{
    cse::simulator_index index;
    std::map<std::string, cse::variable_description> variables;
};


struct plug_socket_description {
    std::string type;
    std::vector<std::string> variables;
};

struct bond_description {
    std::vector<std::string> plugs;
    std::vector<std::string> sockets;
};

struct extended_model_description
{

    explicit extended_model_description(const boost::filesystem::path& msmiJson)
    {
        boost::filesystem::ifstream i(msmiJson);
        nlohmann::json j;
        i >> j;

        for (const auto& plug : j.at("plugs")) {
            plug_socket_description p;
            const auto name = plug.at("name").get<std::string>();
            p.type = plug.at("type").get<std::string>();
            p.variables = parse_string_vector(plug.at("variables"));
            plugs[name] = std::move(p);
        }

        for (const auto& socket : j.at("sockets")) {
            plug_socket_description s;
            const auto name = socket.at("name").get<std::string>();
            s.type = socket.at("type").get<std::string>();
            s.variables = parse_string_vector(socket.at("variables"));
            sockets[name] = std::move(s);
        }

        for (const auto& bond : j.at("bonds")) {
            bond_description b;
            const auto name = bond.at("name").get<std::string>();
            b.plugs = parse_string_vector(bond.at("plugs"));
            b.sockets = parse_string_vector(bond.at("sockets"));
            bonds[name] = std::move(b);
        }
    }

    std::unordered_map<std::string, plug_socket_description> plugs;
    std::unordered_map<std::string, plug_socket_description> sockets;
    std::unordered_map<std::string, bond_description> bonds;
};

} // namespace

void connect_variables(
    std::vector<cse_config_parser::Connection> variableConnections,
    std::unordered_map<std::string, slave_info>& slaves,
    cse::execution& execution) {

    for (const auto& connection : variableConnections) {
        cse::variable_id output = {slaves[connection.sourceSimulator].index,
                                   slaves[connection.sourceSimulator].variables[connection.sourceConnector].type,
                                   slaves[connection.sourceSimulator].variables[connection.sourceConnector].index};

        cse::variable_id input = {slaves[connection.targetSimulator].index,
                                  slaves[connection.targetSimulator].variables[connection.targetConnector].type,
                                  slaves[connection.targetSimulator].variables[connection.targetConnector].index};


        execution.connect_variables(output, input);
    }
}

std::pair<execution, simulator_map> load_cse_config(
    cse::model_uri_resolver& resolver,
    const boost::filesystem::path& configPath,
    std::optional<cse::time_point> overrideStartTime)
{
    simulator_map simulatorMap;
    const auto configFile = configPath / "mapping.json";
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
        
        std::string msmiFileName = model->description()->name + ".json";
        const auto msmiFilePath = configPath / msmiFileName;
        emds.emplace(simulator.name, msmiFilePath);
    }

    connect_variables(parser.get_variable_connections(), slaves, execution);

//    for (const auto& connection : parser.get_variable_connections()) {
//        cse::variable_id output = {slaves[connection.sourceSimulator].index,
//            slaves[connection.sourceSimulator].variables[connection.sourceConnector].type,
//            slaves[connection.sourceSimulator].variables[connection.sourceConnector].index};
//
//        cse::variable_id input = {slaves[connection.targetSimulator].index,
//            slaves[connection.targetSimulator].variables[connection.targetConnector].type,
//            slaves[connection.targetSimulator].variables[connection.targetConnector].index};
//
//        execution.connect_variables(output, input);
//    }

    for (const auto& connection : parser.get_plug_socket_connections()) {

        const auto& plugVariables = emds.at(connection.sourceSimulator).plugs.at(connection.sourceConnector).variables;
        const auto& socketVariables = emds.at(connection.targetSimulator).sockets.at(connection.targetConnector).variables;

        assert(plugVariables.size() == socketVariables.size());

        for (std::size_t i = 0; i < plugVariables.size(); ++i) {
            const auto& plugVarName = plugVariables.at(i);
            const auto& socketVarName = socketVariables.at(i);

            std::cout << "Connecting " << connection.sourceSimulator << "." << plugVarName << " to "
                << connection.targetSimulator << "." << socketVarName << std::endl;

            cse::variable_id output = {slaves[connection.sourceSimulator].index,
                                       slaves[connection.sourceSimulator].variables[plugVarName].type,
                                       slaves[connection.sourceSimulator].variables[plugVarName].index};

            cse::variable_id input = {slaves[connection.targetSimulator].index,
                                      slaves[connection.targetSimulator].variables[socketVarName].type,
                                      slaves[connection.targetSimulator].variables[socketVarName].index};

            execution.connect_variables(output, input);
        }
    }

    return std::make_pair(std::move(execution), std::move(simulatorMap));
}

} // namespace cse
