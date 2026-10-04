// Persisted VkPipelineCache data file: path, header check, atomic write;
// no Vulkan device involved.

#include "erhe_graphics/vulkan/vulkan_pipeline_cache_file.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#   include <process.h>
#else
#   include <unistd.h>
#endif

namespace {

using erhe::graphics::Pipeline_cache_identity;
using erhe::graphics::make_pipeline_cache_path;
using erhe::graphics::read_pipeline_cache_file;
using erhe::graphics::write_pipeline_cache_file;

auto process_id() -> long
{
#if defined(_WIN32)
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

auto make_identity() -> Pipeline_cache_identity
{
    Pipeline_cache_identity identity{};
    identity.vendor_id = 0x8086;
    identity.device_id = 0x7d55;
    for (std::size_t i = 0; i < identity.uuid.size(); ++i) {
        identity.uuid[i] = static_cast<uint8_t>(0xa0 + i);
    }
    return identity;
}

void write_u32_le(std::vector<uint8_t>& data, const std::size_t offset, const uint32_t value)
{
    data[offset + 0] = static_cast<uint8_t>((value      ) & 0xffu);
    data[offset + 1] = static_cast<uint8_t>((value >>  8) & 0xffu);
    data[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xffu);
    data[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xffu);
}

// A VkPipelineCacheHeaderVersionOne for the identity followed by payload bytes.
auto make_cache_data(const Pipeline_cache_identity& identity, const std::size_t payload_size) -> std::vector<uint8_t>
{
    std::vector<uint8_t> data(32 + payload_size, 0);
    write_u32_le(data, 0, 32);
    write_u32_le(data, 4, 1);
    write_u32_le(data, 8, identity.vendor_id);
    write_u32_le(data, 12, identity.device_id);
    std::memcpy(data.data() + 16, identity.uuid.data(), identity.uuid.size());
    for (std::size_t i = 0; i < payload_size; ++i) {
        data[32 + i] = static_cast<uint8_t>(i * 7);
    }
    return data;
}

class Pipeline_cache_file_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_directory = std::filesystem::temp_directory_path() / ("erhe_pipeline_cache_test_" + std::to_string(process_id()));
        std::filesystem::remove_all(m_directory);
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

    std::filesystem::path m_directory;
};

} // anonymous namespace

TEST_F(Pipeline_cache_file_test, path_names_the_identity)
{
    const Pipeline_cache_identity identity = make_identity();
    const std::filesystem::path   path     = make_pipeline_cache_path(m_directory, identity);
    EXPECT_EQ(path.parent_path(), m_directory);
    EXPECT_EQ(path.filename().string(), "vk_00008086_00007d55_a0a1a2a3a4a5a6a7a8a9aaabacadaeaf.bin");
}

TEST_F(Pipeline_cache_file_test, write_then_read_round_trips_and_creates_directory)
{
    const Pipeline_cache_identity identity = make_identity();
    const std::filesystem::path   path     = make_pipeline_cache_path(m_directory, identity);
    const std::vector<uint8_t>    data     = make_cache_data(identity, 100);

    EXPECT_FALSE(std::filesystem::exists(m_directory));
    ASSERT_TRUE(write_pipeline_cache_file(path, data));
    EXPECT_EQ(read_pipeline_cache_file(path, identity), data);
    EXPECT_EQ(file_count(), 1) << "no temporary file left behind";

    ASSERT_TRUE(write_pipeline_cache_file(path, make_cache_data(identity, 10)));
    EXPECT_EQ(read_pipeline_cache_file(path, identity).size(), 42u) << "replaced in place";
    EXPECT_EQ(file_count(), 1);
}

TEST_F(Pipeline_cache_file_test, read_rejects_other_identity_and_bad_header)
{
    const Pipeline_cache_identity identity = make_identity();
    const std::filesystem::path   path     = make_pipeline_cache_path(m_directory, identity);
    ASSERT_TRUE(write_pipeline_cache_file(path, make_cache_data(identity, 8)));

    Pipeline_cache_identity other_device = identity;
    other_device.device_id = 0x1234;
    EXPECT_TRUE(read_pipeline_cache_file(path, other_device).empty());

    Pipeline_cache_identity other_driver = identity;
    other_driver.uuid[3] ^= 0xff;
    EXPECT_TRUE(read_pipeline_cache_file(path, other_driver).empty());

    std::vector<uint8_t> wrong_version = make_cache_data(identity, 8);
    write_u32_le(wrong_version, 4, 2);
    ASSERT_TRUE(write_pipeline_cache_file(path, wrong_version));
    EXPECT_TRUE(read_pipeline_cache_file(path, identity).empty());

    std::vector<uint8_t> truncated = make_cache_data(identity, 8);
    truncated.resize(20);
    ASSERT_TRUE(write_pipeline_cache_file(path, truncated));
    EXPECT_TRUE(read_pipeline_cache_file(path, identity).empty());

    EXPECT_TRUE(read_pipeline_cache_file(m_directory / "absent.bin", identity).empty());
}
