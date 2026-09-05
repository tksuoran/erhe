#include "parsers/usd.hpp"

#include "editor_log.hpp"

#include "erhe_primitive/build_info.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace editor {

auto is_usd_file_extension(const std::filesystem::path& path) -> bool
{
    std::string extension = path.extension().generic_string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](const unsigned char c) { return static_cast<char>(std::tolower(c)); }
    );
    return (extension == ".usd") || (extension == ".usda") || (extension == ".usdc") || (extension == ".usdz");
}

} // namespace editor

#if defined(ERHE_USD_LIBRARY_LIGHTUSD)

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "operations/async_raytrace_kickoff_operation.hpp"
#include "operations/compound_operation.hpp"
#include "operations/content_library_attach_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/operation_stack.hpp"
#include "parsers/gltf.hpp"
#include "scene/scene_root.hpp"

#include "scene/generated/gltf_source_reference.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_file/file.hpp"
#include "erhe_gltf/gltf.hpp"
#include "erhe_gltf/image_transfer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/image_loader.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_usd/usd.hpp"

#include "editor_log.hpp"
#include "items.hpp"

#include <fmt/format.h>

#include <chrono>
#include <span>
#include <vector>

namespace editor {

namespace {

// One image file the stage names, loaded into a GPU texture. USD material
// inputs reference image files, and LightUSD's own image loaders are off
// (erhe::usd/notes.md), so the decode goes through erhe's Image_loader - the
// same one Texture_file_loader uses for standalone image files - and the
// upload through a blocking-drain Image_transfer, which is what the inline
// glTF import path does as well.
[[nodiscard]] auto load_usd_image(
    erhe::graphics::Device&                      graphics_device,
    erhe::gltf::Image_transfer&                  image_transfer,
    const erhe::usd::Usd_image&                  image
) -> std::shared_ptr<erhe::graphics::Texture>
{
    ERHE_PROFILE_FUNCTION();

    std::error_code error_code;
    if (image.path.empty() || !std::filesystem::exists(image.path, error_code)) {
        log_parsers->warn("USD image '{}' not found", image.path.generic_string());
        return {};
    }

    erhe::graphics::Image_loader              loader;
    erhe::graphics::Image_info                info;
    const erhe::graphics::Transcode_format_preference transcode_format_preference =
        erhe::gltf::query_gltf_device_options(graphics_device).transcode_format_preference;
    // `linear` selects the non-color interpretation: a normal or occlusion
    // map carries data, everything else carries sRGB color. The USD source
    // color space says which (erhe::usd::Usd_image::srgb).
    if (!loader.open(image.path, info, !image.srgb, transcode_format_preference)) {
        log_parsers->warn("USD image '{}' could not be decoded", image.path.generic_string());
        return {};
    }

    std::size_t byte_count = 0;
    if (erhe::dataformat::is_block_compressed(info.format) || (info.level_count > 1)) {
        byte_count = erhe::dataformat::get_mip_chain_byte_count(
            info.format,
            static_cast<std::size_t>(info.width),
            static_cast<std::size_t>(info.height),
            static_cast<std::size_t>(info.level_count)
        );
    } else {
        byte_count = static_cast<std::size_t>(info.row_stride) * static_cast<std::size_t>(info.height);
    }
    if (byte_count == 0) {
        loader.close();
        log_parsers->warn("USD image '{}' is empty", image.path.generic_string());
        return {};
    }
    std::vector<std::uint8_t> pixels;
    pixels.resize(byte_count);
    const bool loaded = loader.load(std::span<std::uint8_t>{pixels.data(), pixels.size()});
    loader.close();
    if (!loaded) {
        log_parsers->warn("USD image '{}' pixels could not be read", image.path.generic_string());
        return {};
    }

    erhe::graphics::Texture_create_info texture_create_info{
        .device      = graphics_device,
        .usage_mask  =
            erhe::graphics::Image_usage_flag_bit_mask::sampled |
            erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
        .pixelformat = info.format,
        .use_mipmaps = true,
        .width       = info.width,
        .height      = info.height,
        .depth       = info.depth,
        .level_count = info.level_count,
        .row_stride  = info.row_stride,
        .debug_label = erhe::utility::Debug_label{image.name}
    };
    const int  mipmap_count = texture_create_info.get_texture_level_count();
    const bool generate_mipmap =
        (mipmap_count != info.level_count) &&
        !erhe::dataformat::is_block_compressed(info.format);
    if (generate_mipmap) {
        texture_create_info.usage_mask |= erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        texture_create_info.level_count = mipmap_count;
    }

    std::shared_ptr<erhe::graphics::Texture> texture = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        texture_create_info
    );
    texture->set_name(image.name);
    texture->set_source_path(image.path);
    texture->set_two_component_normal(info.two_component_normal);
    image_transfer.upload(
        info,
        std::span<const std::uint8_t>{pixels.data(), pixels.size()},
        *texture.get(),
        generate_mipmap
    );
    return texture;
}

[[nodiscard]] auto get_material_texture_slot(
    erhe::primitive::Material_texture_samplers&  texture_samplers,
    const erhe::usd::Usd_material_texture_slot   slot
) -> erhe::primitive::Material_texture_sampler&
{
    switch (slot) {
        case erhe::usd::Usd_material_texture_slot::base_color:         return texture_samplers.base_color;
        case erhe::usd::Usd_material_texture_slot::metallic_roughness: return texture_samplers.metallic_roughness;
        case erhe::usd::Usd_material_texture_slot::normal:             return texture_samplers.normal;
        case erhe::usd::Usd_material_texture_slot::occlusion:          return texture_samplers.occlusion;
        case erhe::usd::Usd_material_texture_slot::emissive:           return texture_samplers.emissive;
        default:                                                       return texture_samplers.base_color;
    }
}

} // anonymous namespace

auto make_import_usd_operation(
    App_context&                       context,
    erhe::primitive::Build_info        build_info,
    const std::shared_ptr<Scene_root>& scene_root,
    const std::filesystem::path&       path
) -> std::shared_ptr<Operation>
{
    ERHE_PROFILE_FUNCTION();

    if (!scene_root || (context.graphics_device == nullptr)) {
        return {};
    }

    // The imported content hangs from an import_root node, exactly as a glTF
    // import does: an implicit container that is not file content. It is
    // parented to a temporary scene while the nodes are built (a node needs a
    // host to attach to) and detached again before the insert operation takes
    // it.
    erhe::scene::Scene temp_scene{"temp usd scene", nullptr};
    std::shared_ptr<erhe::scene::Node> root_node = std::make_shared<erhe::scene::Node>(
        erhe::file::to_string(path.filename())
    );
    root_node->enable_flag_bits(
        erhe::Item_flags::content    |
        erhe::Item_flags::show_in_ui |
        erhe::Item_flags::import_root
    );
    root_node->set_parent(temp_scene.get_root_node());

    const std::chrono::steady_clock::time_point load_start_time = std::chrono::steady_clock::now();
    erhe::usd::Usd_load_result result = erhe::usd::load_usd(
        erhe::usd::Usd_load_arguments{
            .path          = path,
            .root_node     = root_node,
            .mesh_layer_id = scene_root->layers().content()->id
        }
    );
    const std::chrono::steady_clock::duration load_duration = std::chrono::steady_clock::now() - load_start_time;
    log_parsers->info(
        "load_usd '{}': {} ms",
        erhe::file::to_string(path.filename()),
        std::chrono::duration_cast<std::chrono::milliseconds>(load_duration).count()
    );
    if (!result.error.empty()) {
        log_parsers->error("USD import '{}' failed: {}", path.generic_string(), result.error);
        root_node->set_parent({});
        return {};
    }
    erhe::usd::Usd_data& usd_data = result.data;
    root_node->set_parent({});

    // Textures. erhe::usd names image FILES (it creates no GPU object at
    // all), so this is where they become erhe::graphics::Texture objects and
    // where the material slots the loader recorded are filled.
    std::vector<std::shared_ptr<erhe::graphics::Texture>> textures;
    textures.resize(usd_data.images.size());
    {
        erhe::gltf::Image_transfer image_transfer{*context.graphics_device};
        for (std::size_t i = 0, end = usd_data.images.size(); i < end; ++i) {
            textures[i] = load_usd_image(*context.graphics_device, image_transfer, usd_data.images[i]);
        }
        image_transfer.flush();
    }
    for (const erhe::usd::Usd_material_texture_binding& binding : usd_data.material_texture_bindings) {
        if ((binding.material_index >= usd_data.materials.size()) || (binding.image_index >= textures.size())) {
            continue;
        }
        const std::shared_ptr<erhe::primitive::Material>& material = usd_data.materials[binding.material_index];
        const std::shared_ptr<erhe::graphics::Texture>&   texture  = textures[binding.image_index];
        if (!material || !texture) {
            continue;
        }
        material->set_slot_texture(get_material_texture_slot(material->data.texture_samplers, binding.slot), texture);
    }

    log_parsers->info(
        "USD import '{}': {} nodes, {} meshes, {} materials, {} textures",
        erhe::file::to_string(path.filename()),
        usd_data.nodes.size(),
        usd_data.meshes.size(),
        usd_data.materials.size(),
        usd_data.images.size()
    );

    std::vector<std::shared_ptr<erhe::Item_base>> mesh_node_items;
    finalize_imported_meshes(
        context,
        build_info,
        std::span<const std::shared_ptr<erhe::scene::Node>>{usd_data.nodes},
        &mesh_node_items
    );

    const std::string                      path_string     = path.generic_string();
    const std::shared_ptr<Content_library> content_library = scene_root->get_content_library();
    std::vector<std::shared_ptr<Operation>> operations;
    for (std::size_t i = 0, end = textures.size(); i < end; ++i) {
        if (!textures[i]) {
            continue;
        }
        operations.push_back(
            std::make_shared<Content_library_attach_operation<erhe::graphics::Texture>>(
                content_library,
                content_library->textures,
                textures[i],
                Gltf_source_reference{
                    .gltf_path  = path_string,
                    .item_name  = textures[i]->get_name(),
                    .item_index = static_cast<int>(i),
                    .item_type  = "texture",
                }
            )
        );
    }
    for (std::size_t i = 0, end = usd_data.materials.size(); i < end; ++i) {
        if (!usd_data.materials[i]) {
            continue;
        }
        operations.push_back(
            std::make_shared<Content_library_attach_operation<erhe::primitive::Material>>(
                content_library,
                content_library->materials,
                usd_data.materials[i],
                Gltf_source_reference{
                    .gltf_path  = path_string,
                    .item_name  = usd_data.materials[i]->get_name(),
                    .item_index = static_cast<int>(i),
                    .item_type  = "material",
                }
            )
        );
    }

    erhe::scene::Scene* scene = scene_root->get_hosted_scene();
    operations.push_back(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = context,
                .item    = root_node,
                .parent  = scene->get_root_node(),
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );
    operations.push_back(
        std::make_shared<Async_raytrace_kickoff_operation>(
            scene_root,
            std::move(mesh_node_items)
        )
    );

    std::shared_ptr<Compound_operation> compound = std::make_shared<Compound_operation>(
        Compound_operation::Parameters{.operations = std::move(operations)}
    );
    compound->set_description(
        fmt::format("[{}] Import USD {}", compound->get_serial(), erhe::file::to_string(path.filename()))
    );
    return compound;
}

void import_usd(
    App_context&                       context,
    erhe::primitive::Build_info        build_info,
    const std::shared_ptr<Scene_root>& scene_root,
    const std::filesystem::path&       path
)
{
    if (context.operation_stack == nullptr) {
        return;
    }
    std::shared_ptr<Operation> operation = make_import_usd_operation(context, build_info, scene_root, path);
    if (!operation) {
        return;
    }
    context.operation_stack->queue(operation);
}

} // namespace editor

#else

#include "editor_log.hpp"

namespace editor {

auto make_import_usd_operation(
    App_context&,
    erhe::primitive::Build_info,
    const std::shared_ptr<Scene_root>&,
    const std::filesystem::path& path
) -> std::shared_ptr<Operation>
{
    log_parsers->error("USD import '{}': USD support not built (ERHE_USD_LIBRARY=none)", path.generic_string());
    return {};
}

void import_usd(
    App_context&,
    erhe::primitive::Build_info,
    const std::shared_ptr<Scene_root>&,
    const std::filesystem::path& path
)
{
    log_parsers->error("USD import '{}': USD support not built (ERHE_USD_LIBRARY=none)", path.generic_string());
}

} // namespace editor

#endif
