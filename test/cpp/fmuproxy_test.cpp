
#include <iostream>
#include <string>
#include <cse/fmuproxy/remote_fmu.hpp>

using namespace cse;
using namespace cse::fmuproxy;

int main() {

    const double dt = 1.0/100;
    cse::time_point t = cse::to_time_point(0);
    const std::string fmuId = "{06c2700b-b39c-4895-9151-304ddde28443}";

    remote_fmu fmu(fmuId, "localhost", 9090);
    auto md = fmu.model_description();

    std::cout << "modelName=" << md->name << std::endl;

    auto slave = fmu.instantiate_slave();
    slave->setup(t, {}, {});
    slave->start_simulation();

    slave->do_step(t, cse::to_duration(dt));

    slave->end_simulation();

    return 0;

}