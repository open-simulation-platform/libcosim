#define BOOST_TEST_MODULE cosim file cache unittests
#include <cosim/file_cache.hpp>
#include <cosim/fmi/fmu.hpp>
#include <cosim/fmi/importer.hpp>

#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <future>
#include <stdexcept>


BOOST_AUTO_TEST_CASE(temporary_cache_retains_all_readers)
{
    cosim::temporary_file_cache cache;
    auto writer = cache.get_directory_rw("entry");
    const auto path = writer->path();
    writer.reset();

    auto first = cache.get_directory_ro("entry");
    auto second = cache.get_directory_ro("entry");
    BOOST_TEST(first->path() == path);
    BOOST_TEST(second->path() == path);
    second.reset();
    BOOST_CHECK_THROW(cache.get_directory_rw("entry"), std::logic_error);
    first.reset();
    BOOST_CHECK_NO_THROW(cache.get_directory_rw("entry"));
}


BOOST_AUTO_TEST_CASE(temporary_cache_readers_release_in_any_order)
{
    cosim::temporary_file_cache cache;
    cache.get_directory_rw("entry");
    auto first = cache.get_directory_ro("entry");
    auto second = cache.get_directory_ro("entry");
    first.reset();
    BOOST_CHECK_THROW(cache.get_directory_rw("entry"), std::logic_error);
    second.reset();
    BOOST_CHECK_NO_THROW(cache.get_directory_rw("entry"));
}


BOOST_AUTO_TEST_CASE(importer_rejects_concurrent_imports)
{
    class blocking_cache final : public cosim::file_cache
    {
    public:
        std::promise<void> entered;
        std::promise<void> resume;
        std::future<void> resumed = resume.get_future();
        std::function<void()> onResume;

        std::unique_ptr<directory_rw> get_directory_rw(std::string_view key) override
        {
            entered.set_value();
            resumed.wait();
            onResume();
            return cache.get_directory_rw(key);
        }

        std::unique_ptr<directory_ro> get_directory_ro(std::string_view key) override
        {
            return cache.get_directory_ro(key);
        }

    private:
        cosim::temporary_file_cache cache;
    };

    const auto* referenceDir = std::getenv("REFERENCE_FMU_DIR");
    BOOST_REQUIRE(referenceDir);
    const auto path = cosim::filesystem::path(referenceDir) / "Feedthrough.fmu";
    auto cache = std::make_shared<blocking_cache>();
    auto entered = cache->entered.get_future();
    auto importer = cosim::fmi::importer::create(cache);
    bool reentrantRejected = false;
    cache->onResume = [&] {
        try {
            importer->import(path);
        } catch (const std::logic_error&) {
            reentrantRejected = true;
        }
    };
    auto pending = std::async(std::launch::async, [importer, path] {
        return importer->import(path);
    });
    const auto status = entered.wait_for(std::chrono::seconds(10));
    if (status != std::future_status::ready) {
        cache->resume.set_value();
        BOOST_FAIL("The import did not reach the cache");
    }
    BOOST_CHECK_THROW(importer->import(path), std::logic_error);
    BOOST_CHECK_THROW(importer->import_unpacked(path), std::logic_error);
    cache->resume.set_value();
    const auto fmu = pending.get();
    BOOST_REQUIRE(fmu);
    BOOST_CHECK(reentrantRejected);
    BOOST_TEST(fmu->model_description()->name == "Feedthrough");
    BOOST_CHECK_THROW(importer->import_unpacked(path / "missing"), std::exception);
    BOOST_CHECK(importer->import(path) == fmu);
}
