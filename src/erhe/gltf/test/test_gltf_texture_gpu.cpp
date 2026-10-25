// Device-backed glTF texture export: a material texture slot needs an
// erhe::graphics::Texture, which only exists with a Device. The retained
// source streams (Gltf_image_source) are synthetic - the exporter embeds them
// verbatim and chooses the texture carrier from the media type, so the
// structure of the written file does not depend on decodable KTX2 content.

#include "gltf_test_util.hpp"

#include "gpu_test_fixture.hpp"

#include "erhe_file/file_log.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "erhe_geometry/geometry_serialization.hpp"
#include "erhe_gltf/gltf_log.hpp"
#include "erhe_graphics/ktx2_container.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_item/item_log.hpp"
#include "erhe_math/math_log.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_property/property_log.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_log.hpp"

#include <geogram/basic/command_line.h>
#include <geogram/basic/command_line_args.h>
#include <geogram/basic/common.h>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <simdjson.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

using erhe::graphics::test::Gpu_test;
using erhe_gltf_test::parse_file;
using erhe_gltf_test::write_temporary_file;

[[nodiscard]] auto glb_json(const std::string& glb) -> std::string
{
    EXPECT_GE(glb.size(), 20u);
    uint32_t json_length = 0;
    std::memcpy(&json_length, glb.data() + 12, sizeof(uint32_t));
    EXPECT_LE(20u + json_length, glb.size());
    return glb.substr(20, json_length);
}

[[nodiscard]] auto contains(simdjson::dom::array array, const std::string_view value) -> bool
{
    for (const simdjson::dom::element element : array) {
        if (element.get_string().value() == value) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] auto make_source(const std::vector<uint8_t>& bytes, const std::string_view mime_type) -> std::shared_ptr<erhe::gltf::Gltf_image_source>
{
    std::shared_ptr<erhe::gltf::Gltf_image_source> source = std::make_shared<erhe::gltf::Gltf_image_source>();
    for (const uint8_t byte : bytes) {
        source->encoded_bytes.push_back(static_cast<std::byte>(byte));
    }
    source->mime_type = std::string{mime_type};
    return source;
}

class Gltf_texture_gpu_test : public Gpu_test
{
protected:
    void SetUp() override
    {
        Gpu_test::SetUp();
        // After Gpu_test_environment created the log sinks and the graphics
        // loggers; the rest as main.cpp of erhe_gltf_tests does.
        static std::once_flag s_once;
        std::call_once(
            s_once,
            []() {
                if (!erhe::file::log_file) {
                    erhe::file::log_file = spdlog::stdout_color_mt("erhe.file.bootstrap");
                }
                erhe::property::initialize_logging();
                erhe::item::initialize_logging();
                erhe::math::initialize_logging();
                erhe::geometry::initialize_logging();
                erhe::raytrace::initialize_logging();
                erhe::primitive::initialize_logging();
                erhe::scene::initialize_logging();
                erhe::gltf::initialize_logging();
                GEO::initialize(GEO::GEOGRAM_INSTALL_NONE);
                GEO::CmdLine::import_arg_group("algo");
                erhe::geometry::register_geogram_attribute_types();
            }
        );
    }

    [[nodiscard]] auto make_texture(const char* name) -> std::shared_ptr<erhe::graphics::Texture>
    {
        return std::make_shared<erhe::graphics::Texture>(
            device(),
            erhe::graphics::Texture_create_info{
                .device      = device(),
                .usage_mask  = erhe::graphics::Image_usage_flag_bit_mask::sampled | erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
                .type        = erhe::graphics::Texture_type::texture_2d,
                .pixelformat = erhe::dataformat::Format::format_8_vec4_unorm,
                .width       = 4,
                .height      = 4,
                .debug_label = erhe::utility::Debug_label{name}
            }
        );
    }
};

// A KTX2 image is written through KHR_texture_basisu (no core `source`, the
// extension used and required); a PNG image keeps the core `source`. Parsing
// the file back binds each material slot to its image.
TEST_F(Gltf_texture_gpu_test, ktx2_texture_exports_through_khr_texture_basisu)
{
    const std::shared_ptr<erhe::graphics::Texture> ktx2_texture = make_texture("ktx2_texture");
    const std::shared_ptr<erhe::graphics::Texture> png_texture  = make_texture("png_texture");

    // The KTX2 stream is the file identifier plus filler bytes: the Basis
    // transcoder rejects it with a warning; the binding is recorded anyway.
    std::map<const erhe::graphics::Texture*, std::shared_ptr<erhe::gltf::Gltf_image_source>> sources;
    sources[ktx2_texture.get()] = make_source({0xABu, 'K', 'T', 'X', ' ', '2', '0', 0xBBu, '\r', '\n', 0x1Au, '\n', 1, 2, 3, 4}, "image/ktx2");
    // A complete 1x1 RGBA PNG: the PNG decoder treats a malformed stream as fatal.
    sources[png_texture.get()]  = make_source(
        {
            0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au, 0x00u, 0x00u, 0x00u, 0x0Du, 0x49u, 0x48u, 0x44u, 0x52u,
            0x00u, 0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x01u, 0x08u, 0x06u, 0x00u, 0x00u, 0x00u, 0x1Fu, 0x15u, 0xC4u,
            0x89u, 0x00u, 0x00u, 0x00u, 0x0Du, 0x49u, 0x44u, 0x41u, 0x54u, 0x78u, 0x9Cu, 0x63u, 0xF8u, 0xDFu, 0xE0u, 0xF0u,
            0x1Fu, 0x00u, 0x07u, 0x00u, 0x02u, 0xBFu, 0x2Bu, 0xD7u, 0xC7u, 0xE2u, 0x00u, 0x00u, 0x00u, 0x00u, 0x49u, 0x45u,
            0x4Eu, 0x44u, 0xAEu, 0x42u, 0x60u, 0x82u
        },
        "image/png"
    );

    erhe::primitive::Material_create_info ktx2_info{.name = "ktx2_material"};
    ktx2_info.data.texture_samplers.base_color.texture_reference = ktx2_texture;
    erhe::primitive::Material_create_info png_info{.name = "png_material"};
    png_info.data.texture_samplers.base_color.texture_reference = png_texture;

    erhe::scene::Scene scene{"source", nullptr};
    const erhe::gltf::Gltf_export_arguments arguments{
        .root_node             = *scene.get_root_node(),
        .binary                = true,
        .image_source_provider = [&sources](const erhe::graphics::Texture* texture) -> std::shared_ptr<const erhe::gltf::Gltf_image_source> {
            const auto i = sources.find(texture);
            return (i != sources.end()) ? i->second : std::shared_ptr<const erhe::gltf::Gltf_image_source>{};
        },
        .extra_materials = {
            std::make_shared<erhe::primitive::Material>(ktx2_info),
            std::make_shared<erhe::primitive::Material>(png_info)
        }
    };
    const std::string glb = erhe::gltf::export_gltf(arguments);
    ASSERT_FALSE(glb.empty());

    const std::string            json_text = glb_json(glb);
    simdjson::dom::parser        parser;
    const simdjson::dom::element root = parser.parse(json_text);
    EXPECT_TRUE(contains(root["extensionsUsed"].get_array(), "KHR_texture_basisu"));
    EXPECT_TRUE(contains(root["extensionsRequired"].get_array(), "KHR_texture_basisu"));

    const simdjson::dom::array images   = root["images"].get_array();
    const simdjson::dom::array textures = root["textures"].get_array();
    ASSERT_EQ(images.size(), 2u);
    ASSERT_EQ(textures.size(), 2u);
    std::size_t basisu_texture_count = 0;
    std::size_t core_texture_count   = 0;
    for (const simdjson::dom::element texture : textures) {
        simdjson::dom::element basisu;
        if (texture.at_pointer("/extensions/KHR_texture_basisu").get(basisu) == simdjson::SUCCESS) {
            ++basisu_texture_count;
            const uint64_t image_index = basisu["source"].get_uint64().value();
            EXPECT_EQ(images.at(image_index)["mimeType"].get_string().value(), "image/ktx2");
            simdjson::dom::element core_source;
            EXPECT_NE(texture["source"].get(core_source), simdjson::SUCCESS) << "KTX2 texture must not name a core source";
        } else {
            ++core_texture_count;
            const uint64_t image_index = texture["source"].get_uint64().value();
            EXPECT_EQ(images.at(image_index)["mimeType"].get_string().value(), "image/png");
        }
    }
    EXPECT_EQ(basisu_texture_count, 1u);
    EXPECT_EQ(core_texture_count, 1u);

    erhe::scene::Scene          loaded{"loaded", nullptr};
    const erhe::gltf::Gltf_data data = parse_file(write_temporary_file("ktx2_texture.glb", glb), loaded);
    ASSERT_EQ(data.materials.size(), 2u);
    const std::vector<erhe::gltf::Gltf_material_texture_binding>& bindings = data.image_residency.material_texture_bindings;
    ASSERT_EQ(bindings.size(), 2u);
    for (const erhe::gltf::Gltf_material_texture_binding& binding : bindings) {
        EXPECT_EQ(binding.slot, erhe::gltf::Gltf_material_texture_slot::base_color);
        ASSERT_LT(binding.image_index, images.size()) << "material " << binding.material_index << " lost its image";
        const std::string_view expected_mime = (data.materials.at(binding.material_index)->get_name() == "ktx2_material")
            ? "image/ktx2"
            : "image/png";
        EXPECT_EQ(images.at(binding.image_index)["mimeType"].get_string().value(), expected_mime);
    }
}

// A raw block KTX2 image (ASTC 4x4, as "Save for Quest" writes it) is written
// through the asset-root ERHE_texture_ktx2 extension: the texture names no
// core source and no KHR_texture_basisu. Parsing the file back resolves the
// texture to its image through the extension, and the retained stream is the
// written KTX2 file byte for byte. ktx2_astc_4x4 leaves a raw KTX2 and a PNG
// as they are (only Basis Universal payloads are transcoded).
TEST_F(Gltf_texture_gpu_test, raw_ktx2_texture_round_trips_through_erhe_texture_ktx2)
{
    const std::shared_ptr<erhe::graphics::Texture> astc_texture = make_texture("astc_texture");

    // 4x4 image, one level: one ASTC void-extent block (constant color).
    const std::vector<uint8_t> block{
        0xFCu, 0xFDu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
        0xFFu, 0xFFu, 0x80u, 0x80u, 0x00u, 0x00u, 0xFFu, 0xFFu
    };
    const std::vector<uint8_t> ktx2 = erhe::graphics::write_ktx2_raw_blocks(
        erhe::graphics::Ktx2_raw_blocks_image{
            .format               = erhe::dataformat::Format::format_astc_4x4_srgb,
            .width                = 4,
            .height               = 4,
            .levels               = {std::span<const uint8_t>{block.data(), block.size()}},
            .two_component_normal = false
        }
    );
    ASSERT_FALSE(ktx2.empty());
    std::map<const erhe::graphics::Texture*, std::shared_ptr<erhe::gltf::Gltf_image_source>> sources;
    sources[astc_texture.get()] = make_source(ktx2, "image/ktx2");

    erhe::primitive::Material_create_info astc_info{.name = "astc_material"};
    astc_info.data.texture_samplers.base_color.texture_reference = astc_texture;

    for (const erhe::gltf::Gltf_texture_export texture_export : {erhe::gltf::Gltf_texture_export::source, erhe::gltf::Gltf_texture_export::ktx2_astc_4x4}) {
        erhe::scene::Scene scene{"source", nullptr};
        erhe::gltf::Gltf_export_texture_statistics statistics;
        erhe::gltf::Gltf_export_arguments arguments{
            .root_node             = *scene.get_root_node(),
            .binary                = true,
            .image_source_provider = [&sources](const erhe::graphics::Texture* texture) -> std::shared_ptr<const erhe::gltf::Gltf_image_source> {
                const auto i = sources.find(texture);
                return (i != sources.end()) ? i->second : std::shared_ptr<const erhe::gltf::Gltf_image_source>{};
            },
            .extra_materials = {std::make_shared<erhe::primitive::Material>(astc_info)}
        };
        arguments.texture_export     = texture_export;
        arguments.texture_statistics = &statistics;
        const std::string glb = erhe::gltf::export_gltf(arguments);
        ASSERT_FALSE(glb.empty());
        EXPECT_EQ(statistics.transcoded_count, 0u);
        EXPECT_EQ(statistics.raw_ktx2_count, 1u);

        const std::string            json_text = glb_json(glb);
        simdjson::dom::parser        parser;
        const simdjson::dom::element root = parser.parse(json_text);
        EXPECT_TRUE(contains(root["extensionsUsed"].get_array(), "ERHE_texture_ktx2"));
        EXPECT_FALSE(contains(root["extensionsUsed"].get_array(), "KHR_texture_basisu"));
        simdjson::dom::array required;
        if (root["extensionsRequired"].get_array().get(required) == simdjson::SUCCESS) {
            EXPECT_FALSE(contains(required, "KHR_texture_basisu"));
        }
        const simdjson::dom::array textures = root["textures"].get_array();
        ASSERT_EQ(textures.size(), 1u);
        simdjson::dom::element unused;
        EXPECT_NE(textures.at(0)["source"].get(unused), simdjson::SUCCESS);
        EXPECT_NE(textures.at(0)["extensions"].get(unused), simdjson::SUCCESS);
        const simdjson::dom::element extension = root.at_pointer("/extensions/ERHE_texture_ktx2");
        EXPECT_EQ(extension["version"].get_uint64().value(), 1u);
        const simdjson::dom::array entries = extension["textures"].get_array();
        ASSERT_EQ(entries.size(), 1u);
        EXPECT_EQ(entries.at(0)["texture"].get_uint64().value(), 0u);
        const uint64_t image_index = entries.at(0)["source"].get_uint64().value();
        EXPECT_EQ(root["images"].get_array().at(image_index)["mimeType"].get_string().value(), "image/ktx2");

        erhe::scene::Scene          loaded{"loaded", nullptr};
        const erhe::gltf::Gltf_data data = parse_file(write_temporary_file("raw_ktx2_texture.glb", glb), loaded);
        const std::vector<erhe::gltf::Gltf_material_texture_binding>& bindings = data.image_residency.material_texture_bindings;
        ASSERT_EQ(bindings.size(), 1u);
        EXPECT_EQ(bindings[0].slot, erhe::gltf::Gltf_material_texture_slot::base_color);
        ASSERT_EQ(bindings[0].image_index, image_index);
        ASSERT_LT(image_index, data.image_sources.size());
        const std::shared_ptr<erhe::gltf::Gltf_image_source>& loaded_source = data.image_sources[image_index];
        ASSERT_TRUE(loaded_source);
        ASSERT_EQ(loaded_source->encoded_bytes.size(), ktx2.size());
        EXPECT_EQ(std::memcmp(loaded_source->encoded_bytes.data(), ktx2.data(), ktx2.size()), 0);
    }
}

} // anonymous namespace
