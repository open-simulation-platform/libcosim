//
// Created by STENBRO on 10/30/2018.
//

#include <cse/error.hpp>
#include <cse/observer.hpp>
#include <map>
#include <mutex>

namespace cse
{

class file_observer::file_observer
{
public:
    file_observer::file_observer(char* logPath, bool binary)
        : logPath_(logPath)
        , binary_(binary)
    {

        if (binary) {
            fsw_ = std::ofstream(logPath_, std::ios_base::out | std::ios_base::binary);
        } else {
            fsw_ = std::ofstream(logPath_, std::ios_base::out | std::ios_base::app);
        }

        if (fsw_.fail()) {
            throw std::runtime_error("Failed to open file stream for logging");
        }
    }

private:
};


} // namespace cse
