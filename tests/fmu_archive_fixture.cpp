#include <cosim/fs_portability.hpp>

#include <boost/test/unit_test.hpp>
#include <zip.h>

#include <string>

namespace cosim::test
{
void write_archive(const filesystem::path& path, const std::string& xml)
{
    int zipError = 0;
    auto* archive = zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipError);
    BOOST_REQUIRE(archive != nullptr);
    auto* source = zip_source_buffer(archive, xml.data(), xml.size(), 0);
    if (!source) {
        zip_discard(archive);
        BOOST_FAIL("Could not create ZIP source for importer test");
    }
    if (zip_file_add(archive, "modelDescription.xml", source, ZIP_FL_ENC_UTF_8) < 0) {
        zip_source_free(source);
        zip_discard(archive);
        BOOST_FAIL("Could not add model description to importer test ZIP");
    }
    if (zip_close(archive) != 0) {
        zip_discard(archive);
        BOOST_FAIL("Could not close importer test ZIP");
    }
}
} // namespace cosim::test
