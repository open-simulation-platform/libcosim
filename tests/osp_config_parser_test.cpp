#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/exception.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/orchestration.hpp>
#include <cosim/osp_config_parser.hpp>

#include <cstdlib>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>


#include "test_require.hpp"

void test(const cosim::filesystem::path& configPath, size_t expectedNumConnections)
{
    auto resolver = cosim::default_model_uri_resolver();
    const auto config = cosim::load_osp_config(configPath, *resolver);
    auto algorithm_cfg = std::get<cosim::fixed_step_algorithm_params>(config.algorithm_configuration);

    auto execution = cosim::execution(
        config.start_time,
        std::make_shared<cosim::fixed_step_algorithm>(algorithm_cfg));

    const auto entityMaps = cosim::inject_system_structure(
        execution, config.system_structure, config.initial_values);

    REQUIRE(entityMaps.simulators.size() == 4);
    REQUIRE(boost::size(config.system_structure.connections()) == expectedNumConnections);

    auto obs = std::make_shared<cosim::last_value_observer>();
    execution.add_observer(obs);

    auto result = execution.simulate_until(cosim::to_time_point(0.01));
    REQUIRE(result);

    const auto simIndex = entityMaps.simulators.at("CraneController");
    const auto varReference1 = config.system_structure.get_variable_description({"CraneController", "cl1_min"}).reference;
    double realValue = -1.0;

    obs->get_real(simIndex, gsl::make_span(&varReference1, 1), gsl::make_span(&realValue, 1));

    double magicNumberFromConf = 2.2;
    REQUIRE(std::fabs(realValue - magicNumberFromConf) < 1e-9);
}

void test_missing_model_description(const cosim::filesystem::path& configPath)
{
    class unavailable_model : public cosim::model
    {
    public:
        explicit unavailable_model(std::shared_ptr<cosim::model> delegate)
            : delegate_(std::move(delegate)) {}

        std::shared_ptr<const cosim::model_description> description() const noexcept override
        {
            return calls_++ == 0 ? delegate_->description() : nullptr;
        }

        std::shared_ptr<cosim::slave> instantiate(std::string_view name) override
        {
            return delegate_->instantiate(name);
        }

    private:
        std::shared_ptr<cosim::model> delegate_;
        mutable std::size_t calls_ = 0;
    };

    class resolver : public cosim::model_uri_sub_resolver
    {
    public:
        std::shared_ptr<cosim::model> lookup_model(const cosim::uri& uri) override
        {
            auto model = fallback_.lookup_model(uri);
            if (uri.path().find("KnuckleBoomCrane.fmu") != std::string_view::npos) {
                return std::make_shared<unavailable_model>(std::move(model));
            }
            return model;
        }

    private:
        cosim::fmu_file_uri_sub_resolver fallback_;
    };

    cosim::model_uri_resolver models;
    models.add_sub_resolver(std::make_shared<resolver>());
    try {
        cosim::load_osp_config(configPath, models);
        throw std::runtime_error("Expected a missing model description error");
    } catch (const cosim::error& error) {
        REQUIRE(std::string(error.what()).find(
            "Model description is unavailable for simulator KnuckleBoomCrane") != std::string::npos);
    }
}

void test_schema_rejection(const cosim::filesystem::path& configPath)
{
    auto resolver = cosim::default_model_uri_resolver();
    try {
        cosim::load_osp_config(configPath, *resolver);
    } catch (const std::runtime_error& error) {
        REQUIRE(std::string_view(error.what()).find("Validation of ") == 0);
        return;
    }
    throw std::runtime_error("Expected schema validation to reject " + configPath.string());
}

int main()
{
    try {
        cosim::log::setup_simple_console_logging();
        cosim::log::set_global_output_level(cosim::log::info);

        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(testDataDir);
        test(cosim::filesystem::path(testDataDir) / "msmi", 7);
        test(cosim::filesystem::path(testDataDir) / "msmi" / "OspSystemStructure_DefaultAlgorithm.xml", 7);
        test(cosim::filesystem::path(testDataDir) / "msmi" / "OspSystemStructure_Bond.xml", 9);
        test_missing_model_description(
            cosim::filesystem::path(testDataDir) / "msmi" / "OspSystemStructure_EndTime.xml");
        test_schema_rejection(
            cosim::filesystem::path(testDataDir) / "msmi" / "OspSystemStructure_V01UnsupportedInitialValue.xml");
        test_schema_rejection(
            cosim::filesystem::path(testDataDir) / "msmi" / "OspSystemStructure_UnknownVersion.xml");
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what();
        return 1;
    }
    return 0;
}
