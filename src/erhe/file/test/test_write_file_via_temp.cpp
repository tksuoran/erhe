// erhe::file::write_file_via_temp: the target is replaced only by a
// completed write; a failed write leaves it byte-identical and leaves no
// temporary file behind.

#include "erhe_file/file.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <optional>
#include <span>
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

// write_files_via_temp(): a file that cannot be written replaces none of the
// others, and no temporary stays behind; a complete set replaces them all.
TEST_F(Write_file_via_temp_test, files_are_replaced_only_when_all_are_written)
{
    const std::filesystem::path buffer_path = m_directory / "scene.bin";
    ASSERT_TRUE(erhe::file::write_file_via_temp(buffer_path, std::string_view{"old buffer"}));
    ASSERT_TRUE(erhe::file::write_file_via_temp(m_path, std::string_view{"old document"}));
    const std::string new_buffer   = "new buffer";
    const std::string new_document = "new document";
    const std::span<const std::byte> buffer_bytes   = std::as_bytes(std::span<const char>{new_buffer});
    const std::span<const std::byte> document_bytes = std::as_bytes(std::span<const char>{new_document});

    const erhe::file::File_write failing[] = {
        {.path = buffer_path,                           .bytes = buffer_bytes},
        {.path = m_directory / "missing" / "scene.glb", .bytes = document_bytes}
    };
    EXPECT_FALSE(erhe::file::write_files_via_temp(failing));
    EXPECT_EQ(erhe::file::read("test", buffer_path).value_or(std::string{}), "old buffer");
    EXPECT_EQ(entry_count(), 2u) << "no temporary file stays";

    const erhe::file::File_write complete[] = {
        {.path = buffer_path, .bytes = buffer_bytes},
        {.path = m_path,      .bytes = document_bytes}
    };
    EXPECT_TRUE(erhe::file::write_files_via_temp(complete));
    EXPECT_EQ(erhe::file::read("test", buffer_path).value_or(std::string{}), "new buffer");
    EXPECT_EQ(read_back(), "new document");
    EXPECT_EQ(entry_count(), 2u);
}

namespace {

// The platform rename, failing at call number 'fail_at' (0-based).
[[nodiscard]] auto make_failing_rename(std::size_t& call_count, const std::size_t fail_at) -> erhe::file::Rename_function
{
    return [&call_count, fail_at](const std::filesystem::path& from, const std::filesystem::path& to) -> bool {
        const std::size_t call = call_count++;
        return (call != fail_at) && erhe::file::rename_file(from, to);
    };
}

} // anonymous namespace

// Every rename of a two-file replace failing in turn: the old files come back
// byte-identical, the new ones vanish and no temporary stays. Renames:
// 0, 1 move the old files aside, 2, 3 move the new ones in.
TEST_F(Write_file_via_temp_test, a_failed_rename_restores_every_old_file)
{
    const std::filesystem::path buffer_path = m_directory / "scene.bin";
    const std::string new_buffer   = "new buffer";
    const std::string new_document = "new document";
    const erhe::file::File_write files[] = {
        {.path = buffer_path, .bytes = std::as_bytes(std::span<const char>{new_buffer})},
        {.path = m_path,      .bytes = std::as_bytes(std::span<const char>{new_document})}
    };
    for (std::size_t fail_at = 0; fail_at < 4; ++fail_at) {
        ASSERT_TRUE(erhe::file::write_file_via_temp(buffer_path, std::string_view{"old buffer"}));
        ASSERT_TRUE(erhe::file::write_file_via_temp(m_path, std::string_view{"old document"}));
        std::size_t call_count = 0;
        EXPECT_FALSE(erhe::file::write_files_via_temp(files, make_failing_rename(call_count, fail_at))) << fail_at;
        EXPECT_EQ(erhe::file::read("test", buffer_path).value_or(std::string{}), "old buffer") << fail_at;
        EXPECT_EQ(read_back(), "old document") << fail_at;
        EXPECT_EQ(entry_count(), 2u) << "no temporary file stays, rename " << fail_at;
    }
}

// A set whose second target is new: a failed move-in removes the first new file
// and restores its old one, and leaves the second target absent.
TEST_F(Write_file_via_temp_test, a_failed_rename_removes_a_new_target)
{
    const std::filesystem::path buffer_path = m_directory / "scene.bin";
    ASSERT_TRUE(erhe::file::write_file_via_temp(buffer_path, std::string_view{"old buffer"}));
    const std::string new_buffer   = "new buffer";
    const std::string new_document = "new document";
    const erhe::file::File_write files[] = {
        {.path = buffer_path, .bytes = std::as_bytes(std::span<const char>{new_buffer})},
        {.path = m_path,      .bytes = std::as_bytes(std::span<const char>{new_document})}
    };
    std::size_t call_count = 0;
    // Renames: 0 moves the old buffer aside, 1 moves the new buffer in, 2 fails the document.
    EXPECT_FALSE(erhe::file::write_files_via_temp(files, make_failing_rename(call_count, 2)));
    EXPECT_EQ(erhe::file::read("test", buffer_path).value_or(std::string{}), "old buffer");
    EXPECT_FALSE(std::filesystem::exists(m_path));
    EXPECT_EQ(entry_count(), 1u);
}

// A target that is a directory refuses the set before anything is written.
TEST_F(Write_file_via_temp_test, a_target_that_is_not_a_file_is_refused)
{
    const std::filesystem::path buffer_path = m_directory / "scene.bin";
    ASSERT_TRUE(erhe::file::write_file_via_temp(buffer_path, std::string_view{"old buffer"}));
    ASSERT_TRUE(std::filesystem::create_directory(m_path));
    const std::string new_bytes = "new";
    const erhe::file::File_write files[] = {
        {.path = buffer_path, .bytes = std::as_bytes(std::span<const char>{new_bytes})},
        {.path = m_path,      .bytes = std::as_bytes(std::span<const char>{new_bytes})}
    };
    EXPECT_FALSE(erhe::file::write_files_via_temp(files));
    EXPECT_EQ(erhe::file::read("test", buffer_path).value_or(std::string{}), "old buffer");
    EXPECT_TRUE(std::filesystem::is_directory(m_path));
    EXPECT_EQ(entry_count(), 2u);
}

// One file: a failed rename leaves the old target in place (never absent).
TEST_F(Write_file_via_temp_test, a_single_file_is_one_rename_over_the_target)
{
    ASSERT_TRUE(erhe::file::write_file_via_temp(m_path, std::string_view{"old"}));
    const std::string new_bytes = "new";
    const erhe::file::File_write files[] = {{.path = m_path, .bytes = std::as_bytes(std::span<const char>{new_bytes})}};
    std::size_t call_count = 0;
    EXPECT_FALSE(erhe::file::write_files_via_temp(files, make_failing_rename(call_count, 0)));
    EXPECT_EQ(call_count, 1u) << "no move aside";
    EXPECT_EQ(read_back(), "old");
    EXPECT_EQ(entry_count(), 1u);
    EXPECT_TRUE(erhe::file::write_files_via_temp(files));
    EXPECT_EQ(read_back(), "new");
}
