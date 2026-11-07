// Raw block KTX2 images (doc/erhe/gltf.md "Authoring data"): raw GPU blocks
// are derived data, so a file parse refuses such an image as a texture
// source (logged; the texture has no image), and an item parse (a quest
// Scene item) reads it.

#include "gltf_test_util.hpp"

#include "erhe_graphics/ktx2_container.hpp"
#include "erhe_log/log.hpp"
#include "erhe_scene/scene.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <deque>
#include <fstream>
#include <string>
#include <vector>

namespace {

using erhe_gltf_test::parse_file;
using erhe_gltf_test::process_temporary_directory;
using erhe_gltf_test::write_temporary_file;

void write_bytes(const std::string& file_name, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream file{process_temporary_directory() / file_name, std::ios::binary};
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// A 4x4 ASTC image of one void-extent block, and an 8-byte PNG signature (sniffed, never decoded here).
void write_images()
{
    const std::vector<std::uint8_t> block{
        0xFCu, 0xFDu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
        0xFFu, 0xFFu, 0x80u, 0x80u, 0x00u, 0x00u, 0xFFu, 0xFFu
    };
    const std::vector<std::uint8_t> ktx2 = erhe::graphics::write_ktx2_raw_blocks(
        erhe::graphics::Ktx2_raw_blocks_image{
            .format = erhe::dataformat::Format::format_astc_4x4_srgb,
            .width  = 4,
            .height = 4,
            .levels = {std::span<const std::uint8_t>{block.data(), block.size()}}
        }
    );
    ASSERT_TRUE(erhe::graphics::is_ktx2_raw_blocks(std::span<const std::uint8_t>{ktx2.data(), ktx2.size()}));
    write_bytes("raw_blocks.ktx2", ktx2);
    write_bytes("raw_blocks_neighbour.png", {0x89u, 'P', 'N', 'G', '\r', '\n', 0x1Au, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R'});
}

constexpr const char* c_document = R"({"asset":{"version":"2.0"},
"images":[{"uri":"raw_blocks.ktx2","name":"raw"},{"uri":"raw_blocks_neighbour.png","name":"png"}],
"textures":[{"source":0},{"source":1}],
"materials":[{"name":"m","pbrMetallicRoughness":{"baseColorTexture":{"index":0}},"normalTexture":{"index":1}}]})";

[[nodiscard]] auto count_refusals() -> std::size_t
{
    std::size_t count = 0;
    erhe::log::get_tail_store_log().access_entries([&count](std::deque<erhe::log::Entry>& entries) {
        for (const erhe::log::Entry& entry : entries) {
            if ((entry.message.find("raw GPU block data") != std::string::npos) && (entry.message.find("'raw'") != std::string::npos)) {
                ++count;
            }
        }
    });
    return count;
}

} // anonymous namespace

TEST(Gltf_raw_block_images, a_file_parse_refuses_a_raw_block_source_and_an_item_parse_reads_it)
{
    write_images();
    const std::filesystem::path path = write_temporary_file("raw_blocks.gltf", c_document);

    const std::size_t refusals_before = count_refusals();
    erhe::scene::Scene file_scene{"file", nullptr};
    const erhe::gltf::Gltf_data file = parse_file(path, file_scene);
    ASSERT_EQ(file.image_sources.size(), 2u);
    EXPECT_FALSE(file.image_sources[0]) << "a raw block KTX2 image is no source of an authoring document";
    ASSERT_EQ(file.image_residency.decoded_images.size(), 2u);
    EXPECT_FALSE(file.image_residency.decoded_images[0].source);
    EXPECT_TRUE (file.image_residency.decoded_images[0].decoded);
    EXPECT_FALSE(file.image_residency.decoded_images[0].ok) << "settled as an undecodable image: the texture has no image";
    EXPECT_TRUE (file.image_sources[1]) << "the PNG beside it stays";
    EXPECT_EQ(count_refusals(), refusals_before + 1u);

    erhe::scene::Scene item_scene{"item", nullptr};
    const erhe::gltf::Gltf_data item = parse_file(path, item_scene, erhe::gltf::Gltf_document::item);
    ASSERT_EQ(item.image_sources.size(), 2u);
    ASSERT_TRUE(item.image_sources[0]);
    EXPECT_EQ(item.image_sources[0]->mime_type, "image/ktx2");
    EXPECT_FALSE(item.image_residency.decoded_images[0].decoded);
    EXPECT_EQ(count_refusals(), refusals_before + 1u);
}
