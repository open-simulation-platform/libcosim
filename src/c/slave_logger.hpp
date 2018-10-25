//
// Created by STENBRO on 9/25/2018.
//

#ifndef CSECORE_SLAVE_LOGGER_HPP
#    define CSECORE_SLAVE_LOGGER_HPP

#endif //CSECORE_SLAVE_LOGGER_HPP

#include <cse.h>
#include <cse/slave.hpp>

#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <fstream>

namespace cse
{

class single_slave_logger
{
public:
    single_slave_logger(char* logPath);

    int write_int_samples(int* values, int nSamples, int binary);
    int write_real_samples(double* values, int nSamples, int binary);
    int memory_mapped_binary_write(double* values);

private:
    int write_csv_ints(int* values, int nSamples);
    int write_csv_reals(double* values, int nSamples);
    int write_binary_ints(int* values);
    int write_binary_reals(double* values);

    cse_observer* observer_;
    cse_slave_index slave_index_;
    cse_variable_index variable_index_;
    const char* logPath_;
};

} // namespace cse