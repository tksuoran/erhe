// Spirv_cache: key composition and atomic storage, without a Device.

#include "erhe_graphics/spirv_cache.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#   include <process.h>
#else
#   include <unistd.h>
#endif

namespace {

using erhe::graphics::Shader_type;
using erhe::graphics::Spirv_cache;

constexpr unsigned int c_spirv_magic = 0x07230203;

// The test's own directory name only has to differ between concurrent runs.
auto process_id() -> long
{
#if defined(_WIN32)
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

class Spirv_cache_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_directory = std::filesystem::temp_directory_path() / ("erhe_spirv_cache_test_" + std::to_string(process_id()));
        std::filesystem::remove_all(m_directory);
    }
    void TearDown() override
    {
        std::filesystem::remove_all(m_directory);
    }
    [[nodiscard]] auto file_count(const std::string& name_contains) const -> int
    {
        int count = 0;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{m_directory}) {
            if (entry.path().filename().string().find(name_contains) != std::string::npos) {
                ++count;
            }
        }
        return count;
    }

    std::filesystem::path m_directory;
};

} // anonymous namespace

TEST_F(Spirv_cache_test, put_then_get_round_trips)
{
    Spirv_cache cache{m_directory};
    const std::string               source{"void main() {}"};
    const std::vector<unsigned int> spirv{c_spirv_magic, 0x00010600, 7, 0, 0};

    EXPECT_TRUE(cache.get(source, Shader_type::vertex_shader, 1).empty());
    cache.put(source, Shader_type::vertex_shader, 1, spirv);
    EXPECT_EQ(cache.get(source, Shader_type::vertex_shader, 1), spirv);
}

TEST_F(Spirv_cache_test, key_covers_stage_settings_and_source)
{
    Spirv_cache cache{m_directory};
    const std::string               source{"void main() {}"};
    const std::vector<unsigned int> spirv{c_spirv_magic, 0x00010600, 7, 0, 0};
    cache.put(source, Shader_type::vertex_shader, 1, spirv);

    EXPECT_TRUE(cache.get(source,        Shader_type::fragment_shader, 1).empty()) << "stage is part of the key";
    EXPECT_TRUE(cache.get(source,        Shader_type::vertex_shader,   2).empty()) << "compile settings hash is part of the key";
    EXPECT_TRUE(cache.get(source + "\n", Shader_type::vertex_shader,   1).empty()) << "source is part of the key";
    EXPECT_EQ  (cache.get(source,        Shader_type::vertex_shader,   1), spirv);
}

TEST_F(Spirv_cache_test, put_leaves_no_temporary_file)
{
    Spirv_cache cache{m_directory};
    const std::vector<unsigned int> spirv{c_spirv_magic, 0x00010600, 7, 0, 0};
    cache.put("a", Shader_type::compute_shader, 3, spirv);
    cache.put("a", Shader_type::compute_shader, 3, spirv); // replaces the entry in place
    EXPECT_EQ(file_count(".spv"), 1);
    EXPECT_EQ(file_count(".tmp"), 0);
}

TEST_F(Spirv_cache_test, rejects_entry_without_spirv_magic)
{
    Spirv_cache cache{m_directory};
    const std::vector<unsigned int> not_spirv{0xdeadbeef, 1, 2, 3};
    cache.put("b", Shader_type::compute_shader, 3, not_spirv);
    EXPECT_TRUE(cache.get("b", Shader_type::compute_shader, 3).empty());
}
