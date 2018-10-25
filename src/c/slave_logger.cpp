//
// Created by STENBRO on 9/26/2018.
//

#include "slave_logger.hpp"

namespace cse
{

single_slave_logger::single_slave_logger(char* logPath)
    : logPath_(logPath)
{
}

int single_slave_logger::write_int_samples(int* values, int nSamples, int binary)
{
    int rc = 0;

    if (binary == 1) {
        rc = write_binary_ints(values);
    } else {
        rc = write_csv_ints(values, nSamples);
    }

    return rc;
}

int single_slave_logger::write_real_samples(double* values, int nSamples, int binary)
{
    int rc = 0;

    if (binary == 1) {
        rc = write_binary_reals(values);
    } else {
        rc = write_csv_reals(values, nSamples);
    }

    return rc;
}


int single_slave_logger::write_csv_ints(int* values, int nSamples)
{
    std::ofstream fsw(logPath_, std::ios_base::out | std::ios_base::app);

    if (fsw.fail()) {
        return -1;
    }

    for (int i = 0; i <= nSamples - 1; i++) {
        fsw << values[i] << ",";
    }

    fsw << "\n";

    return 1;
}

int single_slave_logger::write_csv_reals(double* values, int nSamples)
{
    std::ofstream fsw(logPath_, std::ios_base::out | std::ios_base::app);

    if (fsw.fail()) {
        return -1;
    }

    for (int i = 0; i <= nSamples - 1; i++) {
        fsw << values[i] << ",";
    }

    fsw << "\n";

    return 1;
}

int single_slave_logger::write_binary_ints(int* values)
{
    std::ofstream fsw(logPath_, std::ios_base::out | std::ios_base::app | std::ios_base::binary);

    if (fsw.fail()) {
        return -1;
    }

    fsw.write((char*)&values, sizeof(values));

    return 1;
}

int single_slave_logger::write_binary_reals(double* values)
{
    std::ofstream fsw(logPath_, std::ios_base::out | std::ios_base::app | std::ios_base::binary);

    if (fsw.fail()) {
        return -1;
    }

    fsw.write((char*)&values, sizeof(values));

    return 1;
}

int single_slave_logger::memory_mapped_binary_write(double* values)
{
    boost::interprocess::file_mapping::remove(logPath_);

    std::filebuf buf;
    buf.open(logPath_, std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);
    buf.pubseekoff(sizeof(values), std::ios_base::beg);
    buf.sputc(0);

    boost::interprocess::file_mapping mapping(logPath_, boost::interprocess::read_write);
    boost::interprocess::mapped_region region(mapping, boost::interprocess::read_write);

    void* addr = region.get_address();

    memcpy(addr, &values, sizeof(values));

    region.flush();

    return 1;
}

} // namespace cse