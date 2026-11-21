#include "mock_slave.hpp"

#include <cosim/algorithm.hpp>
#include <cosim/execution.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/file_observer.hpp>

#include <chrono>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>


// A helper macro to test various assertions
#include "test_require.hpp"

int64_t filecount(const cosim::filesystem::path& path)
{
    return std::count_if(
        cosim::filesystem::directory_iterator(path),
        cosim::filesystem::directory_iterator(),
        static_cast<bool (*)(const cosim::filesystem::path&)>(cosim::filesystem::is_regular_file));
}

void remove_directory_contents(const cosim::filesystem::path& path)
{
    for (cosim::filesystem::directory_iterator end_dir_it, it(path); it != end_dir_it; ++it) {
        cosim::filesystem::remove_all(it->path());
    }
}

class progress_file_observer final : public cosim::file_observer
{
public:
    using cosim::file_observer::file_observer;

    void step_complete(cosim::step_number step, cosim::duration size, cosim::time_point time) override
    {
        cosim::file_observer::step_complete(step, size, time);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++completedSteps_;
        }
        progressed_.notify_one();
    }

    bool wait_for_complete_step()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // A step may already be in progress when recording is toggled.
        const auto target = completedSteps_ + 2;
        return progressed_.wait_for(lock, std::chrono::seconds(10), [&] {
            return completedSteps_ >= target;
        });
    }

private:
    std::mutex mutex_;
    std::condition_variable progressed_;
    std::size_t completedSteps_ = 0;
};

int main()
{
    try {
        cosim::log::setup_simple_console_logging();
        cosim::log::set_global_output_level(cosim::log::debug);

        constexpr cosim::time_point startTime = cosim::to_time_point(0.0);
        constexpr cosim::duration stepSize = cosim::to_duration(0.1);

        const auto logPath = cosim::filesystem::current_path() / "logs" / "clean";
        cosim::filesystem::create_directories(logPath);
        cosim::filesystem::remove_all(logPath);

        // Set up the execution
        auto execution = cosim::execution(startTime, std::make_unique<cosim::fixed_step_algorithm>(stepSize));

        // Set up and add file observer to the execution
        auto observer = std::make_shared<progress_file_observer>(logPath);
        execution.add_observer(observer);

        // Add slaves to the execution
        execution.add_slave(
            std::make_unique<mock_slave>(
                [](double x) { return x - 1.1; }),
            "slave_one");
        execution.add_slave(
            std::make_unique<mock_slave>(
                [](double x) { return x + 1.1; },
                [](int y) { return y - 4; },
                [](bool z) { return !z; }),
            "slave_two");

        // Run the simulation
        auto t = execution.simulate_until_async(std::nullopt);

        try {
            REQUIRE(observer->wait_for_complete_step());
            REQUIRE(observer->is_recording());
            observer->stop_recording();
            REQUIRE(!observer->is_recording());

            REQUIRE(filecount(logPath) == 4);

            remove_directory_contents(logPath);
            REQUIRE(filecount(logPath) == 0);

            observer->start_recording();
            REQUIRE(observer->wait_for_complete_step());
            observer->stop_recording();
            REQUIRE(filecount(logPath) == 4);

            REQUIRE(observer->wait_for_complete_step());
            REQUIRE(filecount(logPath) == 4);

            observer->start_recording();
            REQUIRE(observer->wait_for_complete_step());
            observer->stop_recording();
        } catch (...) {
            execution.stop_simulation();
            t.wait();
            throw;
        }

        execution.stop_simulation();
        t.get();

        REQUIRE(filecount(logPath) == 8);

        // Test that files are released.
        remove_directory_contents(logPath);

        REQUIRE(filecount(logPath) == 0);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
