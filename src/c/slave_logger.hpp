//
// Created by STENBRO on 9/25/2018.
//

#ifndef CSECORE_SLAVE_LOGGER_HPP
#    define CSECORE_SLAVE_LOGGER_HPP

#endif //CSECORE_SLAVE_LOGGER_HPP

#include <cse.h>
#include <cse/slave.hpp>
#include <fstream>

namespace cse
{

class single_slave_logger
{
public:
    single_slave_logger(char* logPath, int binary);
    ~single_slave_logger();

    int write_int_samples(int* values, int nSamples);
    int write_real_samples(double* values, int nSamples);

private:
    int write_csv_ints(int* values, int nSamples);
    int write_csv_reals(double* values, int nSamples);
    int write_binary_ints(int* values);
    int write_binary_reals(double* values);

    const char* logPath_;
    int binary_;
    std::ofstream fsw_;
};

} // namespace cse