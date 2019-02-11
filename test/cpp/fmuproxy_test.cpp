
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <utility>

//must appear before other cse headers due to <winsock2.h> #include
#include <cse/fmuproxy/remote_fmu.hpp>

#include <cse/execution.hpp>
#include <cse/algorithm.hpp>

using namespace cse;
using namespace cse::fmuproxy;

const int NUM_FMUS = 25;

const double stop = 1;
const double stepSize_s = 1.0 / 100;
const int numSteps = (int) (stop / stepSize_s);
const cse::duration stepSize = cse::to_duration(stepSize_s);

template <typename function>
inline float measure_time_sec(function &&fun) {
    auto t_start =  std::chrono::high_resolution_clock::now();
    fun();
    auto t_stop =  std::chrono::high_resolution_clock::now();
    return std::chrono::duration<float>(t_stop - t_start).count();
}

void run_serial(remote_fmu &fmu) {

    auto elapsed = measure_time_sec([&fmu] {
        for (int i = 0; i < NUM_FMUS; i++) {
            cse::time_point t = cse::time_point();
            auto slave = fmu.instantiate_slave();
            slave->setup(t, {}, {});
            slave->start_simulation();

            for (int j = 0; j < numSteps; j++) {
                slave->do_step(t, stepSize);
                t += stepSize;
            }

        }
    });

    std::cout << "[serial] elapsed=" << elapsed << "s" << std::endl;

}

void run_execution(remote_fmu &fmu) {

    auto execution = cse::execution(cse::time_point(),
                                    std::make_unique<cse::fixed_step_algorithm>(cse::to_duration(stepSize_s)));

    auto elapsed = measure_time_sec([&fmu, &execution] {
        for (int i = 0; i < NUM_FMUS; i++) {
            auto slave = fmu.instantiate_slave();
            execution.add_slave(cse::make_pseudo_async(slave), std::string("slave") + std::to_string(i));
        }

        for (int i = 0; i < numSteps; i++) {
            execution.step({});
        }

    });

    std::cout << "[fibers] elapsed=" << elapsed << "s" << std::endl;

}

void run_threads(remote_fmu &fmu) {

    auto elapsed = measure_time_sec([&fmu] {

        cse::time_point t = cse::time_point();
        std::shared_ptr<cse::slave> slaves[NUM_FMUS];

        for (auto &i : slaves) {
            auto slave = fmu.instantiate_slave();
            slave->setup(t, {}, {});
            slave->start_simulation();
            i = slave;
        }

        for (int k = 0; k < numSteps; k++) {
            std::thread threads[NUM_FMUS];
            for (int j = 0; j < NUM_FMUS; j++) {
                auto slave = slaves[j];
                threads[j] = std::thread([slave, t]{
                    slave->do_step(t, stepSize);
                });
            }
            t += stepSize;

            for (auto &thread : threads) {
                thread.join();
            }

        }

    });

    std::cout << "[threads] elapsed=" << elapsed << "s" << std::endl;

}

int main(int argc, char** argv) {

    if (argc != 4) {
        std::cerr << "Missing one or more program arguments: [fmuId:string host:string port:int]" << std::endl;
        return 1;
    }

    auto fmuId = argv[1];
    auto host = argv[2];
    auto port = std::stoi(argv[3]);
    auto fmu1 = remote_fmu::from_guid(fmuId, host, port, /*concurrent*/false);

    run_serial(fmu1);
    run_execution(fmu1);

    auto fmu2 = remote_fmu::from_guid(fmuId, host, port, /*concurrent*/true);
    run_threads(fmu2);

    return 0;

}