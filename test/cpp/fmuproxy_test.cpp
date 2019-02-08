
#include <iostream>
#include <string>
#include <chrono>
#include <thread>

//must appear before other cse headers due to <winsock2.h> #include
#include <cse/fmuproxy/remote_fmu.hpp>

#include <cse/execution.hpp>
#include <cse/algorithm.hpp>

using namespace cse;
using namespace cse::fmuproxy;

const int numFmus = 25;
const double stop = 1;
const double stepSize = 1.0 / 100;
const int numSteps = (int) (stop / stepSize);

typedef std::chrono::high_resolution_clock Clock;

void run1(remote_fmu &fmu) {

    auto dt = cse::to_duration(stepSize);

    auto t_start = Clock::now();
    for (int i = 0; i < numFmus; i++) {
        cse::time_point t = cse::time_point();
        auto slave = fmu.instantiate_slave();
        slave->setup(t, {}, {});
        slave->start_simulation();

        for (int j = 0; j < numSteps; j++) {
            slave->do_step(t, dt);
            t += dt;
        }

    }
    auto t_stop = Clock::now();
    auto elapsed = std::chrono::duration<float>(t_stop - t_start).count();
    std::cout << "[serial] elapsed=" << elapsed << "s" << std::endl;

}

void run2(remote_fmu &fmu) {

    auto execution = cse::execution(cse::time_point(),
                                    std::make_unique<cse::fixed_step_algorithm>(cse::to_duration(stepSize)));

    auto t_start = Clock::now();
    for (int i = 0; i < numFmus; i++) {
        auto slave = fmu.instantiate_slave();
        execution.add_slave(cse::make_pseudo_async(slave), std::string("slave") + std::to_string(i));
    }

    execution.step({});
    for (int i = 0; i < numSteps - 1; i++) {
        execution.step({});
    }

    auto t_stop = Clock::now();
    auto elapsed = std::chrono::duration<float>(t_stop - t_start).count();
    std::cout << "[fibers] elapsed=" << elapsed << "s" << std::endl;

}

void thread_run(const std::shared_ptr<cse::slave> &slave, cse::time_point t, cse::duration dt) {
    slave->do_step(t, dt);
}

void run3(remote_fmu *fmu) {

    cse::time_point t = cse::time_point();
    cse::duration dt = cse::to_duration(stepSize);
    std::shared_ptr<cse::slave> slaves[numFmus];

    auto t_start = Clock::now();
    for (auto &i : slaves) {
        auto slave = fmu->instantiate_slave();
        slave->setup(t, {}, {});
        slave->start_simulation();
        i = slave;
    }

    for (int k = 0; k < numSteps; k++) {
        std::thread threads[numFmus];
        for (int j = 0; j < numFmus; j++) {
            threads[j] = std::thread(thread_run, slaves[j], t, dt);
        }
        t += dt;

        for (auto &thread : threads) {
            thread.join();
        }

    }

    for (auto &slave : slaves) {
        slave->end_simulation();
    }

    auto t_stop = Clock::now();
    auto elapsed = std::chrono::duration<float>(t_stop - t_start).count();
    std::cout << "[threads] elapsed=" << elapsed << "s" << std::endl;

}

int main(int argc, char** argv) {

    if (argc != 4) {
        std::cerr << "Missing one or more program arguments: [fmuId:string, host:string, port:int]" << std::endl;
        return 1;
    }

    auto fmuId = argv[1];
    auto host = argv[2];
    auto port = std::stoi(argv[3]);
    remote_fmu fmu1(fmuId, host, port, false);

    run1(fmu1);
    run2(fmu1);

    remote_fmu *fmu2 = new remote_fmu(fmuId, host, port, true);
    run3(fmu2);
    delete fmu2;

    return 0;

}