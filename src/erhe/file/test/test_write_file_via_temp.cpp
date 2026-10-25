// erhe::file::write_file_via_temp: the target is replaced only by a
// completed write; a failed write leaves it byte-identical and leaves no
// temporary file behind.

#include "erhe_file/file.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace {

class Write_file_via_temp_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* const info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_directory = std::filesystem::temp_directory_path() / (std::string{"erhe_file_tests_"} + info->name());
        std::error_code error_code;
        std::filesystem::remove_all(m_directory, error_code);
        ASSERT_TRUE(std::filesystem::create_directories(m_directory));
        m_path = m_directory / "scene.glb";
    }

    void TearDown() override
    {
        std::error_code error_code;
        std::filesystem::remove_all(m_directory, error_code);
    }

    [[nodiscard]] auto read_back() const -> std::string
    {
        const std::optional<std::string> content = erhe::file::read("test", m_path);
        return content.has_value() ? content.value() : std::string{};
    }

    [[nodiscard]] auto entry_count() const -> std::size_t
    {
        std::size_t count = 0;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{m_directory}) {
            static_cast<void>(entry);
            ++count;
        }
        return count;
    }

    std::filesystem::path m_directory;
    std::filesystem::path m_path;
};

} // anonymous namespace

TEST_F(Write_file_via_temp_test, writes_a_new_file)
{
    ASSERT_TRUE(erhe::file::write_file_via_temp(m_path, std::string_view{"first"}));
    EXPECT_EQ(read_back(), "first");
    EXPECT_EQ(entry_count(), 1u) << "no temporary file is left";
}

TEST_F(Write_file_via_temp_test, replaces_an_existing_file_while_the_writer_reads_it)
{
    ASSERT_TRUE(erhe::file::write_file_via_temp(m_path, std::string_view{"old content"}));
    // The writer reads the file it replaces: the old content is intact
    // until the write completes.
    std::string seen_during_write;
    const bool ok = erhe::file::write_file_via_temp(
        m_path,
        [this, &seen_during_write](std::FILE* const file) -> bool {
            seen_during_write = read_back();
            const std::string text = seen_during_write + " + new";
            return std::fwrite(text.data(), 1, text.size(), file) == text.size();
        }
    );
    ASSERT_TRUE(ok);
    EXPECT_EQ(seen_during_write, "old content");
    EXPECT_EQ(read_back(), "old content + new");
    EXPECT_EQ(entry_count(), 1u);
}

TEST_F(Write_file_via_temp_test, a_failed_write_keeps_the_old_file)
{
    ASSERT_TRUE(erhe::file::write_file_via_temp(m_path, std::string_view{"original bytes"}));
    const bool ok = erhe::file::write_file_via_temp(
        m_path,
        [](std::FILE* const file) -> bool {
            const std::string partial{"partial"};
            static_cast<void>(std::fwrite(partial.data(), 1, partial.size(), file));
            return false; // the save fails mid-write
        }
    );
    EXPECT_FALSE(ok);
    EXPECT_EQ(read_back(), "original bytes");
    EXPECT_EQ(entry_count(), 1u) << "the temporary file is removed";
}

TEST_F(Write_file_via_temp_test, a_failed_open_keeps_the_old_file)
{
    ASSERT_TRUE(erhe::file::write_file_via_temp(m_path, std::string_view{"original bytes"}));
    // The temporary file goes next to the target: a target in a missing
    // directory cannot be written at all.
    const std::filesystem::path missing = m_directory / "missing" / "scene.glb";
    EXPECT_FALSE(erhe::file::write_file_via_temp(missing, std::string_view{"new"}));
    EXPECT_FALSE(std::filesystem::exists(missing));
    EXPECT_EQ(read_back(), "original bytes");
    EXPECT_EQ(entry_count(), 1u);
}

#if defined(ERHE_OS_WINDOWS)
// Windows refuses to replace a file another handle holds open without
// FILE_SHARE_DELETE (the CRT opens files that way): the rename fails after
// the temporary file is complete, and the target keeps its old bytes.
TEST_F(Write_file_via_temp_test, a_failed_replace_keeps_the_old_file)
{
    ASSERT_TRUE(erhe::file::write_file_via_temp(m_path, std::string_view{"original bytes"}));
    std::FILE* const holder = _wfopen(m_path.wstring().c_str(), L"rb");
    ASSERT_NE(holder, nullptr);
    const bool ok = erhe::file::write_file_via_temp(m_path, std::string_view{"new"});
    std::fclose(holder);
    EXPECT_FALSE(ok);
    EXPECT_EQ(read_back(), "original bytes");
    EXPECT_EQ(entry_count(), 1u) << "the temporary file is removed";
}
#endif
