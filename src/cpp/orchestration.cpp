#include "cse/orchestration.hpp"

#include "cse/fmi/fmu.hpp"

namespace cse
{
namespace
{
class fmu_model : public model
{
public:
    fmu_model(std::shared_ptr<fmi::fmu> fmu)
        : fmu_(fmu)
    {}

    std::shared_ptr<const model_description> description() const noexcept
    {
        return fmu_->model_description();
    }

    std::shared_ptr<async_slave> instantiate(std::string_view name) override
    {
        return make_background_thread_slave(fmu_->instantiate_slave(name));
    }

private:
    std::shared_ptr<fmi::fmu> fmu_;
};
} // namespace


file_uri_sub_resolver::file_uri_sub_resolver()
    : importer_(fmi::importer::create())
{
}


std::shared_ptr<model> file_uri_sub_resolver::lookup_model(std::string_view uri)
{
    if (uri.substr(0, 8) != "file:///") return nullptr;
    #ifdef _WIN32
        const auto path = uri.substr(8);
    #else
        const auto path = uri.substr(7);
    #endif
    auto fmu = importer_->import(boost::filesystem::path(std::string(path)));

    return std::make_shared<fmu_model>(fmu);
}


std::shared_ptr<model_uri_resolver> default_model_uri_resolver()
{
    auto resolver = std::make_shared<model_uri_resolver>();
    resolver->add_sub_resolver(std::make_shared<file_uri_sub_resolver>());
    return resolver;
}

} // namespace cse
