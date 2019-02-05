
#include <iostream>
#include <string>
#include <vector>
#include <ctime>
#include <cse/fmuproxy/remote_fmu.hpp>
#include <cse/execution.hpp>

#include <cse/algorithm.hpp>

using namespace cse;
using namespace cse::fmuproxy;


const double stop = 100;
const double stepSize = 1.0/100;
const int numSteps = (int) (stop/stepSize);

const std::string fmuId = "{9c347d26-2562-424a-9731-e44a4c2de5bb}";

void run1(remote_fmu &fmu) {

    auto slave = fmu.instantiate_slave();
    slave->setup(cse::time_point(), {}, {});
    slave->start_simulation();

    clock_t begin = clock();
    for (int i = 0; i < numSteps; i++) {
        slave->do_step(cse::time_point(), cse::to_duration(stepSize));
    }
    clock_t end = clock();

    double elapsed_secs = double(end-begin) / CLOCKS_PER_SEC;
    std::cout << "elapsed=" << elapsed_secs << "s" << std::endl;

    slave->end_simulation();
}

void run2(remote_fmu &fmu) {

    auto slave = fmu.instantiate_slave();
    auto execution =  cse::execution(cse::time_point(), std::make_unique<cse::fixed_step_algorithm>(cse::to_duration(stepSize)));

    execution.add_slave(cse::make_pseudo_async(slave), "slave1");
    execution.step({});

    clock_t begin = clock();
    for (int i = 0; i < numSteps-1; i++) {
        execution.step({});
    }
    clock_t end = clock();

    double elapsed_secs = double(end-begin) / CLOCKS_PER_SEC;
    std::cout << "elapsed=" << elapsed_secs << "s" << std::endl;

    slave->end_simulation();

}

int main() {


    remote_fmu fmu(fmuId, "localhost", 9090);
    run1(fmu);
    run2(fmu);

    return 0;

}