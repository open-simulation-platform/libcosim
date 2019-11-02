#include "cse/config_loader.hpp"

#include <utility>

namespace cse
{

config_loader::config_loader()
    : modelResolver_(cse::default_model_uri_resolver())
{}

void config_loader::override_start_time(cse::time_point timePoint)
{
    overrideStartTime_ = timePoint;
}

void config_loader::override_algorithm(std::shared_ptr<cse::algorithm> algorithm)
{
    overrideAlgorithm_ = std::move(algorithm);
}

void config_loader::set_custom_model_uri_resolver(std::shared_ptr<cse::model_uri_resolver> modelResolver)
{
    if (modelResolver != nullptr) modelResolver_ = std::move(modelResolver);
}

} // namespace cse
