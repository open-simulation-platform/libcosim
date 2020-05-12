#include "mock_slave.hpp"

#include <cosim/algorithm.hpp>
#include <cosim/async_slave.hpp>
#include <cosim/execution.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/file_observer.hpp>

#include <boost/filesystem.hpp>

#include <exception>
#include <memory>
#include <stdexcept>


// A helper macro to test various assertions
#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int64_t filecount(const boost::filesystem::path& path)
{
    return std::count_if(
        boost::filesystem::directory_iterator(path),
        boost::filesystem::directory_iterator(),
        static_cast<bool (*)(const boost::filesystem::path&)>(boost::filesystem::is_regular_file));
}

void remove_directory_contents(const boost::filesystem::path& path)
{
    for (boost::filesystem::directory_iterator end_dir_it, it(path); it != end_dir_it; ++it) {
        boost::filesystem::remove_all(it->path());
    }
}

int main()
{
    try {
        cosim::log::setup_simple_console_logging();
        cosim::log::set_global_output_level(cosim::log::debug);

        constexpr cosim::time_point startTime = cosim::to_time_point(0.0);
        constexpr cosim::duration stepSize = cosim::to_duration(0.1);

        const auto logPath = boost::filesystem::current_path() / "logs" / "clean";
        boost::filesystem::create_directories(logPath);
        boost::filesystem::remove_all(logPath);

        // Set up the execution
        auto execution = cosim::execution(startTime, std::make_unique<cosim::fixed_step_algorithm>(stepSize));

        // Set up and add file observer to the execution
        auto observer = std::make_shared<cosim::file_observer>(logPath);
        execution.add_observer(observer);

        // Add slaves to the execution
        execution.add_slave(
            cosim::make_pseudo_async(std::make_unique<mock_slave>(
                [](double x) { return x - 1.1; })),
            "slave_one");
        execution.add_slave(
            cosim::make_pseudo_async(std::make_unique<mock_slave>(
                [](double x) { return x + 1.1; },
                [](int y) { return y - 4; },
                [](bool z) { return !z; })),
            "slave_two");

        // Run the simulation
        auto t = std::thread([&]() { execution.simulate_until(std::nullopt).get(); });

        constexpr std::chrono::duration sleepTime = std::chrono::milliseconds(500);

        std::this_thread::sleep_for(sleepTime);

        REQUIRE(observer->is_recording());
        observer->stop_recording();
        REQUIRE(!observer->is_recording());

        REQUIRE(filecount(logPath) == 2);

        remove_directory_contents(logPath);
        REQUIRE(filecount(logPath) == 0);

        observer->start_recording();
        std::this_thread::sleep_for(sleepTime);
        observer->stop_recording();

        std::this_thread::sleep_for(sleepTime);

        observer->start_recording();
        std::this_thread::sleep_for(sleepTime);
        observer->stop_recording();

        execution.stop_simulation();
        t.join();
        REQUIRE(filecount(logPath) == 4);

        // Test that files are released.
        remove_directory_contents(logPath);
        REQUIRE(filecount(logPath) == 0);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
