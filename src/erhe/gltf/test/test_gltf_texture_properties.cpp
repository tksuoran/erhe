// Texture properties at the parse (doc/erhe/gltf.md "Texture properties"):
// derived from the first material slot that uses an image, or read from
// ERHE_texture_properties; and an image in a named buffer (a Texture item)
// read by range through its reader. parse_gltf creates no Texture, so the
// decoded images carry what residency sets onto the textures.

#include "gltf_test_util.hpp"

#include "erhe_scene/scene.hpp"

#include <gtest/gtest.h>

#include <taskflow/taskflow.hpp>

#include <fmt/format.h>

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

using erhe::graphics::Sampler_address_mode;
using erhe::graphics::Texture_normal_encoding;
using erhe::graphics::Texture_properties;
using erhe::graphics::Texture_transfer_function;
using erhe_gltf_test::parse_file;
using erhe_gltf_test::process_temporary_directory;
using erhe_gltf_test::write_temporary_file;

// The 8-byte PNG signature and filler: enough to sniff, never decoded here.
void write_fake_png(const std::string& file_name)
{
    const unsigned char bytes[] = {0x89u, 'P', 'N', 'G', '\r', '\n', 0x1Au, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
    std::ofstream file{process_temporary_directory() / file_name, std::ios::binary};
    file.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

// Two images: "a" used as base color (wrap clamp / repeat) and then as
// occlusion (a conflicting use); "b" used as a normal map.
[[nodiscard]] auto make_two_image_gltf(const std::string& extensions) -> std::string
{
    return fmt::format(
        R"({{"asset":{{"version":"2.0"}},{}
"samplers":[{{"wrapS":33071,"wrapT":10497}}],
"images":[{{"uri":"a.png"}},{{"uri":"b.png"}}],
"textures":[{{"source":0,"sampler":0}},{{"source":1}}],
"materials":[{{"name":"m","pbrMetallicRoughness":{{"baseColorTexture":{{"index":0}}}}}},
             {{"name":"n","occlusionTexture":{{"index":0}},"normalTexture":{{"index":1}}}}]}})",
        extensions
    );
}

TEST(Gltf_texture_properties, derived_from_the_first_use)
{
    write_fake_png("a.png");
    write_fake_png("b.png");
    erhe::scene::Scene scene{"texture_properties_derived", nullptr};
    const erhe::gltf::Gltf_data data = parse_file(write_temporary_file("derived.gltf", make_two_image_gltf("")), scene);
    const std::vector<erhe::gltf::Gltf_decoded_image>& images = data.image_residency.decoded_images;
    ASSERT_EQ(images.size(), 2u);

    // The material order puts n's occlusion use after m's base color use.
    EXPECT_FALSE(images[0].texture_properties_stored);
    EXPECT_FALSE(images[0].linear);
    EXPECT_EQ(images[0].texture_properties.transfer_function, Texture_transfer_function::srgb);
    EXPECT_EQ(images[0].texture_properties.normal_encoding,   Texture_normal_encoding::none);
    EXPECT_EQ(images[0].texture_properties.wrap_u,            Sampler_address_mode::clamp_to_edge);
    EXPECT_EQ(images[0].texture_properties.wrap_v,            Sampler_address_mode::repeat);
    EXPECT_TRUE(images[0].texture_properties.generate_mipmaps);

    EXPECT_TRUE(images[1].linear);
    EXPECT_EQ(images[1].texture_properties.transfer_function, Texture_transfer_function::linear);
    EXPECT_EQ(images[1].texture_properties.normal_encoding,   Texture_normal_encoding::three_component);
}

TEST(Gltf_texture_properties, stored_properties_replace_the_derivation)
{
    write_fake_png("a.png");
    write_fake_png("b.png");
    const std::string extension =
        R"("extensionsUsed":["ERHE_texture_properties"],"extensions":{"ERHE_texture_properties":{"version":1,"images":[)"
        R"({"image":0,"transfer_function":"linear","normal_encoding":"none","generate_mipmaps":false,"wrap_u":"mirrored_repeat","wrap_v":"clamp_to_edge"}]}},)";
    erhe::scene::Scene scene{"texture_properties_stored", nullptr};
    const erhe::gltf::Gltf_data data = parse_file(write_temporary_file("stored.gltf", make_two_image_gltf(extension)), scene);
    const std::vector<erhe::gltf::Gltf_decoded_image>& images = data.image_residency.decoded_images;
    ASSERT_EQ(images.size(), 2u);

    EXPECT_TRUE(images[0].texture_properties_stored);
    EXPECT_TRUE(images[0].linear) << "the stored transfer function decides the decode";
    const Texture_properties expected{
        .transfer_function = Texture_transfer_function::linear,
        .normal_encoding   = Texture_normal_encoding::none,
        .generate_mipmaps  = false,
        .wrap_u            = Sampler_address_mode::mirrored_repeat,
        .wrap_v            = Sampler_address_mode::clamp_to_edge
    };
    EXPECT_TRUE(images[0].texture_properties == expected);
    // An image without an entry is derived.
    EXPECT_FALSE(images[1].texture_properties_stored);
    EXPECT_EQ(images[1].texture_properties.normal_encoding, Texture_normal_encoding::three_component);
}

class Memory_reader : public erhe::gltf::Gltf_buffer_reader
{
public:
    explicit Memory_reader(std::vector<std::byte> bytes) : m_bytes{std::move(bytes)} {}
    auto read_at(const std::uint64_t offset, const std::span<std::byte> out) const -> bool override
    {
        if ((offset > m_bytes.size()) || (out.size() > (m_bytes.size() - offset))) {
            return false;
        }
        std::memcpy(out.data(), m_bytes.data() + offset, out.size());
        ++m_read_count;
        return true;
    }
    std::vector<std::byte> m_bytes;
    mutable std::size_t    m_read_count{0};
};

// R13.4: an image in a named buffer is not read at the parse; its source
// reads the range through the reader when it is decoded.
TEST(Gltf_texture_properties, named_buffer_image_is_read_by_range)
{
    std::vector<std::byte> payload(64, std::byte{0});
    const unsigned char png[] = {0x89u, 'P', 'N', 'G', '\r', '\n', 0x1Au, '\n'};
    std::memcpy(payload.data() + 16, png, sizeof(png));
    for (std::size_t i = 24; i < 48; ++i) {
        payload[i] = static_cast<std::byte>(i);
    }
    const std::shared_ptr<Memory_reader> reader = std::make_shared<Memory_reader>(payload);
    const std::string uri = "erhe-item:0123456789abcdef0123456789abcdef";
    const std::string json = fmt::format(
        R"({{"asset":{{"version":"2.0"}},"buffers":[{{"uri":"{}","byteLength":64}}],
"bufferViews":[{{"buffer":0,"byteOffset":16,"byteLength":32}}],
"images":[{{"bufferView":0,"mimeType":"image/png"}}],"textures":[{{"source":0}}],
"materials":[{{"pbrMetallicRoughness":{{"baseColorTexture":{{"index":0}}}}}}]}})",
        uri
    );
    const std::filesystem::path path = write_temporary_file("named_image.gltf", json);
    erhe::scene::Scene scene{"named_image", nullptr};
    tf::Executor executor{1};
    const std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
    const erhe::gltf::Gltf_data data = erhe::gltf::parse_gltf(
        erhe::gltf::Gltf_parse_arguments{
            .executor        = executor,
            .root_node       = root_node,
            .path            = path,
            .parallel        = false,
            .buffer_resolver = [&uri, &reader](const std::string_view buffer_uri) -> std::shared_ptr<const erhe::gltf::Gltf_buffer_reader> {
                return (buffer_uri == uri) ? reader : std::shared_ptr<const erhe::gltf::Gltf_buffer_reader>{};
            }
        }
    );
    ASSERT_EQ(data.image_sources.size(), 1u);
    const std::shared_ptr<erhe::gltf::Gltf_image_source>& source = data.image_sources[0];
    ASSERT_TRUE(source);
    EXPECT_TRUE(source->is_reader_backed());
    EXPECT_TRUE(source->encoded_bytes.empty()) << "nothing of the item is retained";
    EXPECT_EQ(source->get_byte_count(), 32u);
    EXPECT_EQ(source->mime_type, "image/png");
    std::vector<std::byte> bytes;
    ASSERT_TRUE(source->read(bytes));
    ASSERT_EQ(bytes.size(), 32u);
    EXPECT_EQ(std::memcmp(bytes.data(), payload.data() + 16, 32), 0);
}

} // anonymous namespace
