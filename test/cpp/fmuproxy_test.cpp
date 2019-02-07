
#include <iostream>
#include <string>
#include <ctime>

#ifdef _WIN32
#include <thread>
#endif

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
    std::vector<std::shared_ptr<cse::slave>> slaves;
    clock_t begin = clock();
    for (int i = 0; i < numFmus; i++) {
        auto slave = fmu.instantiate_slave();
        slaves.emplace_back(slave);
        execution.add_slave(cse::make_pseudo_async(slave), std::string("slave") + std::to_string(i));
    }


    execution.step({});
    for (int i = 0; i < numSteps - 1; i++) {
        execution.step({});
    }

    for (int i = 0; i < numFmus; i++) {
        slaves[i]->end_simulation();
    }
    clock_t end = clock();

    double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
    std::cout << "elapsed=" << elapsed_secs << "s" << std::endl;

}

#ifdef _WIN32
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
#endif

int main(int argc, char** argv) {

    if (argc != 4) {
        std::cerr << "Missing one or more program arguments: [fmuId:string, host:string, port:int]" << std::endl;
        return 1;
    }

    auto fmuId = argv[1];
    auto host = argv[2];
    auto port = std::stoi(argv[3]);
    remote_fmu fmu1(fmuId, host, port, false);

//    run1(fmu1);
    run2(fmu1);

#ifdef _WIN32
    remote_fmu *fmu2 = new remote_fmu(fmuId, host, port, true);
    run3(fmu2);
    delete fmu2;
#endif

    return 0;

}