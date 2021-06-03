#define BOOST_TEST_MODULE cosim::utility::zip unittest
#include <cosim/utility/filesystem.hpp>
#include <cosim/utility/zip.hpp>

#include <cosim/fs_portability.hpp>
#include <boost/test/unit_test.hpp>

#include <cstdlib>
#include <system_error>


BOOST_AUTO_TEST_CASE(zip_archive)
{
    namespace ut = cosim::utility;
    namespace uz = cosim::utility::zip;
    namespace fs = cosim::filesystem;

    // Info about the test archive file and its contents
    const std::uint64_t archiveEntryCount = 3;
    const std::string dirFilename = "images/";
    const std::string binFilename = "smiley.png";
    const std::string txtFilename = "a text file.txt";
    const std::string dirName = dirFilename;
    const std::string binName = dirFilename + binFilename;
    const std::string txtName = txtFilename;
    const std::uint64_t binSize = 16489;
    const std::uint64_t txtSize = 13;

    // Test setup
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(!!testDataDir);
    const auto archivePath = fs::path(testDataDir) / "ziptest.zip";

    // Open archive
    auto archive = uz::archive(archivePath);
    BOOST_TEST_REQUIRE(archive.is_open());

    // Get entry info
    BOOST_TEST(archive.entry_count() == archiveEntryCount);
    const auto dirIndex = archive.find_entry(dirName);
    const auto binIndex = archive.find_entry(binName);
    const auto txtIndex = archive.find_entry(txtName);
    const auto invIndex = archive.find_entry("no such entry");
    BOOST_TEST_REQUIRE(dirIndex != uz::invalid_entry_index);
    BOOST_TEST_REQUIRE(binIndex != uz::invalid_entry_index);
    BOOST_TEST_REQUIRE(txtIndex != uz::invalid_entry_index);
    BOOST_TEST(invIndex == uz::invalid_entry_index);
    BOOST_TEST(binIndex != dirIndex);
    BOOST_TEST(txtIndex != dirIndex);
    BOOST_TEST(txtIndex != binIndex);
    BOOST_TEST(archive.entry_name(dirIndex) == dirName);
    BOOST_TEST(archive.entry_name(binIndex) == binName);
    BOOST_TEST(archive.entry_name(txtIndex) == txtName);
    BOOST_CHECK_THROW(archive.entry_name(invIndex), uz::error);
    BOOST_TEST(archive.is_dir_entry(dirIndex));
    BOOST_TEST(!archive.is_dir_entry(binIndex));
    BOOST_TEST(!archive.is_dir_entry(txtIndex));
    BOOST_CHECK_THROW(archive.is_dir_entry(invIndex), uz::error);

    // Extract entire archive
    {
        ut::temp_dir tempDir;
        archive.extract_all(tempDir.path());
        const auto dirExtracted = tempDir.path() / dirName;
        const auto binExtracted = tempDir.path() / binName;
        const auto txtExtracted = tempDir.path() / txtName;
        BOOST_TEST_REQUIRE(fs::exists(dirExtracted));
        BOOST_TEST_REQUIRE(fs::exists(binExtracted));
        BOOST_TEST_REQUIRE(fs::exists(txtExtracted));
        BOOST_TEST(fs::is_directory(dirExtracted));
        BOOST_TEST(fs::is_regular_file(binExtracted));
        BOOST_TEST(fs::is_regular_file(txtExtracted));
        BOOST_TEST(fs::file_size(binExtracted) == binSize);
        BOOST_TEST(fs::file_size(txtExtracted) == txtSize);
        BOOST_CHECK_THROW(archive.extract_file_to(binIndex, tempDir.path() / "nonexistent"), std::system_error);
    }

    // Extract individual entries
    {
        ut::temp_dir tempDir;
        const auto binExtracted = archive.extract_file_to(binIndex, tempDir.path());
        const auto txtExtracted = archive.extract_file_to(txtIndex, tempDir.path());
        BOOST_TEST(binExtracted.compare(tempDir.path() / binFilename) == 0);
        BOOST_TEST(txtExtracted.compare(tempDir.path() / txtFilename) == 0);
        BOOST_TEST(fs::file_size(binExtracted) == binSize);
        BOOST_TEST(fs::file_size(txtExtracted) == txtSize);
        BOOST_CHECK_THROW(archive.extract_file_to(invIndex, tempDir.path()), uz::error);
        BOOST_CHECK_THROW(archive.extract_file_to(binIndex, tempDir.path() / "nonexistent"), std::system_error);
    }

    archive.discard();
    BOOST_TEST(!archive.is_open());
    BOOST_CHECK_NO_THROW(archive.discard());
}
