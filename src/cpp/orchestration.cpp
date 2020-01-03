#include "cse/orchestration.hpp"

#include "cse/error.hpp"
#include "cse/fmi/fmu.hpp"
#include "cse/log/logger.hpp"

#ifdef HAS_FMUPROXY
#    include <cse/fmuproxy/fmuproxy_uri_sub_resolver.hpp>
#endif

namespace cse
{

// =============================================================================
// class model_uri_sub_resolver
// =============================================================================

std::shared_ptr<model> model_uri_sub_resolver::lookup_model(
    const uri& baseUri,
    const uri& modelUriReference)
{
    return lookup_model(resolve_reference(baseUri, modelUriReference));
}


// =============================================================================
// class model_uri_resolver
// =============================================================================

// Defaulted constructor, move and destructor.
model_uri_resolver::model_uri_resolver() noexcept = default;
model_uri_resolver::model_uri_resolver(model_uri_resolver&&) noexcept = default;
model_uri_resolver& model_uri_resolver::operator=(model_uri_resolver&&) noexcept = default;
model_uri_resolver::~model_uri_resolver() noexcept = default;


void model_uri_resolver::add_sub_resolver(std::shared_ptr<model_uri_sub_resolver> sr)
{
    subResolvers_.push_back(sr);
}


std::shared_ptr<model> model_uri_resolver::lookup_model(
    const uri& baseUri,
    const uri& modelUriReference)
{
    CSE_INPUT_CHECK(baseUri.scheme().has_value() ||
        modelUriReference.scheme().has_value());
    for (auto sr : subResolvers_) {
        if (auto r = sr->lookup_model(baseUri, modelUriReference)) return r;
    }
    throw std::runtime_error(
        "No resolvers available to handle URI: " + std::string(modelUriReference.view()));
}


std::shared_ptr<model> model_uri_resolver::lookup_model(const uri& modelUri)
{
    CSE_INPUT_CHECK(modelUri.scheme().has_value());
    for (auto sr : subResolvers_) {
        if (auto r = sr->lookup_model(modelUri)) return r;
    }
    throw std::runtime_error(
        "No resolvers available to handle URI: " + std::string(modelUri.view()));
}


// =============================================================================
// class fmu_file_uri_sub_resolver
// =============================================================================

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


fmu_file_uri_sub_resolver::fmu_file_uri_sub_resolver()
    : importer_(fmi::importer::create())
{
}


fmu_file_uri_sub_resolver::fmu_file_uri_sub_resolver(
    const boost::filesystem::path& cacheDir)
    : importer_(fmi::importer::create(cacheDir))
{
}


std::shared_ptr<model> fmu_file_uri_sub_resolver::lookup_model(const uri& modelUri)
{
    assert(modelUri.scheme().has_value());
    if (*modelUri.scheme() != "file") return nullptr;
    if (modelUri.authority().has_value() &&
        !(modelUri.authority()->empty() || *modelUri.authority() == "localhost")) {
        return nullptr;
    }
    if (modelUri.query().has_value() || modelUri.fragment().has_value()) {
        BOOST_LOG_SEV(log::logger(), log::warning)
            << "Query and/or fragment component(s) in a file:// URI were ignored: "
            << modelUri;
    }
    const auto path = file_uri_to_path(modelUri);
    if (path.extension() != ".fmu") return nullptr;
    auto fmu = importer_->import(path);
    return std::make_shared<fmu_model>(fmu);
}


// =============================================================================
// misc
// =============================================================================

std::shared_ptr<model_uri_resolver> default_model_uri_resolver(
    const boost::filesystem::path* cacheDir)
{
    auto resolver = std::make_shared<model_uri_resolver>();
    if (cacheDir) {
        resolver->add_sub_resolver(
            std::make_shared<fmu_file_uri_sub_resolver>(*cacheDir / "fmus"));
    } else {
        resolver->add_sub_resolver(
            std::make_shared<fmu_file_uri_sub_resolver>());
    }
#ifdef HAS_FMUPROXY
    resolver->add_sub_resolver(
        std::make_shared<fmuproxy::fmuproxy_uri_sub_resolver>());
#endif
    return resolver;
}

} // namespace cse
