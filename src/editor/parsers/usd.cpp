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
#include "assets/asset_manager.hpp"
#include "prefabs/prefab_library.hpp"
#include "brushes/brush.hpp"
#include "content_library/content_library.hpp"
#include "content_library/style.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "texture_graph/graph_texture.hpp"
#include "operations/async_raytrace_kickoff_operation.hpp"
#include "operations/compound_operation.hpp"
#include "operations/library_attach_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/operation_stack.hpp"
#include "parsers/gltf.hpp"
#include "scene/scene_root.hpp"

#include "app_message_bus.hpp"
#include "app_scenes.hpp"
#include "app_settings.hpp"
#include "scene/draw_list_scene_dependencies.hpp"
#include "scene/generated/scene_settings_serialization.hpp"

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
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include "editor_log.hpp"
#include "items.hpp"

#include <nlohmann/json.hpp>
#include <simdjson.h>

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

// The image files the stage names, decoded into textures, with every
// material slot the loader recorded filled in. Shared by the import and the
// open path: neither can reach a texture through erhe::usd, which creates no
// GPU object at all.
[[nodiscard]] auto create_usd_textures(
    App_context&         context,
    erhe::usd::Usd_data& usd_data
) -> std::vector<std::shared_ptr<erhe::graphics::Texture>>
{
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
    return textures;
}

// The content-library attaches for the textures and materials one USD file
// contributed, in the order the glTF import builds them.
void append_usd_content_library_operations(
    App_context&                                                 context,
    const std::shared_ptr<Content_library>&                      content_library,
    const std::vector<std::shared_ptr<erhe::graphics::Texture>>& textures,
    const erhe::usd::Usd_data&                                   usd_data,
    const std::string&                                           path_string,
    std::vector<std::shared_ptr<Operation>>&                     operations
)
{
    for (std::size_t i = 0, end = textures.size(); i < end; ++i) {
        if (!textures[i]) {
            continue;
        }
        operations.push_back(
            make_library_attach_operation(
                context,
                content_library,
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
        const std::shared_ptr<erhe::primitive::Material>& material = usd_data.materials[i];
        if (!material) {
            continue;
        }
        const Gltf_source_reference gltf_source{
            .gltf_path  = path_string,
            .item_name  = material->get_name(),
            .item_index = static_cast<int>(i),
            .item_type  = "material",
        };
        // A material the file placed is already a prim of the loaded tree
        // (doc/usd-compatibility-plan.md U4), and the tree enters the scene
        // as a whole: the material needs the library's bookkeeping, not an
        // insert of its own. Only a material the file gave no place is
        // attached, which is what creates the `Materials` kind scope.
        if (material->get_parent().lock()) {
            content_library->set_gltf_source(material, gltf_source);
            continue;
        }
        operations.push_back(
            make_library_attach_operation(context, content_library, material, gltf_source)
        );
    }
}

// The node one USD prim path names, walked down from `root` by prim name.
// Null when no such prim is in the imported tree.
[[nodiscard]] auto find_prim_node(
    const std::shared_ptr<erhe::scene::Node>& root,
    const std::string&                        prim_path
) -> std::shared_ptr<erhe::scene::Node>
{
    std::shared_ptr<erhe::Hierarchy> current = root;
    std::size_t                      position = 0;
    while ((position < prim_path.size()) && (current)) {
        while ((position < prim_path.size()) && (prim_path[position] == '/')) {
            ++position;
        }
        if (position >= prim_path.size()) {
            break;
        }
        const std::size_t separator = prim_path.find('/', position);
        const std::string element   = (separator == std::string::npos)
            ? prim_path.substr(position)
            : prim_path.substr(position, separator - position);
        position = (separator == std::string::npos) ? prim_path.size() : separator;

        std::shared_ptr<erhe::Hierarchy> next;
        for (const std::shared_ptr<erhe::Hierarchy>& child : current->get_children()) {
            if (child && (child->get_name() == element)) {
                next = child;
                break;
            }
        }
        current = next;
    }
    return std::dynamic_pointer_cast<erhe::scene::Node>(current);
}

// The file one arc names: the stage's own file for an internal reference, and
// otherwise the asset path resolved against the referencing layer's directory.
[[nodiscard]] auto resolve_reference_asset_path(
    const std::filesystem::path& source_path,
    const std::string&           asset_path
) -> std::filesystem::path
{
    if (asset_path.empty()) {
        return source_path; // internal reference: a prim of the same layer
    }
    const std::filesystem::path referenced{asset_path};
    if (referenced.is_absolute()) {
        return referenced;
    }
    return source_path.parent_path() / referenced;
}

// True when `stage_path` is `prefix` or a prim below it. An empty prefix
// accepts every prim.
[[nodiscard]] auto is_under_prim_path(const std::string& stage_path, const std::string& prefix) -> bool
{
    if (prefix.empty()) {
        return true;
    }
    if (stage_path == prefix) {
        return true;
    }
    return (stage_path.size() > prefix.size()) &&
           (stage_path.compare(0, prefix.size(), prefix) == 0) &&
           (stage_path[prefix.size()] == '/');
}

// Instantiate every composition arc the file's prims author
// (doc/usd-compatibility-plan.md X1): one Prefab_instance attachment per arc,
// in the order the arcs resolved to, each with a clone of the template the arc
// names below the carrier prim. `prim_path_prefix` limits this to one subtree,
// which is what a template load wants. Cloned meshes are pointed at
// `content_layer_id` and appended to `out_mesh_node_items` when non-null.
void resolve_usd_references(
    App_context&                                   context,
    Prefab_library&                                prefab_library,
    const erhe::usd::Usd_data&                     usd_data,
    const std::filesystem::path&                   source_path,
    const erhe::scene::Layer_id                    content_layer_id,
    std::vector<std::shared_ptr<erhe::Item_base>>* out_mesh_node_items,
    const std::string&                             prim_path_prefix
)
{
    static_cast<void>(context);
    for (const erhe::usd::Usd_prim_references& entry : usd_data.references) {
        if (!is_under_prim_path(entry.stage_path, prim_path_prefix)) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Node> carrier = std::dynamic_pointer_cast<erhe::scene::Node>(entry.item);
        if (!carrier) {
            log_parsers->warn(
                "USD prim '{}' authors composition arcs but carries no transform - the arcs are not instantiated",
                entry.stage_path
            );
            continue;
        }
        for (const erhe::usd::Usd_reference& reference : entry.references) {
            const std::filesystem::path target_path = resolve_reference_asset_path(source_path, reference.asset_path);
            const std::shared_ptr<Prefab> prefab = prefab_library.get_or_load(target_path, reference.prim_path);
            if (!prefab) {
                log_parsers->error(
                    "USD prim '{}': failed to load reference target '{}'{} (missing file, no prims, or a reference cycle - see log)",
                    entry.stage_path,
                    erhe::file::to_string(target_path),
                    reference.prim_path
                );
                continue;
            }
            attach_prefab_instance(
                prefab,
                carrier,
                content_layer_id,
                out_mesh_node_items,
                (reference.kind == erhe::usd::Usd_reference_kind::payload)
                    ? Prefab_arc_kind::payload
                    : Prefab_arc_kind::reference
            );
        }
    }
}

// The `customLayerData` key the editor's scene state travels under, and the
// key naming the writer's format revision. The value of `erhe:scene` is the
// JSON object the glTF ERHE_scene block carries, verbatim as a string:
// ambient_light, enable_physics and the codegen-serialized per-scene
// settings (doc/scene_serialization.md, USD-backed scenes).
constexpr const char* c_usd_scene_state_key = "erhe:scene";
constexpr const char* c_usd_version_key     = "erhe:version";
constexpr const char* c_usd_version_value   = "1";

// What `erhe:scene` carries, with editor defaults for everything the file
// leaves out.
class Usd_scene_state
{
public:
    glm::vec4   ambient_light {0.0f, 0.0f, 0.0f, 0.0f};
    bool        enable_physics{true};
    std::string settings_json;
};

[[nodiscard]] auto parse_usd_scene_state(const erhe::usd::Usd_data& usd_data) -> Usd_scene_state
{
    Usd_scene_state state{};
    const std::map<std::string, std::string>::const_iterator i = usd_data.custom_layer_data.find(c_usd_scene_state_key);
    if (i == usd_data.custom_layer_data.end()) {
        return state;
    }
    const nlohmann::json payload = nlohmann::json::parse(i->second, nullptr, false);
    if (!payload.is_object()) {
        log_parsers->error("open_scene_usd: customLayerData '{}' is not a JSON object - editor defaults are used", c_usd_scene_state_key);
        return state;
    }
    if (payload.contains("ambient_light") && payload["ambient_light"].is_array() && (payload["ambient_light"].size() == 4)) {
        for (int component = 0; component < 4; ++component) {
            state.ambient_light[component] = payload["ambient_light"][static_cast<std::size_t>(component)].get<float>();
        }
    }
    if (payload.contains("enable_physics") && payload["enable_physics"].is_boolean()) {
        state.enable_physics = payload["enable_physics"].get<bool>();
    }
    if (payload.contains("settings") && payload["settings"].is_object()) {
        state.settings_json = payload["settings"].dump();
    }
    return state;
}

} // anonymous namespace

auto make_import_usd_operation(
    App_context&                       context,
    erhe::primitive::Build_info        build_info,
    const std::shared_ptr<Scene_root>& scene_root,
    const std::filesystem::path&       path
) -> Usd_import_result
{
    ERHE_PROFILE_FUNCTION();

    Usd_import_result import_result{};
    if (!scene_root) {
        import_result.error = "no target scene";
        return import_result;
    }
    if (context.graphics_device == nullptr) {
        import_result.error = "no graphics device";
        return import_result;
    }

    // The imported content hangs from an import_root node, exactly as a glTF
    // import does: an implicit container that is not file content. It is
    // parented to a temporary scene while the nodes are built (a node needs a
    // host to attach to) and detached again before the insert operation takes
    // it.
    erhe::scene::Scene temp_scene{"temp usd scene", nullptr};
    std::shared_ptr<erhe::scene::Node> root_node = std::make_shared<erhe::scene::Xform>(
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
        import_result.error = result.error;
        return import_result;
    }
    erhe::usd::Usd_data& usd_data = result.data;
    root_node->set_parent({});

    // Textures. erhe::usd names image FILES (it creates no GPU object at
    // all), so this is where they become erhe::graphics::Texture objects and
    // where the material slots the loader recorded are filled.
    const std::vector<std::shared_ptr<erhe::graphics::Texture>> textures = create_usd_textures(context, usd_data);

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

    // Composition arcs: each referencing prim gets one Prefab_instance per
    // arc, with the arc's target cloned below it. The instances ride the
    // import_root insert below, so an undo of the import removes them.
    if (context.prefab_library != nullptr) {
        resolve_usd_references(
            context,
            *context.prefab_library,
            usd_data,
            path,
            scene_root->layers().content()->id,
            &mesh_node_items,
            std::string{}
        );
    }

    const std::string                      path_string     = path.generic_string();
    const std::shared_ptr<Content_library> content_library = scene_root->get_content_library();
    std::vector<std::shared_ptr<Operation>> operations;
    append_usd_content_library_operations(context, content_library, textures, usd_data, path_string, operations);

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
    import_result.operation      = compound;
    import_result.node_count     = usd_data.nodes.size();
    import_result.mesh_count     = usd_data.meshes.size();
    import_result.material_count = usd_data.materials.size();
    import_result.texture_count  = usd_data.images.size();
    return import_result;
}

auto load_usd_prefab_template(
    App_context&                 context,
    Prefab_library&              prefab_library,
    const std::filesystem::path& path,
    const std::string&           prim_path
) -> Usd_prefab_template
{
    ERHE_PROFILE_FUNCTION();

    Usd_prefab_template usd_template{};
    if (context.graphics_device == nullptr) {
        usd_template.error = "no graphics device";
        return usd_template;
    }

    // The prims are built under a container node parented to a temporary
    // scene (a node needs a host to attach to); the template subtree is taken
    // out of it below and the container is dropped.
    erhe::scene::Scene temp_scene{"temp usd prefab scene", nullptr};
    std::shared_ptr<erhe::scene::Node> container_node = std::make_shared<erhe::scene::Xform>(
        erhe::file::to_string(path.filename())
    );
    container_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    container_node->set_parent(temp_scene.get_root_node());

    erhe::usd::Usd_load_result result = erhe::usd::load_usd(
        erhe::usd::Usd_load_arguments{
            .path          = path,
            .root_node     = container_node,
            // Instances are retargeted to the destination scene's content
            // layer when the template is cloned.
            .mesh_layer_id = 0
        }
    );
    if (!result.error.empty()) {
        container_node->set_parent({});
        usd_template.error = result.error;
        return usd_template;
    }
    erhe::usd::Usd_data& usd_data = result.data;

    static_cast<void>(create_usd_textures(context, usd_data));
    finalize_imported_meshes(
        context,
        make_import_build_info(context),
        std::span<const std::shared_ptr<erhe::scene::Node>>{usd_data.nodes},
        nullptr
    );

    // Which prim the template is rooted at: the arc's prim path, else the
    // file's default prim, else the whole file.
    const std::string root_prim_path = !prim_path.empty()
        ? prim_path
        : (usd_data.default_prim.empty() ? std::string{} : ("/" + usd_data.default_prim));

    // Arcs authored inside the template subtree are instantiated the same way
    // the scene paths do it, so nested references reproduce.
    resolve_usd_references(context, prefab_library, usd_data, path, 0, nullptr, root_prim_path);

    if (root_prim_path.empty()) {
        container_node->set_parent({});
        usd_template.root      = container_node;
        usd_template.materials = std::move(usd_data.materials);
        return usd_template;
    }

    const std::shared_ptr<erhe::scene::Node> target = find_prim_node(container_node, root_prim_path);
    if (!target) {
        container_node->set_parent({});
        usd_template.error = fmt::format("prim '{}' is not in '{}'", root_prim_path, path.generic_string());
        return usd_template;
    }

    // The template root is a wrapper whose only child is the prim the arc
    // named: an instance clones the wrapper's children, so the prim itself -
    // its class, its transform and its content - rides the instance.
    std::shared_ptr<erhe::scene::Node> template_root = std::make_shared<erhe::scene::Xform>(target->get_name());
    template_root->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    template_root->set_parent(temp_scene.get_root_node());
    // set_parent preserves the world transform by rewriting the local one; a
    // template keeps the local transform it was authored with.
    const erhe::scene::Trs_transform parent_from_node = target->parent_from_node_transform();
    target->set_parent(template_root);
    target->set_parent_from_node(parent_from_node);

    template_root->set_parent({});
    container_node->set_parent({});
    usd_template.root      = template_root;
    usd_template.materials = std::move(usd_data.materials);
    return usd_template;
}

auto open_scene_usd(App_context& context, const std::filesystem::path& path) -> std::shared_ptr<Scene_root>
{
    ERHE_PROFILE_FUNCTION();

    if (context.graphics_device == nullptr) {
        log_parsers->error("open_scene_usd '{}': no graphics device", path.generic_string());
        return {};
    }

    // The prims are built under a container node parented to a temporary
    // scene (a node needs a host to attach to) and moved under the new
    // scene's root below. Unlike the import path this container is NOT an
    // import_root wrapper: the file IS the scene, so its top-level prims are
    // the scene's top-level nodes.
    erhe::scene::Scene temp_scene{"temp usd scene", nullptr};
    std::shared_ptr<erhe::scene::Node> container_node = std::make_shared<erhe::scene::Xform>(
        erhe::file::to_string(path.filename())
    );
    container_node->set_parent(temp_scene.get_root_node());

    erhe::usd::Usd_load_result result = erhe::usd::load_usd(
        erhe::usd::Usd_load_arguments{
            .path          = path,
            .root_node     = container_node,
            .mesh_layer_id = Mesh_layer_id::content
        }
    );
    if (!result.error.empty()) {
        log_parsers->error("open_scene_usd '{}' failed: {}", path.generic_string(), result.error);
        container_node->set_parent({});
        return {};
    }
    erhe::usd::Usd_data& usd_data = result.data;

    const Usd_scene_state scene_state = parse_usd_scene_state(usd_data);

    // Fresh, EMPTY content library: the file carries the scene's own
    // materials and textures, the way an erhe-authored glTF scene does.
    std::shared_ptr<Content_library> content_library = std::make_shared<Content_library>();
    const Draw_list_scene_dependencies draw_list_dependencies = make_draw_list_scene_dependencies(context);
    std::shared_ptr<Scene_root> scene_root = std::make_shared<Scene_root>(
        context.app_message_bus,
        content_library,
        erhe::file::to_string(path.stem()),
        scene_state.enable_physics,
        &draw_list_dependencies,
        make_scene_root_material_set_create_info(context, "Scene forward material set")
    );
    {
        std::error_code error_code;
        const std::filesystem::path canonical_path = std::filesystem::weakly_canonical(path, error_code);
        scene_root->set_source_path(error_code ? path : canonical_path, Scene_source_format::usd);
    }

    erhe::scene::Scene& scene = scene_root->get_scene();
    scene.ambient_light = scene_state.ambient_light;
    if (!scene_state.settings_json.empty()) {
        simdjson::ondemand::parser   settings_parser;
        simdjson::padded_string      settings_padded{scene_state.settings_json};
        simdjson::ondemand::document settings_document;
        simdjson::ondemand::object   settings_object;
        if (
            (settings_parser.iterate(settings_padded).get(settings_document) == simdjson::SUCCESS) &&
            (settings_document.get_object().get(settings_object) == simdjson::SUCCESS) &&
            (deserialize(settings_object, scene_root->get_scene_settings()) == simdjson::SUCCESS)
        ) {
            log_parsers->info("open_scene_usd: applied per-scene setting overrides");
        } else {
            log_parsers->error("open_scene_usd: failed to deserialize the customLayerData settings payload");
        }
    }

    scene_root->register_to_editor_scenes(*context.app_scenes);

    const std::vector<std::shared_ptr<erhe::graphics::Texture>> textures = create_usd_textures(context, usd_data);

    std::vector<std::shared_ptr<erhe::Item_base>> mesh_node_items;
    finalize_imported_meshes(
        context,
        make_import_build_info(context),
        std::span<const std::shared_ptr<erhe::scene::Node>>{usd_data.nodes},
        &mesh_node_items
    );

    // Composition arcs: one Prefab_instance per arc under its carrier prim,
    // before the prims move under the scene root.
    if (context.prefab_library != nullptr) {
        resolve_usd_references(
            context,
            *context.prefab_library,
            usd_data,
            path,
            scene_root->layers().content()->id,
            &mesh_node_items,
            std::string{}
        );
    }

    // The content-library attaches are undoable operations on the import
    // path; opening a scene is not undoable, so they are executed inline and
    // dropped - the same shape finish_open_scene_gltf uses.
    std::vector<std::shared_ptr<Operation>> operations;
    append_usd_content_library_operations(context, content_library, textures, usd_data, path.generic_string(), operations);
    for (const std::shared_ptr<Operation>& operation : operations) {
        operation->execute(context);
    }

    // The file's top-level prims become the scene's top-level nodes: no
    // wrapper is added, so a save writes back exactly the shape that was
    // read. Copy the child list - reparenting mutates it.
    const std::shared_ptr<erhe::scene::Node> scene_root_node = scene.get_root_node();
    const std::vector<std::shared_ptr<erhe::Hierarchy>> children = container_node->get_children();
    for (const std::shared_ptr<erhe::Hierarchy>& child : children) {
        child->set_parent(scene_root_node);
    }
    container_node->set_parent({});

    Async_raytrace_kickoff_operation raytrace_kickoff{scene_root, std::move(mesh_node_items)};
    raytrace_kickoff.execute(context);

    log_parsers->info(
        "open_scene_usd: opened scene '{}' from '{}': {} nodes, {} meshes, {} materials, {} textures",
        scene_root->get_name(),
        erhe::file::to_string(path),
        usd_data.nodes.size(),
        usd_data.meshes.size(),
        usd_data.materials.size(),
        usd_data.images.size()
    );
    return scene_root;
}

namespace {

// The five erhe texture slots in the order erhe::usd names them, so the save
// can walk a material's slots and the writer's slot enumeration together.
class Usd_save_slot
{
public:
    const erhe::primitive::Material_texture_sampler erhe::primitive::Material_texture_samplers::* member;
    erhe::usd::Usd_material_texture_slot                                                          slot;
    const char*                                                                                   name;
};

constexpr Usd_save_slot c_usd_save_slots[] = {
    {&erhe::primitive::Material_texture_samplers::base_color,         erhe::usd::Usd_material_texture_slot::base_color,         "base_color"},
    {&erhe::primitive::Material_texture_samplers::metallic_roughness, erhe::usd::Usd_material_texture_slot::metallic_roughness, "metallic_roughness"},
    {&erhe::primitive::Material_texture_samplers::normal,             erhe::usd::Usd_material_texture_slot::normal,             "normal"},
    {&erhe::primitive::Material_texture_samplers::occlusion,          erhe::usd::Usd_material_texture_slot::occlusion,          "occlusion"},
    {&erhe::primitive::Material_texture_samplers::emissive,           erhe::usd::Usd_material_texture_slot::emissive,           "emissive"}
};

// The editor-state kinds a USD file does not carry yet. Each is reported
// once per save, so a scene that holds any of them says what the written
// file leaves behind (src/erhe/usd/notes.md future work).
template <typename T>
void log_uncarried_editor_state_kind(
    const Content_library&       content_library,
    const char*                  kind,
    const std::filesystem::path& path
)
{
    const std::size_t count = content_library.get_all<T>().size();
    if (count > 0) {
        log_parsers->info(
            "save_scene_usd '{}': {} {}(s) are not carried by a USD file yet",
            erhe::file::to_string(path), count, kind
        );
    }
}

void log_uncarried_editor_state(const Content_library& content_library, const std::filesystem::path& path)
{
    log_uncarried_editor_state_kind<Brush>        (content_library, "brush",              path);
    log_uncarried_editor_state_kind<Graph_mesh>   (content_library, "node graph mesh",    path);
    log_uncarried_editor_state_kind<Graph_texture>(content_library, "node graph texture", path);
    log_uncarried_editor_state_kind<Style>        (content_library, "style",              path);
    // A folder is a Scope of the scene tree, so it is written when it holds a
    // resource the file carries (today a material); a folder holding only
    // kinds listed above, or nothing at all, is left out
    // (src/erhe/usd/notes.md, Export).
    log_parsers->info(
        "save_scene_usd '{}': a content-library folder holding no material is not carried by a USD file yet",
        erhe::file::to_string(path)
    );
}

// The composition arcs the scene carries, one entry per carrier prim: a node
// with Prefab_instance attachments is a referencing prim, and each attachment
// is one arc, in the order the attachments hold (doc/usd-compatibility-plan.md
// X1). The writer needs no prefab library - the attachment already names the
// target file, the target prim and the arc form.
void collect_usd_references(
    const std::shared_ptr<erhe::Hierarchy>&                 prim,
    std::vector<erhe::usd::Usd_save_prim_references>&       out_references
)
{
    const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(prim);
    if (node) {
        erhe::usd::Usd_save_prim_references entry{};
        for (const std::shared_ptr<erhe::scene::Node_attachment>& attachment : node->get_attachments()) {
            const std::shared_ptr<Prefab_instance> prefab_instance = std::dynamic_pointer_cast<Prefab_instance>(attachment);
            if (!prefab_instance) {
                continue;
            }
            entry.references.push_back(
                erhe::usd::Usd_save_reference{
                    .source_path = prefab_instance->get_prefab_source_path(),
                    .prim_path   = prefab_instance->get_prefab_prim_path(),
                    .kind        = (prefab_instance->get_prefab_arc_kind() == Prefab_arc_kind::payload)
                        ? erhe::usd::Usd_reference_kind::payload
                        : erhe::usd::Usd_reference_kind::reference
                }
            );
        }
        if (!entry.references.empty()) {
            entry.item = node;
            out_references.push_back(std::move(entry));
            return; // the prims below are instance content the arcs supply
        }
    }
    for (const std::shared_ptr<erhe::Hierarchy>& child : prim->get_children()) {
        collect_usd_references(child, out_references);
    }
}

} // anonymous namespace

auto save_scene_usd(App_context& context, Scene_root& scene_root, const std::filesystem::path& path) -> bool
{
    ERHE_PROFILE_FUNCTION();

    const erhe::scene::Scene&                scene     = scene_root.get_scene();
    const std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
    if (!root_node) {
        log_parsers->error("save_scene_usd: scene '{}' has no root node", scene_root.get_name());
        return false;
    }

    erhe::usd::Usd_save_arguments save_arguments{
        .path      = path,
        .root_node = root_node
    };

    for (const std::shared_ptr<erhe::Hierarchy>& child : root_node->get_children()) {
        collect_usd_references(child, save_arguments.references);
    }

    const std::shared_ptr<Content_library> content_library = scene_root.get_content_library();
    if (content_library) {
        save_arguments.materials = content_library->get_all<erhe::primitive::Material>();
    }
    for (std::size_t material_index = 0, end = save_arguments.materials.size(); material_index < end; ++material_index) {
        const std::shared_ptr<erhe::primitive::Material>& material = save_arguments.materials[material_index];
        if (!material) {
            continue;
        }
        for (const Usd_save_slot& slot : c_usd_save_slots) {
            const erhe::primitive::Material_texture_sampler& sampler = material->data.texture_samplers.*(slot.member);
            if (!sampler.texture_reference) {
                continue;
            }
            const erhe::graphics::Texture* texture = sampler.texture_reference->get_referenced_texture();
            const std::filesystem::path*   source  = (texture != nullptr) ? texture->get_source_path() : nullptr;
            if ((source == nullptr) || source->empty()) {
                // A generated texture has no bytes on disk, so USD has no
                // asset path to name (src/erhe/usd/notes.md).
                log_parsers->warn(
                    "save_scene_usd '{}': material '{}' slot '{}' has no source image file - the slot is not written",
                    erhe::file::to_string(path), material->get_name(), slot.name
                );
                continue;
            }
            save_arguments.textures.push_back(
                erhe::usd::Usd_save_texture{
                    .material_index = material_index,
                    .slot           = slot.slot,
                    .path           = *source,
                    // A normal or occlusion map carries data, everything else
                    // carries sRGB color - the same rule the load applies.
                    .srgb           =
                        (slot.slot != erhe::usd::Usd_material_texture_slot::normal) &&
                        (slot.slot != erhe::usd::Usd_material_texture_slot::occlusion)
                }
            );
        }
    }

    // The editor's scene state, in the JSON shape the glTF ERHE_scene block
    // carries, as one `customLayerData` string.
    {
        nlohmann::json scene_json{
            {"ambient_light",  {scene.ambient_light.x, scene.ambient_light.y, scene.ambient_light.z, scene.ambient_light.w}},
            {"enable_physics", scene_root.has_physics_world()}
        };
        const Scene_settings& scene_settings = scene_root.get_scene_settings();
        if (!is_default(scene_settings)) {
            const nlohmann::json settings_json = nlohmann::json::parse(serialize(scene_settings, 0), nullptr, false);
            if (!settings_json.is_discarded()) {
                scene_json["settings"] = settings_json;
            } else {
                log_parsers->error("save_scene_usd: Scene_settings serialization did not parse - settings not written");
            }
        }
        save_arguments.custom_layer_data[c_usd_scene_state_key] = scene_json.dump();
        save_arguments.custom_layer_data[c_usd_version_key]     = c_usd_version_value;
    }

    if (content_library) {
        log_uncarried_editor_state(*content_library.get(), path);
    }

    const erhe::usd::Usd_save_result result = erhe::usd::save_usda(save_arguments);
    if (!result.warning.empty()) {
        log_parsers->warn("save_scene_usd '{}': {}", erhe::file::to_string(path), result.warning);
    }
    if (!result.error.empty()) {
        log_parsers->error("save_scene_usd '{}' failed: {}", erhe::file::to_string(path), result.error);
        return false;
    }

    // Same post-save bookkeeping the glTF save does, minus the prefab reload
    // (a USD file is not a prefab source).
    if (context.asset_manager != nullptr) {
        context.asset_manager->on_scene_saved(scene_root);
    }
    context.app_message_bus->scene_saved.send_message(Scene_saved_message{.path = path});
    log_parsers->info("save_scene_usd: scene '{}' saved to '{}'", scene_root.get_name(), erhe::file::to_string(path));
    return true;
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
    const Usd_import_result import_result = make_import_usd_operation(context, build_info, scene_root, path);
    if (!import_result.operation) {
        return; // make_import_usd_operation logged the reason
    }
    context.operation_stack->queue(import_result.operation);
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
) -> Usd_import_result
{
    log_parsers->error("USD import '{}': USD support not built (ERHE_USD_LIBRARY=none)", path.generic_string());
    Usd_import_result import_result{};
    import_result.error = "USD support not built (ERHE_USD_LIBRARY=none)";
    return import_result;
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

auto open_scene_usd(App_context&, const std::filesystem::path& path) -> std::shared_ptr<Scene_root>
{
    log_parsers->error("open_scene_usd '{}': USD support not built (ERHE_USD_LIBRARY=none)", path.generic_string());
    return {};
}

auto save_scene_usd(App_context&, Scene_root&, const std::filesystem::path& path) -> bool
{
    log_parsers->error("save_scene_usd '{}': USD support not built (ERHE_USD_LIBRARY=none)", path.generic_string());
    return false;
}

auto load_usd_prefab_template(
    App_context&,
    Prefab_library&,
    const std::filesystem::path& path,
    const std::string&
) -> Usd_prefab_template
{
    log_parsers->error("USD prefab template '{}': USD support not built (ERHE_USD_LIBRARY=none)", path.generic_string());
    Usd_prefab_template usd_template{};
    usd_template.error = "USD support not built (ERHE_USD_LIBRARY=none)";
    return usd_template;
}

} // namespace editor

#endif
