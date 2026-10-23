// Metal 4 pipeline archive file: path, manifest check, install; no Metal
// device involved.

#include "erhe_graphics/metal4/metal4_pipeline_archive_file.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

using erhe::graphics::check_metal4_pipeline_archive;
using erhe::graphics::get_metal4_pipeline_archive_manifest_path;
using erhe::graphics::install_metal4_pipeline_archive;
using erhe::graphics::make_metal4_pipeline_archive_manifest;
using erhe::graphics::make_metal4_pipeline_archive_path;
using erhe::graphics::remove_stale_metal4_pipeline_archive_temporaries;

void write_text(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void write_bytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

auto make_bytes(const std::size_t size, const uint8_t seed) -> std::vector<uint8_t>
{
    std::vector<uint8_t> bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<uint8_t>((i * 31u) + seed);
    }
    return bytes;
}

class Metal4_pipeline_archive_file_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_directory = std::filesystem::temp_directory_path() / ("erhe_metal4_archive_test_" + std::to_string(static_cast<long>(getpid())));
        std::filesystem::remove_all(m_directory);
        std::filesystem::create_directories(m_directory);
        m_archive_path = make_metal4_pipeline_archive_path(m_directory, "Apple M4 Pro", "26A434");
    }
    void TearDown() override
    {
        std::filesystem::remove_all(m_directory);
    }
    [[nodiscard]] auto file_count() const -> int
    {
        int count = 0;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{m_directory}) {
            static_cast<void>(entry);
            ++count;
        }
        return count;
    }
    // Writes bytes as a serialized archive would be and installs them.
    void install(const std::vector<uint8_t>& bytes)
    {
        std::filesystem::path temporary = m_archive_path;
        temporary += ".tmp";
        write_bytes(temporary, bytes);
        std::string error;
        ASSERT_TRUE(install_metal4_pipeline_archive(temporary, m_archive_path, error)) << error;
    }

    std::filesystem::path m_directory;
    std::filesystem::path m_archive_path;
};

} // anonymous namespace

TEST_F(Metal4_pipeline_archive_file_test, path_names_gpu_and_os_build)
{
    EXPECT_EQ(m_archive_path.parent_path(), m_directory);
    const std::string name = m_archive_path.filename().string();
    EXPECT_EQ(name.rfind("metal4_", 0), 0u);
    EXPECT_NE(name.find("_26A434.mtl4archive"), std::string::npos);
    EXPECT_NE(make_metal4_pipeline_archive_path(m_directory, "Apple M4 Max", "26A434"), m_archive_path);
    EXPECT_NE(make_metal4_pipeline_archive_path(m_directory, "Apple M4 Pro", "26B100"), m_archive_path);
    EXPECT_EQ(make_metal4_pipeline_archive_path(m_directory, "Apple M4 Pro", "26A434"), m_archive_path) << "stable across calls";
}

TEST_F(Metal4_pipeline_archive_file_test, installed_archive_passes_check)
{
    install(make_bytes(1000, 1));
    std::string reason;
    EXPECT_TRUE(check_metal4_pipeline_archive(m_archive_path, reason)) << reason;
    EXPECT_EQ(file_count(), 2) << "archive and manifest, no temporary files";

    install(make_bytes(500, 2));
    EXPECT_TRUE(check_metal4_pipeline_archive(m_archive_path, reason)) << reason;
    EXPECT_EQ(std::filesystem::file_size(m_archive_path), 500u) << "replaced in place";
    EXPECT_EQ(file_count(), 2);
}

TEST_F(Metal4_pipeline_archive_file_test, check_rejects_absent_truncated_and_foreign_files)
{
    std::string reason;
    EXPECT_FALSE(check_metal4_pipeline_archive(m_archive_path, reason));

    install(make_bytes(1000, 3));
    std::vector<uint8_t> truncated = make_bytes(1000, 3);
    truncated.resize(400);
    write_bytes(m_archive_path, truncated);
    EXPECT_FALSE(check_metal4_pipeline_archive(m_archive_path, reason));

    install(make_bytes(1000, 4));
    std::vector<uint8_t> modified = make_bytes(1000, 4);
    modified[10] ^= 0x01u;
    write_bytes(m_archive_path, modified);
    EXPECT_FALSE(check_metal4_pipeline_archive(m_archive_path, reason)) << "same size, other content";

    // A file erhe did not install (no manifest), e.g. random bytes.
    std::filesystem::remove(get_metal4_pipeline_archive_manifest_path(m_archive_path));
    write_bytes(m_archive_path, make_bytes(4096, 5));
    EXPECT_FALSE(check_metal4_pipeline_archive(m_archive_path, reason));
}

TEST_F(Metal4_pipeline_archive_file_test, empty_gpu_name_and_os_build_give_distinct_file_names)
{
    const std::filesystem::path no_name  = make_metal4_pipeline_archive_path(m_directory, "", "26A434");
    const std::filesystem::path no_build = make_metal4_pipeline_archive_path(m_directory, "Apple M4 Pro", "");
    EXPECT_EQ(no_name.parent_path(), m_directory);
    EXPECT_EQ(no_build.parent_path(), m_directory);
    EXPECT_NE(no_name, m_archive_path);
    EXPECT_NE(no_build, m_archive_path);
    EXPECT_NE(no_name, no_build);
    EXPECT_EQ(no_build.extension().string(), ".mtl4archive");
}

TEST_F(Metal4_pipeline_archive_file_test, install_fails_for_missing_or_empty_temporary)
{
    std::filesystem::path temporary = m_archive_path;
    temporary += ".tmp";
    std::string error;
    EXPECT_FALSE(install_metal4_pipeline_archive(temporary, m_archive_path, error));
    EXPECT_FALSE(error.empty());

    write_bytes(temporary, {});
    error.clear();
    EXPECT_FALSE(install_metal4_pipeline_archive(temporary, m_archive_path, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(std::filesystem::exists(temporary)) << "temporary removed";
    EXPECT_FALSE(std::filesystem::exists(m_archive_path));
    EXPECT_EQ(file_count(), 0);
}

TEST_F(Metal4_pipeline_archive_file_test, check_rejects_bad_manifests)
{
    const std::vector<uint8_t> bytes = make_bytes(1000, 6);
    install(bytes);
    const std::filesystem::path manifest_path = get_metal4_pipeline_archive_manifest_path(m_archive_path);
    const std::string good = make_metal4_pipeline_archive_manifest(bytes);
    std::string reason;

    std::string wrong_header = good;
    wrong_header.replace(0, 4, "ERHE");
    write_text(manifest_path, wrong_header);
    EXPECT_FALSE(check_metal4_pipeline_archive(m_archive_path, reason));

    std::string wrong_version = good;
    wrong_version.replace(wrong_version.find(" 1\n"), 3, " 2\n");
    write_text(manifest_path, wrong_version);
    EXPECT_FALSE(check_metal4_pipeline_archive(m_archive_path, reason));

    write_text(manifest_path, "garbage");
    EXPECT_FALSE(check_metal4_pipeline_archive(m_archive_path, reason));

    write_text(manifest_path, "");
    EXPECT_FALSE(check_metal4_pipeline_archive(m_archive_path, reason));

    write_text(manifest_path, good);
    EXPECT_TRUE(check_metal4_pipeline_archive(m_archive_path, reason)) << reason;
}

TEST_F(Metal4_pipeline_archive_file_test, interrupted_install_is_rejected)
{
    // The archive reached its name, the manifest is still at its temporary
    // name (the old manifest was removed first).
    const std::vector<uint8_t> bytes = make_bytes(1000, 7);
    write_bytes(m_archive_path, bytes);
    std::filesystem::path temporary_manifest = get_metal4_pipeline_archive_manifest_path(m_archive_path);
    temporary_manifest += ".12345.tmp";
    write_text(temporary_manifest, make_metal4_pipeline_archive_manifest(bytes));
    std::string reason;
    EXPECT_FALSE(check_metal4_pipeline_archive(m_archive_path, reason));
}

TEST_F(Metal4_pipeline_archive_file_test, stale_temporaries_of_dead_processes_are_removed)
{
    // 99999999 is above the macOS / Linux pid range: no such process.
    std::filesystem::path dead_archive = m_archive_path;
    dead_archive += ".99999999.tmp";
    std::filesystem::path dead_manifest = get_metal4_pipeline_archive_manifest_path(m_archive_path);
    dead_manifest += ".99999999.tmp";
    std::filesystem::path live_archive = m_archive_path;
    live_archive += "." + std::to_string(static_cast<long>(getpid())) + ".tmp";
    const std::filesystem::path unrelated = m_directory / "other.99999999.tmp";
    write_bytes(dead_archive,  make_bytes(10, 8));
    write_bytes(dead_manifest, make_bytes(10, 9));
    write_bytes(live_archive,  make_bytes(10, 10));
    write_bytes(unrelated,     make_bytes(10, 11));

    EXPECT_EQ(remove_stale_metal4_pipeline_archive_temporaries(m_archive_path), 2u);
    EXPECT_FALSE(std::filesystem::exists(dead_archive));
    EXPECT_FALSE(std::filesystem::exists(dead_manifest));
    EXPECT_TRUE(std::filesystem::exists(live_archive)) << "a live process's write is kept";
    EXPECT_TRUE(std::filesystem::exists(unrelated));
}
