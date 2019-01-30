
#include <iostream>
#include <string>
#include <cse/fmuproxy/remote_fmu.hpp>

using namespace cse;
using namespace cse::fmuproxy;

int main() {

    const std::string fmuId = "{06c2700b-b39c-4895-9151-304ddde28443}";

    remote_fmu fmu(fmuId, "localhost", 9090);
    auto md = fmu.model_description();

    std::cout << "Model name=" << md->name << std::endl;

    auto slave = fmu.instantiate_slave();

    return 0;

}