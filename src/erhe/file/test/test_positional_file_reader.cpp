// erhe::file::Positional_file_reader: positional reads, from several threads
// at once through one handle.

#include "erhe_file/file.hpp"
#include "erhe_file/positional_file_reader.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

[[nodiscard]] auto make_test_file(const std::string& name, const std::size_t size) -> std::filesystem::path
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::string text(size, ' ');
    for (std::size_t i = 0; i < size; ++i) {
        text[i] = static_cast<char>(i * 7);
    }
    EXPECT_TRUE(erhe::file::write_file(path, text));
    return path;
}

TEST(Positional_file_reader, reads_ranges_from_many_threads)
{
    constexpr std::size_t size = 1u << 20;
    const std::filesystem::path path = make_test_file("erhe_positional_reader_test.bin", size);
    erhe::file::Positional_file_reader reader;
    ASSERT_TRUE(reader.open(path));
    std::vector<std::thread> threads;
    std::vector<int>         ok(8, 0);
    for (std::size_t t = 0; t < ok.size(); ++t) {
        threads.emplace_back(
            [&, t]() {
                bool all = true;
                for (std::size_t i = 0; i < 64; ++i) {
                    const std::size_t offset = ((t * 64) + i) * 1000;
                    std::vector<std::byte> bytes(512);
                    all = all && reader.read_at(offset, bytes);
                    for (std::size_t j = 0; j < bytes.size(); ++j) {
                        all = all && (bytes[j] == static_cast<std::byte>(static_cast<char>((offset + j) * 7)));
                    }
                }
                ok[t] = all ? 1 : 0;
            }
        );
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    for (const int value : ok) {
        EXPECT_EQ(value, 1);
    }
    std::vector<std::byte> past_end(16);
    EXPECT_FALSE(reader.read_at(size - 8, past_end));
    reader.close();
    EXPECT_FALSE(reader.is_open());
    std::filesystem::remove(path);
}

} // anonymous namespace
