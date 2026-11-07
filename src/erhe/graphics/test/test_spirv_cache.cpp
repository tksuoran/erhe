// Spirv_cache: key composition and atomic storage, without a Device.

#include "erhe_graphics/spirv_cache.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
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
using erhe::graphics::Spirv_cache_record;

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

TEST_F(Spirv_cache_test, record_round_trips)
{
    Spirv_cache cache{m_directory};
    const std::vector<unsigned int> optimized  {c_spirv_magic, 0x00010600, 7, 0, 0};
    const std::vector<unsigned int> unoptimized{c_spirv_magic, 0x00010600, 7, 0, 0, 1, 2};
    const std::string key = cache.get_key("c", Shader_type::fragment_shader, 4);
    cache.put("c", Shader_type::fragment_shader, 4, optimized);
    EXPECT_FALSE(cache.has_record(key));
    EXPECT_FALSE(cache.get_record(key).has_value());

    const Spirv_cache_record record{
        .main_name         = "res/shaders/a.frag",
        .main_source       = "#version 460\n#include \"erhe_preamble.glsl\"\nvoid main() {}\n",
        .preamble          = "#define ERHE_X 1\n",
        .include_paths     = {"res/shaders/erhe_a.glsl", "res/shaders/erhe_b.glsl"},
        .unoptimized_spirv = unoptimized
    };
    cache.put_record(key, record);
    ASSERT_TRUE(cache.has_record(key));
    const std::optional<Spirv_cache_record> read = cache.get_record(key);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->main_name,         record.main_name);
    EXPECT_EQ(read->main_source,       record.main_source);
    EXPECT_EQ(read->preamble,          record.preamble);
    EXPECT_EQ(read->include_paths,     record.include_paths);
    EXPECT_EQ(read->unoptimized_spirv, record.unoptimized_spirv);
    EXPECT_EQ(cache.get_module(key),   optimized) << "the record leaves the cached module as it was";
}

TEST_F(Spirv_cache_test, record_without_unoptimized_module_shares_source_blobs)
{
    Spirv_cache cache{m_directory};
    const std::vector<unsigned int> spirv{c_spirv_magic, 0x00010600, 7, 0, 0};
    const std::string key_a = cache.get_key("d", Shader_type::vertex_shader, 5);
    const std::string key_b = cache.get_key("e", Shader_type::vertex_shader, 5);
    cache.put("d", Shader_type::vertex_shader, 5, spirv);
    cache.put("e", Shader_type::vertex_shader, 5, spirv);
    Spirv_cache_record record{
        .main_name         = "res/shaders/a.vert",
        .main_source       = "void main() {}\n",
        .preamble          = "#define ERHE_Y 1\n",
        .include_paths     = {},
        .unoptimized_spirv = {}
    };
    cache.put_record(key_a, record);
    record.preamble = "#define ERHE_Y 2\n";
    cache.put_record(key_b, record);
    EXPECT_EQ(file_count(".unoptimized.spv"), 0) << "the cached module is the unoptimized one";
    const std::optional<Spirv_cache_record> a = cache.get_record(key_a);
    const std::optional<Spirv_cache_record> b = cache.get_record(key_b);
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_TRUE(a->unoptimized_spirv.empty());
    EXPECT_EQ(a->preamble, "#define ERHE_Y 1\n");
    EXPECT_EQ(b->preamble, "#define ERHE_Y 2\n");
    int blob_count = 0;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{m_directory / "blobs"}) {
        static_cast<void>(entry);
        ++blob_count;
    }
    EXPECT_EQ(blob_count, 3) << "one main source blob shared by both records, two preambles";
}
