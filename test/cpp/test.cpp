#include <cse/algorithm/fixed_step_algorithm.hpp>
#include <cse/log/simple.hpp>
#include <cse/observer/file_observer.hpp>
#include <cse/ssp_parser.hpp>

#include <chrono>

int main()
{
    try {
        cse::log::setup_simple_console_logging();
        cse::log::set_global_output_level(cse::log::debug);

        boost::filesystem::path xmlPath("C:\\Users\\LarsIvar\\Documents\\IdeaProjects\\cse\\cse-demos\\dp-ship");

        auto resolver = cse::default_model_uri_resolver();
        auto load = cse::load_ssp(*resolver, xmlPath);
        auto& execution = load.first;
        const auto t_start = std::chrono::high_resolution_clock::now();

        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        boost::filesystem::path configPath = boost::filesystem::path(testDataDir) / "LogConfig.xml";

        const auto logPath = boost::filesystem::current_path() / "logs";
        boost::filesystem::path csvPath = boost::filesystem::path(logPath);

        auto csv_observer = std::make_shared<cse::file_observer>(csvPath, configPath);
        execution.add_observer(csv_observer);

        //    auto t = measure_time_sec([&execution](){
        //        execution.simulate_until(cse::to_time_point(100));
        auto stop = cse::to_time_point(1000);
        while (execution.current_time() < stop) {
            execution.step();
        }
//        execution.simulate_until(cse::to_time_point(100));
        //    });

        const auto t_stop = std::chrono::high_resolution_clock::now();
        auto t = std::chrono::duration_cast<std::chrono::microseconds>(t_stop - t_start).count();
        std::cout << "t=" << (t * 10e-7) << "s" << std::endl;
        std::cout << "real T = " << cse::to_double_time_point(execution.current_time()) << "s" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}