
#include <iostream>
#include <string>
#include <chrono>
#include <thread>

//must appear before other cse headers due to <winsock2.h> #include
#include <cse/fmuproxy/fmuproxy_client.hpp>
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

    cse::time_point t;
    std::shared_ptr<cse::slave> slaves[NUM_FMUS];
    for (auto &slave : slaves) {
        slave = fmu.instantiate_slave();
        slave->setup(t, {}, {});
        slave->start_simulation();
    }

    auto elapsed = measure_time_sec([&t, &slaves] {
        for (int i = 0; i < numSteps; i++) {
            for (auto &slave : slaves) {
                slave->do_step(t, stepSize);
            }
            t += stepSize;
        }
    });

    for (auto &slave : slaves) {
        slave->end_simulation();
    }

    std::cout << "[serial] elapsed=" << elapsed << "s" << std::endl;

}

void run_execution(remote_fmu &fmu) {

    auto execution = cse::execution(cse::time_point(),
                                    std::make_unique<cse::fixed_step_algorithm>(cse::to_duration(stepSize_s)));

    for (int i = 0; i < NUM_FMUS; i++) {
        auto slave = fmu.instantiate();
        execution.add_slave( slave, std::string("slave_") + std::to_string(i));
    }

    execution.step();
    auto elapsed = measure_time_sec([&execution] {
        for (int i = 0; i < numSteps-1; i++) {
            execution.step();
        }
    });

    std::cout << "[execution] elapsed=" << elapsed << "s" << std::endl;

}

void run_threads(remote_fmu &fmu) {

    cse::time_point t = cse::time_point();
    std::shared_ptr<cse::slave> slaves[NUM_FMUS];

    for (auto &slave : slaves) {
        slave = fmu.instantiate_slave();
        slave->setup(t, {}, {});
        slave->start_simulation();
    }

    auto elapsed = measure_time_sec([&slaves, &t] {

        for (int i = 0; i < numSteps; i++) {
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

    for (auto &slave : slaves) {
        slave->end_simulation();
    }

    std::cout << "[threads] elapsed=" << elapsed << "s" << std::endl;

}

int main(int argc, char** argv) {

    if (argc != 4) {
        std::cerr << "Missing one or more program arguments: [url:string host:string port:int]" << std::endl;
        return 1;
    }

    auto url = argv[1];
    auto host = argv[2];
    auto port = std::stoi(argv[3]);

    fmuproxy_client client1(host, port, /*concurrent*/false);
    auto fmu1 = client1.from_url(url);
    run_serial(fmu1);

    fmuproxy_client client2(host, port, /*concurrent*/true);
    auto fmu2 = client2.from_url(url);
    run_execution(fmu2);
    run_threads(fmu2);

    return 0;

}