//
// Created by LEKJA on 20/09/2018.
//

#ifndef CSECORE_SLAVE_OBSERVER_H
#define CSECORE_SLAVE_OBSERVER_H

#include <map>
#include <mutex>
#include <vector>
#include <memory>

#include <cse.h>
#include <cse/slave.hpp>

namespace cse
{

class single_slave_observer
{
public:
    single_slave_observer(std::shared_ptr<cse::slave> slave);

    void observe(long timeStep);

    void get_real(const cse_variable_index variables[], size_t nv, double values[]);

    void get_int(const cse_variable_index variables[], size_t nv, int values[]);

    size_t get_real_samples(cse_variable_index variableIndex, long fromStep, size_t nSamples, double values[], long steps[]);

    size_t get_int_samples(cse_variable_index variableIndex, long fromStep, size_t nSamples, int values[], long steps[]);

private:
    std::map<long, std::vector<double>> realSamples_;
    std::map<long, std::vector<int>> intSamples_;
    std::vector<cse::variable_index> realIndexes_;
    std::vector<cse::variable_index> intIndexes_;
    std::shared_ptr<cse::slave> slave_;
    std::mutex lock_;

    template<typename T>
    void get(const cse_variable_index variables[], std::vector<cse::variable_index> indices, std::map<long, std::vector<T>> samples, size_t nv, T values[]);

    template<typename T>
    size_t get_samples(cse_variable_index variableIndex, std::vector<cse::variable_index> indices, std::map<long, std::vector<T>> samples, long fromStep, size_t nSamples, T values[], long steps[]);
};
} // namespace cse

#endif //CSECORE_SLAVE_OBSERVER_H
