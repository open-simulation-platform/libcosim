//
// Created by STENBRO on 9/26/2018.
//

#include "slave_logger.hpp"

namespace cse
{

single_slave_logger::single_slave_logger(char* logPath, int binary)
    : logPath_(logPath)
    , binary_(binary)
{
    if (binary == 1) {
        fsw_ = std::ofstream(logPath_, std::ios_base::out | std::ios_base::binary);
    } else {
        fsw_ = std::ofstream(logPath_, std::ios_base::out | std::ios_base::app);
    }

    if (fsw_.fail()) {
        throw std::runtime_error("Failed to open file stream for logging");
    }
}

single_slave_logger::~single_slave_logger()
{
    if (fsw_.is_open()) {
        fsw_.close();
    }
}

int single_slave_logger::write_int_samples(int* values, int nSamples)
{
    int rc = 0;

    if (binary_ == 1) {
        rc = write_binary_ints(values);
    } else {
        rc = write_csv_ints(values, nSamples);
    }

    return rc;
}

int single_slave_logger::write_real_samples(double* values, int nSamples)
{
    int rc = 0;

    if (binary_ == 1) {
        rc = write_binary_reals(values);
    } else {
        rc = write_csv_reals(values, nSamples);
    }

    return rc;
}

int single_slave_logger::write_csv_ints(int* values, int nSamples)
{

    if (fsw_.is_open()) {
        for (int i = 0; i <= nSamples - 1; i++) {
            fsw_ << values[i] << ",";
        }
    }

    return 1;
}

int single_slave_logger::write_csv_reals(double* values, int nSamples)
{

    if (fsw_.is_open()) {
        for (int i = 0; i <= nSamples - 1; i++) {
            fsw_ << values[i] << ",";
        }
    }

    return 1;
}

int single_slave_logger::write_binary_ints(int* values)
{

    if (fsw_.is_open()) {
        fsw_.write((char*)&values, sizeof(values));
    }

    return 1;
}

int single_slave_logger::write_binary_reals(double* values)
{

    if (fsw_.is_open()) {
        fsw_.write((char*)&values, sizeof(values));
    }

    return 1;
}

} // namespace cse