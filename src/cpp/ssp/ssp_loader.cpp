#include "cse/ssp/ssp_loader.hpp"

#include "cse/algorithm.hpp"
#include "cse/cse_config.hpp"
#include "cse/exception.hpp"
#include "cse/fmi/fmu.hpp"
#include "cse/log/logger.hpp"
#include "cse/ssp/ssp_parser.hpp"
#include "cse/utility/filesystem.hpp"
#include "cse/utility/zip.hpp"

namespace cse
{

ssp_loader::ssp_loader()
    : modelResolver_(cse::default_model_uri_resolver())
{}

void ssp_loader::override_start_time(cse::time_point timePoint)
{
    overrideStartTime_ = timePoint;
}

void ssp_loader::override_algorithm(std::shared_ptr<cse::algorithm> algorithm)
{
    overrideAlgorithm_ = std::move(algorithm);
}

void ssp_loader::set_custom_model_uri_resolver(std::shared_ptr<cse::model_uri_resolver> modelResolver)
{
    modelResolver_ = std::move(modelResolver);
}

std::pair<execution, simulator_map> ssp_loader::load(
    const boost::filesystem::path& configPath,
    std::optional<std::string> customSsdFileName)
{
    auto sspFile = configPath;
    std::optional<cse::utility::temp_dir> temp_ssp_dir;
    if (sspFile.extension() == ".ssp") {
        temp_ssp_dir = cse::utility::temp_dir();
        auto archive = cse::utility::zip::archive(sspFile);
        archive.extract_all(temp_ssp_dir->path());
        sspFile = temp_ssp_dir->path();
    }

    const auto ssdFileName = customSsdFileName ? *customSsdFileName : "SystemStructure";
    const auto absolutePath = boost::filesystem::absolute(sspFile);
    const auto configFile = boost::filesystem::is_regular_file(absolutePath)
        ? absolutePath
        : absolutePath / std::string(ssdFileName + ".ssd");
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

    simulator_map simulatorMap;
    std::map<std::string, slave_info> slaves;
    auto elements = parser.get_elements();
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
