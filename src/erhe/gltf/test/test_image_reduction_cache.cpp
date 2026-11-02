#include "erhe_dataformat/dataformat.hpp"
#include "erhe_gltf/image_reduction_cache.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace {

using erhe::graphics::Block_encode_format;
using erhe::graphics::Image_info;
using erhe::graphics::Image_reduction;

class Image_reduction_cache_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_directory = std::filesystem::temp_directory_path() / ("erhe_image_reduction_cache_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
        std::filesystem::remove_all(m_directory);
    }
    void TearDown() override
    {
        std::filesystem::remove_all(m_directory);
    }

    std::filesystem::path m_directory;
};

const std::vector<std::byte> c_stream{std::byte{1}, std::byte{2}, std::byte{3}};

[[nodiscard]] auto make_bc7_payload(Image_info& info) -> std::vector<std::uint8_t>
{
    info = Image_info{.width = 8, .height = 8, .depth = 1, .level_count = 4, .row_stride = 0, .format = erhe::dataformat::Format::format_bc7_srgb};
    std::vector<std::uint8_t> payload(erhe::dataformat::get_mip_chain_byte_count(info.format, 8, 8, 4));
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<std::uint8_t>(i * 7);
    }
    return payload;
}

TEST_F(Image_reduction_cache_test, stored_entry_loads_back_and_other_keys_miss)
{
    const Image_reduction reduction{.encode_format = Block_encode_format::bc7};
    const erhe::gltf::Image_reduction_cache_key key = erhe::gltf::make_image_reduction_cache_key(c_stream, reduction, false);
    Image_info info{};
    const std::vector<std::uint8_t> payload = make_bc7_payload(info);
    erhe::gltf::store_reduced_image(m_directory, key, info, payload);

    Image_info                loaded_info{};
    std::vector<std::uint8_t> loaded;
    ASSERT_TRUE(erhe::gltf::load_reduced_image(m_directory, key, loaded_info, loaded));
    EXPECT_EQ(loaded, payload);
    EXPECT_EQ(loaded_info.format, info.format);
    EXPECT_EQ(loaded_info.width, 8);
    EXPECT_EQ(loaded_info.level_count, 4);

    // Color space, reduction and content are all part of the key.
    for (const erhe::gltf::Image_reduction_cache_key other : {
        erhe::gltf::make_image_reduction_cache_key(c_stream, reduction, true),
        erhe::gltf::make_image_reduction_cache_key(c_stream, Image_reduction{.encode_format = Block_encode_format::bc7, .downscale_levels = 1}, false),
        erhe::gltf::make_image_reduction_cache_key(std::vector<std::byte>{std::byte{1}, std::byte{2}}, reduction, false)
    }) {
        EXPECT_NE(other.value, key.value);
        EXPECT_FALSE(erhe::gltf::load_reduced_image(m_directory, other, loaded_info, loaded));
    }
}

TEST_F(Image_reduction_cache_test, truncated_entry_is_a_miss)
{
    const erhe::gltf::Image_reduction_cache_key key = erhe::gltf::make_image_reduction_cache_key(c_stream, Image_reduction{.encode_format = Block_encode_format::bc7}, false);
    Image_info info{};
    const std::vector<std::uint8_t> payload = make_bc7_payload(info);
    erhe::gltf::store_reduced_image(m_directory, key, info, payload);
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{m_directory}) {
        std::filesystem::resize_file(entry.path(), std::filesystem::file_size(entry.path()) - 1);
    }
    Image_info                loaded_info{};
    std::vector<std::uint8_t> loaded;
    EXPECT_FALSE(erhe::gltf::load_reduced_image(m_directory, key, loaded_info, loaded));
}

} // anonymous namespace
