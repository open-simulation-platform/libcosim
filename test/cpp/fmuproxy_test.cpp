
#include <iostream>
#include <string>
#include <ctime>
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

const std::string fmuId = "{06c2700b-b39c-4895-9151-304ddde28443}";

void run1(remote_fmu &fmu) {
    cse::time_point t = cse::time_point();
    auto dt = cse::to_duration(stepSize);

    clock_t begin = clock();
    for (int i = 0; i < numFmus; i++) {
        auto slave = fmu.instantiate_slave();
        slave->setup(t, {}, {});
        slave->start_simulation();

        for (int j = 0; j < numSteps; j++) {
            slave->do_step(t, dt);
            t += dt;
        }
        slave->end_simulation();
    }
    clock_t end = clock();

    double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
    std::cout << "elapsed=" << elapsed_secs << "s" << std::endl;

}

void run2(remote_fmu &fmu) {

    auto execution = cse::execution(cse::time_point(),
                                    std::make_unique<cse::fixed_step_algorithm>(cse::to_duration(stepSize)));

    clock_t begin = clock();
    for (int i = 0; i < numFmus; i++) {
        auto slave = fmu.instantiate_slave();
        execution.add_slave(cse::make_pseudo_async(slave), std::string("slave") + std::to_string(i));
    }


    execution.step({});
    for (int i = 0; i < numSteps - 1; i++) {
        execution.step({});
    }

    execution.stop_simulation();
    clock_t end = clock();

    double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
    std::cout << "elapsed=" << elapsed_secs << "s" << std::endl;

}


void thread_run(std::shared_ptr<cse::slave> slave, cse::time_point t, cse::duration dt) {
    slave->do_step(t, dt);
}

void run3(remote_fmu *fmu) {

    cse::time_point t = cse::time_point();
    cse::duration dt = cse::to_duration(stepSize);
    std::shared_ptr<cse::slave> slaves[numFmus];

    clock_t begin = clock();
    for (int i = 0; i < numFmus; i++) {
        auto slave = fmu->instantiate_slave();
        slave->setup(t, {}, {});
        slave->start_simulation();
        slaves[i] = slave;
    }

    for (int k = 0; k < numSteps; k++) {
        std::thread threads[numFmus];
        for (int j = 0; j < numFmus; j++) {
            threads[j] = std::thread(thread_run, slaves[j], t, dt);
        }
        t += dt;

        for (int l = 0; l < numFmus; l++) {
            threads[l].join();
        }

    }

    for (int p = 0; p < numFmus; p++) {
        slaves[p]->end_simulation();
    }
    clock_t end = clock();

    double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
    std::cout << "elapsed=" << elapsed_secs << "s" << std::endl;

}


int main() {

    remote_fmu fmu1(fmuId, "localhost", 9090, false);
    remote_fmu *fmu2 = new remote_fmu(fmuId, "localhost", 9090, true);
    run1(fmu1);
    run2(fmu1);
    run3(fmu2);

    delete fmu2;

    return 0;

}