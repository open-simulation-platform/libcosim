
#include <cosim/fs_portability.hpp>
#include <cosim/utility/filesystem.hpp>
#include <cosim/utility/zip.hpp>

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include <cstdlib>
#include <system_error>


TEST_CASE("zip_archive")
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
    REQUIRE(!!testDataDir);
    const auto archivePath = fs::path(testDataDir) / "ziptest.zip";

    // Open archive
    auto archive = uz::archive(archivePath);
    REQUIRE(archive.is_open());

    // Get entry info
    CHECK(archive.entry_count() == archiveEntryCount);
    const auto dirIndex = archive.find_entry(dirName);
    const auto binIndex = archive.find_entry(binName);
    const auto txtIndex = archive.find_entry(txtName);
    const auto invIndex = archive.find_entry("no such entry");
    REQUIRE(dirIndex != uz::invalid_entry_index);
    REQUIRE(binIndex != uz::invalid_entry_index);
    REQUIRE(txtIndex != uz::invalid_entry_index);
    CHECK(invIndex == uz::invalid_entry_index);
    CHECK(binIndex != dirIndex);
    CHECK(txtIndex != dirIndex);
    CHECK(txtIndex != binIndex);
    CHECK(archive.entry_name(dirIndex) == dirName);
    CHECK(archive.entry_name(binIndex) == binName);
    CHECK(archive.entry_name(txtIndex) == txtName);
    CHECK_THROWS_AS(archive.entry_name(invIndex), uz::error);
    CHECK(archive.is_dir_entry(dirIndex));
    CHECK(!archive.is_dir_entry(binIndex));
    CHECK(!archive.is_dir_entry(txtIndex));
    CHECK_THROWS_AS(archive.is_dir_entry(invIndex), uz::error);

    // Extract entire archive
    {
        ut::temp_dir tempDir;
        archive.extract_all(tempDir.path());
        const auto dirExtracted = tempDir.path() / dirName;
        const auto binExtracted = tempDir.path() / binName;
        const auto txtExtracted = tempDir.path() / txtName;
        REQUIRE(fs::exists(dirExtracted));
        REQUIRE(fs::exists(binExtracted));
        REQUIRE(fs::exists(txtExtracted));
        CHECK(fs::is_directory(dirExtracted));
        CHECK(fs::is_regular_file(binExtracted));
        CHECK(fs::is_regular_file(txtExtracted));
        CHECK(fs::file_size(binExtracted) == binSize);
        CHECK(fs::file_size(txtExtracted) == txtSize);
        CHECK_THROWS_AS(archive.extract_file_to(binIndex, tempDir.path() / "nonexistent"), std::system_error);
    }

    // Extract individual entries
    {
        ut::temp_dir tempDir;
        const auto binExtracted = archive.extract_file_to(binIndex, tempDir.path());
        const auto txtExtracted = archive.extract_file_to(txtIndex, tempDir.path());
        CHECK(binExtracted.compare(tempDir.path() / binFilename) == 0);
        CHECK(txtExtracted.compare(tempDir.path() / txtFilename) == 0);
        CHECK(fs::file_size(binExtracted) == binSize);
        CHECK(fs::file_size(txtExtracted) == txtSize);
        CHECK_THROWS_AS(archive.extract_file_to(invIndex, tempDir.path()), uz::error);
        CHECK_THROWS_AS(archive.extract_file_to(binIndex, tempDir.path() / "nonexistent"), std::system_error);
    }

    archive.discard();
    CHECK(!archive.is_open());
    CHECK_NOTHROW(archive.discard());
}
