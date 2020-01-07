#include "mock_slave.hpp"

#include <cse/algorithm.hpp>
#include <cse/async_slave.hpp>
#include <cse/execution.hpp>
#include <cse/log/simple.hpp>
#include <cse/observer/file_observer.hpp>

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
        cse::log::setup_simple_console_logging();
        cse::log::set_global_output_level(cse::log::debug);

        constexpr cse::time_point startTime = cse::to_time_point(0.0);
        constexpr cse::time_point time1 = cse::to_time_point(2.0);
        constexpr cse::time_point time2 = cse::to_time_point(4.0);
        constexpr cse::time_point time3 = cse::to_time_point(6.0);
        constexpr cse::time_point time4 = cse::to_time_point(8.0);
        constexpr cse::duration stepSize = cse::to_duration(0.1);

        const auto logPath = boost::filesystem::current_path() / "logs" / "clean";
        boost::filesystem::create_directories(logPath);
        boost::filesystem::remove_all(logPath);

        // Set up the execution
        auto execution = cse::execution(startTime, std::make_unique<cse::fixed_step_algorithm>(stepSize));

        // Set up and add file observer to the execution
        auto observer = std::make_shared<cse::file_observer>(logPath);
        execution.add_observer(observer);

        // Add a slave to the execution and connect variables
        execution.add_slave(
            cse::make_pseudo_async(std::make_unique<mock_slave>([](double x) { return x - 1.1; })), "slave_one");
        execution.add_slave(
            cse::make_pseudo_async(std::make_unique<mock_slave>([](double x) { return x + 1.1; },
                [](int y) { return y - 4; },
                [](bool z) { return !z; })),
            "slave_two");

        // Run the simulation
        auto simResult = execution.simulate_until(time1);
        REQUIRE(simResult.get());

        REQUIRE(observer->is_recording());
        observer->stop_recording();
        REQUIRE(!observer->is_recording());

        REQUIRE(filecount(logPath) == 2);

        remove_directory_contents(logPath);
        REQUIRE(filecount(logPath) == 0);

        observer->start_recording();
        simResult = execution.simulate_until(time2);
        simResult.get();
        observer->stop_recording();

        REQUIRE(filecount(logPath) == 2);
        simResult = execution.simulate_until(time3);
        simResult.get();
        REQUIRE(filecount(logPath) == 2);

        std::this_thread::sleep_for(std::chrono::seconds(1));

        observer->start_recording();
        simResult = execution.simulate_until(time4);
        simResult.get();
        observer->stop_recording();
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
