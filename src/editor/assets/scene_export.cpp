#include "assets/scene_export.hpp"

#include "app_context.hpp"
#include "assets/asset_manager.hpp"
#include "assets/asset_paths.hpp"
#include "assets/editor_asset.hpp"
#include "content_library/content_library.hpp"
#include "prefabs/instance_structure.hpp"
#include "prefabs/prefab_library.hpp"
#include "scene/scene_settings_resolve.hpp"
#include "scene/generated/scene_settings.hpp"
#include "scene/generated/scene_settings_serialization.hpp"
#include "config/generated/scene_export_settings_serialization.hpp"
#include "assets/item_keys.hpp"
#include "assets/local_mesh_items.hpp"
#include "assets/prepared_asset.hpp"
#include "editor_log.hpp"
#include "parsers/gltf.hpp"
#include "assets/load_memory_budget.hpp"
#include "config/generated/base_variant_load.hpp"
#include "config/generated/scene_export_settings.hpp"
#include "config/generated/texture_encoding.hpp"
#include "scene/scene_root.hpp"
#include "scene/world_space_form.hpp"
#include "renderers/pvs_baker.hpp"

#include "erhe_asset_store/asset_store.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_file/file.hpp"
#include "erhe_item/item.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_gltf/gltf_fastgltf.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ktx2_container.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/prepared_primitive.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_raytrace/prebuilt_bvh.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene/pvs_database.hpp"
#include "erhe_task/task.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <chrono>
#include <fstream>
#include <functional>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace editor {

using erhe::asset_store::Asset_store;
using erhe::asset_store::Item_kind;
using erhe::asset_store::Key;

auto c_str(const Scene_export_stage stage) -> const char*
{
    switch (stage) {
        case Scene_export_stage::mesh_keys:  return "computing mesh keys";
        case Scene_export_stage::mesh_items: return "making mesh items";
        case Scene_export_stage::texture_keys:  return "computing texture keys";
        case Scene_export_stage::texture_items: return "making texture items";
        case Scene_export_stage::basis_texture_items: return "encoding basis texture items";
        case Scene_export_stage::scene_item: return "writing scene item";
        case Scene_export_stage::copy:       return "copying items";
        case Scene_export_stage::check_items: return "checking items";
        case Scene_export_stage::drop_items: return "dropping items";
        case Scene_export_stage::world_mesh_items: return "making world-space mesh items";
        case Scene_export_stage::world_form:       return "loading the world-space form";
        case Scene_export_stage::pvs_bake:         return "baking the PVS";
        default:                             return "?";
    }
}

namespace {

// One primitive shape of the export: a soup, or a geometry-normative shape
// whose item is made from its derived triangles.
class Export_shape
{
public:
    erhe::primitive::Primitive_render_shape*        shape{nullptr};
    std::optional<erhe::primitive::Lod_settings>    lod_settings;
    Local_mesh_source                               source{Local_mesh_source::soup};
    erhe::geometry::Geometry*                       geometry{nullptr}; // derived_triangles: the Geometry built from
    // Keeps the shape alive while a key check runs beside the live scene.
    std::shared_ptr<erhe::primitive::Primitive>     primitive;
    bool                                            key_known{false};
    bool                                            hash_pending{false}; // key check: a derived content hash not computed yet (a status job derives it)
    bool                                            hash_computed{false}; // this job computed the content hash the shape lacked
    Key                                             key;
    std::string                                     meta;
    Key                                             bvh_key; // the Raytrace BVH item of `key`
    std::string                                     bvh_meta;
    bool                                            exported{true}; // false: the exporter writes nothing for it (no facets)
    bool                                            unbaked_user{false}; // a mesh the world-space bake does not admit draws it: it needs a Local-space mesh item
    bool                                            has_item{false};
    bool                                            has_bvh_item{false};
    std::string                                     skip_reason;
    std::size_t                                     file_index{0}; // R10.2: the source file whose editor asset holds its items
};

// One primitive of a mesh the world-space bake admits: its World-space mesh
// item (R4.7), keyed by its shape's Local-space mesh key and its transform.
class Export_world_primitive
{
public:
    const erhe::scene::Mesh* mesh{nullptr};
    std::size_t              primitive_index{0};
    std::size_t              shape_index{0};       // into Impl::shapes
    glm::mat4                world_from_node{1.0f}; // relative to the scene root (Gltf_export_world_bake)
    std::string              node_record;           // R4.8: {"path":[names],"uids":[uids]}
    bool                     key_known{false};
    Key                      key;
    std::string              meta;
    Key                      bvh_key;
    std::string              bvh_meta;
    bool                     has_item{false};
    bool                     has_bvh_item{false};
};

// R4.8: the node's names and uids from the scene root down to it.
[[nodiscard]] auto make_node_record(const erhe::scene::Node& node) -> std::string
{
    std::vector<const erhe::scene::Node*> chain;
    std::shared_ptr<erhe::scene::Node> holder;
    for (const erhe::scene::Node* current = &node; current != nullptr; current = holder.get()) {
        holder = current->get_parent_node();
        if (!holder) {
            break; // the scene root itself is not named
        }
        chain.push_back(current);
    }
    nlohmann::json path = nlohmann::json::array();
    nlohmann::json uids = nlohmann::json::array();
    for (auto i = chain.rbegin(); i != chain.rend(); ++i) {
        path.push_back((*i)->get_name());
        uids.push_back((*i)->get_gltf_uid());
    }
    return nlohmann::json{{"path", path}, {"uids", uids}}.dump();
}

// One texture the exported materials use: a Basis Universal or PNG / JPEG
// source has a Texture item (doc/editor/prepared_assets.md "Export"); any
// other image is written inside the Scene item.
class Export_texture
{
public:
    const erhe::graphics::Texture*                       texture{nullptr};
    std::shared_ptr<const erhe::gltf::Gltf_image_source> source;
    erhe::graphics::Texture_properties                   properties;
    bool                                                 keyed{false}; // a Basis Universal or PNG / JPEG source: it has a Texture key
    Texture_item_source                                  item_source{Texture_item_source::basis_source};
    Key                                                  key;
    Key                                                  basis_key; // basis_texture_item: its Basis texture item
    std::string                                          meta;
    bool                                                 has_item{false};
    std::string                                          skip_reason;
    std::size_t                                          file_index{0}; // R10.2: the file that defines its material
};

[[nodiscard]] auto relative_path_text(const std::filesystem::path& path, const std::filesystem::path& base_directory) -> std::string
{
    std::error_code error_code;
    const std::filesystem::path relative = std::filesystem::relative(path, base_directory, error_code);
    if (error_code || relative.empty()) {
        return path.generic_string();
    }
    return relative.generic_string();
}

// The file index of a prefab source at a nesting depth (R10.1).
using Export_file_of = std::function<std::size_t(const std::filesystem::path& source_path, std::size_t depth)>;

// The primitive shapes the exporter visits (its process_child_nodes() walk);
// file_index is the file whose content the subtree is: the nearest instance arc above it, else the scene.
void collect_export_shapes(
    const erhe::Hierarchy&                                                    node,
    const erhe::gltf::Gltf_export_arguments&                                  arguments,
    const Export_file_of&                                                     file_of,
    const std::size_t                                                         file_index,
    const std::size_t                                                         depth,
    std::vector<Export_shape>&                                                out_shapes,
    std::unordered_map<const erhe::primitive::Primitive_shape*, std::size_t>& shape_indices,
    std::vector<Export_world_primitive>&                                      out_world_primitives,
    std::vector<const erhe::primitive::Material*>&                            out_materials,
    std::vector<std::size_t>&                                                 out_material_files,
    std::unordered_set<const erhe::primitive::Material*>&                     material_set
)
{
    for (const std::shared_ptr<erhe::Hierarchy>& child : node.get_children()) {
        // The exporter's process_child_nodes() skips these with their subtrees.
        const erhe::Typed* const prim = dynamic_cast<const erhe::Typed*>(child.get());
        if (prim == nullptr) {
            continue;
        }
        const uint64_t flag_bits = prim->get_flag_bits();
        if (
            ((flag_bits & erhe::Item_flags::session_only) != 0) ||
            ((prim->get_type() & arguments.excluded_item_type_bits) != 0) ||
            ((flag_bits & erhe::Item_flags::render_proxy) != 0) ||
            (((flag_bits & erhe::Item_flags::content) == 0) && ((flag_bits & erhe::Item_flags::import_root) == 0))
        ) {
            continue;
        }
        const erhe::scene::Mesh* const mesh = dynamic_cast<const erhe::scene::Mesh*>(child.get());
        if (
            (mesh != nullptr) &&
            (arguments.excluded_meshes.contains(mesh) || ((mesh->get_flag_bits() & arguments.excluded_item_flag_bits) != 0))
        ) {
            continue;
        }
        if (mesh != nullptr) {
            const glm::mat4* world_from_node = nullptr;
            if (arguments.world_bake != nullptr) {
                const auto baked = arguments.world_bake->baked_meshes.find(mesh);
                world_from_node = (baked != arguments.world_bake->baked_meshes.end()) ? &baked->second : nullptr;
            }
            const std::string node_record = (world_from_node != nullptr) ? make_node_record(*mesh) : std::string{};
            const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_primitives();
            for (std::size_t primitive_index = 0, end = mesh_primitives.size(); primitive_index < end; ++primitive_index) {
                const erhe::scene::Mesh_primitive& mesh_primitive = mesh_primitives[primitive_index];
                if (mesh_primitive.material && material_set.insert(mesh_primitive.material.get()).second) {
                    out_materials.push_back(mesh_primitive.material.get());
                    out_material_files.push_back(file_index);
                }
                erhe::primitive::Primitive_render_shape* const render_shape =
                    mesh_primitive.primitive ? mesh_primitive.primitive->render_shape.get() : nullptr;
                if (render_shape == nullptr) {
                    continue;
                }
                const auto [i, inserted] = shape_indices.try_emplace(render_shape, out_shapes.size());
                if (inserted) {
                    Export_shape entry{};
                    entry.shape        = render_shape;
                    entry.primitive    = mesh_primitive.primitive;
                    entry.lod_settings = mesh_primitive.primitive->lod_settings;
                    entry.file_index   = file_index;
                    out_shapes.push_back(std::move(entry));
                } else if (!out_shapes[i->second].lod_settings.has_value()) {
                    out_shapes[i->second].lod_settings = mesh_primitive.primitive->lod_settings;
                }
                if (world_from_node == nullptr) {
                    out_shapes[i->second].unbaked_user = true;
                } else {
                    Export_world_primitive world_primitive{};
                    world_primitive.mesh            = mesh;
                    world_primitive.primitive_index = primitive_index;
                    world_primitive.shape_index     = i->second;
                    world_primitive.world_from_node = *world_from_node;
                    world_primitive.node_record     = node_record;
                    out_world_primitives.push_back(std::move(world_primitive));
                }
            }
        }
        // A carrier's children are the content of its arc's source file (doc/plans/gltf_prefabs.md).
        const erhe::Composition_arc* const arc = get_first_instance_arc(*child);
        const std::size_t child_depth = (arc != nullptr) ? (depth + 1) : depth;
        const std::size_t child_file  = (arc != nullptr) ? file_of(arc->source_path, child_depth) : file_index;
        collect_export_shapes(
            *child, arguments, file_of, child_file, child_depth, out_shapes, shape_indices, out_world_primitives,
            out_materials, out_material_files, material_set
        );
    }
}

// The textures of the materials' five slots, each once, in slot order; a texture is in its material's file.
void collect_export_textures(
    const std::vector<const erhe::primitive::Material*>&                     materials,
    const std::vector<std::size_t>&                                          material_files,
    std::vector<Export_texture>&                                             out_textures,
    std::unordered_map<const erhe::graphics::Texture*, std::size_t>&         texture_indices
)
{
    for (std::size_t m = 0, end = materials.size(); m < end; ++m) {
        const erhe::primitive::Material* const material = materials[m];
        const erhe::primitive::Material_texture_samplers& slots = material->get_data().texture_samplers;
        for (const erhe::primitive::Material_texture_sampler* const slot : {&slots.base_color, &slots.metallic_roughness, &slots.normal, &slots.occlusion, &slots.emissive}) {
            if (!slot->texture_reference) {
                continue;
            }
            const erhe::graphics::Texture* const texture = slot->texture_reference->get_referenced_texture();
            if ((texture == nullptr) || !texture_indices.try_emplace(texture, out_textures.size()).second) {
                continue;
            }
            out_textures.push_back(Export_texture{.texture = texture, .properties = texture->get_texture_properties(), .file_index = material_files[m]});
        }
    }
}

// The payload of one editor asset item as a glTF buffer: the world-space form
// reads its World-space mesh items through it, for the editor's lifetime.
class Editor_asset_item_reader : public erhe::gltf::Gltf_buffer_reader
{
public:
    Editor_asset_item_reader(std::shared_ptr<Editor_asset> editor_asset, const Key& key)
        : m_editor_asset{std::move(editor_asset)}
        , m_key         {key}
    {
    }

    [[nodiscard]] auto read_at(const std::uint64_t offset, const std::span<std::byte> out) const -> bool override
    {
        return m_editor_asset->get_store().read_payload(m_key, offset, out);
    }

    void close() const override
    {
        m_editor_asset->get_store().close_payload_handles();
    }

private:
    std::shared_ptr<Editor_asset> m_editor_asset;
    Key                           m_key;
};

// One source file of the export (R10.1) and its editor asset.
class Export_file
{
public:
    std::filesystem::path                      source_path; // normalized
    Export_file_role                           role {Export_file_role::scene};
    std::size_t                                depth{0};
    std::shared_ptr<Editor_asset>              editor_asset;
    std::map<Item_kind, Export_kind_counts>    counts;
};

// What a source file states about itself in ERHE_scene: its editor asset (R2.2), its scene id and export settings (R10.5).
class Source_file_state
{
public:
    std::filesystem::path                record_path; // absolute; empty for none
    std::string                          scene_id;
    std::optional<Scene_export_settings> export_settings;
};

// The glTF JSON of a .glb (its JSON chunk) or a .gltf file; nullopt when the file cannot be read.
[[nodiscard]] auto read_gltf_json(const std::filesystem::path& path) -> std::optional<std::string>
{
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return std::nullopt;
    }
    char header[20] = {};
    stream.read(header, sizeof(header));
    const std::streamsize header_size = stream.gcount();
    if ((header_size == static_cast<std::streamsize>(sizeof(header))) && (std::string_view{header, 4} == "glTF")) {
        std::uint32_t chunk_length = 0;
        std::memcpy(&chunk_length, header + 12, sizeof(chunk_length));
        if (std::string_view{header + 16, 4} != "JSON") {
            return std::nullopt;
        }
        std::string json(chunk_length, '\0');
        stream.read(json.data(), static_cast<std::streamsize>(chunk_length));
        if (stream.gcount() != static_cast<std::streamsize>(chunk_length)) {
            return std::nullopt;
        }
        return json;
    }
    std::string json{header, static_cast<std::size_t>(std::max<std::streamsize>(header_size, 0))};
    json.append(std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{});
    if (stream.bad() || json.empty()) {
        return std::nullopt;
    }
    return json;
}

[[nodiscard]] auto read_source_file_state(const std::filesystem::path& path) -> std::optional<Source_file_state>
{
    const std::optional<std::string> json = read_gltf_json(path);
    if (!json.has_value()) {
        return std::nullopt;
    }
    Source_file_state state{};
    const nlohmann::json document = nlohmann::json::parse(json.value(), nullptr, false);
    if (!document.is_object()) {
        return std::nullopt;
    }
    const std::size_t scene_index = (document.contains("scene") && document["scene"].is_number_unsigned()) ? document["scene"].get<std::size_t>() : 0;
    if (!document.contains("scenes") || !document["scenes"].is_array() || (scene_index >= document["scenes"].size())) {
        return state;
    }
    const nlohmann::json& scene = document["scenes"][scene_index];
    if (!scene.contains("extensions") || !scene["extensions"].contains("ERHE_scene")) {
        return state; // a foreign glTF states nothing
    }
    const nlohmann::json& erhe_scene = scene["extensions"]["ERHE_scene"];
    if (erhe_scene.contains("editor_asset") && erhe_scene["editor_asset"].is_string()) {
        state.record_path = normalize_asset_path(path.parent_path() / erhe_scene["editor_asset"].get<std::string>());
    }
    if (erhe_scene.contains("settings") && erhe_scene["settings"].is_object()) {
        nlohmann::json settings_json = erhe_scene["settings"];
        migrate_scene_settings_json(settings_json);
        const std::string text = settings_json.dump();
        simdjson::ondemand::parser   parser;
        simdjson::padded_string      padded{text};
        simdjson::ondemand::document settings_document;
        simdjson::ondemand::object   settings_object;
        Scene_settings               settings{};
        if (
            (parser.iterate(padded).get(settings_document) == simdjson::SUCCESS) &&
            (settings_document.get_object().get(settings_object) == simdjson::SUCCESS) &&
            (deserialize(settings_object, settings) == simdjson::SUCCESS)
        ) {
            state.scene_id        = settings.scene_id;
            state.export_settings = settings.export_settings;
        }
    }
    return state;
}

template <typename F>
void run_parallel(tf::Executor* const executor, const std::size_t count, F&& task)
{
    if ((executor != nullptr) && (count > 1)) {
        tf::Taskflow taskflow;
        for (std::size_t i = 0; i < count; ++i) {
            taskflow.emplace([&task, i]() { task(i); });
        }
        erhe::task::run(*executor, taskflow).wait();
    } else {
        for (std::size_t i = 0; i < count; ++i) {
            task(i);
        }
    }
}

} // anonymous namespace

auto c_str(const Export_file_role role) -> const char*
{
    switch (role) {
        case Export_file_role::scene:              return "scene";
        case Export_file_role::prefab:             return "prefab";
        case Export_file_role::material_container: return "material_container";
        default:                                   return "?";
    }
}

auto get_target_scene_item_textures(const std::string_view target) -> std::optional<Scene_item_textures>
{
    // pc keeps the source images (doc/plans/prepared_assets_design.md D7).
    if (target == "pc") {
        return Scene_item_textures::source;
    }
    if (target == "quest") {
        return Scene_item_textures::astc_4x4;
    }
    return std::nullopt;
}

auto make_running_mesh_stamp(const erhe::gltf::Gltf_gpu_vertex_formats* const gpu_vertex_formats) -> std::string
{
    return make_local_mesh_stamp(make_vertex_layout_signature(gpu_vertex_formats));
}

auto make_running_bvh_stamp() -> std::string
{
    return make_raytrace_bvh_stamp(erhe::raytrace::get_prebuilt_bvh_backend(), erhe::raytrace::get_prebuilt_bvh_library());
}

auto get_prepared_build_formats(const App_context& context) -> Prepared_build_formats
{
    Prepared_build_formats build{};
#if defined(__ANDROID__)
    build.target = "quest";
#else
    build.target = "pc";
#endif
    std::optional<erhe::gltf::Gltf_gpu_vertex_formats> gpu_vertex_formats;
    if (context.mesh_memory != nullptr) {
        gpu_vertex_formats = make_gltf_gpu_vertex_formats(*context.mesh_memory);
    }
    build.mesh_stamp = make_running_mesh_stamp(gpu_vertex_formats.has_value() ? &gpu_vertex_formats.value() : nullptr);
    build.bvh_stamp  = make_running_bvh_stamp();
    // Source images and raw ASTC 4x4 (decoded where not sampled) read everywhere; raw BC where sampled.
    build.scene_stamps.push_back(make_scene_item_stamp(Scene_item_textures::source));
    build.scene_stamps.push_back(make_scene_item_stamp(Scene_item_textures::astc_4x4));
    if (context.graphics_device != nullptr) {
        const erhe::graphics::Device& device = *context.graphics_device;
        if (
            device.get_format_properties(erhe::dataformat::Format::format_bc7_srgb).supported &&
            device.get_format_properties(erhe::dataformat::Format::format_bc7_unorm).supported &&
            device.get_format_properties(erhe::dataformat::Format::format_bc5_unorm).supported
        ) {
            build.scene_stamps.push_back(make_scene_item_stamp(Scene_item_textures::bc));
            build.texture_stamps.push_back(make_texture_stamp(erhe::graphics::Ktx2_block_target::bc));
        }
    }
    build.texture_stamps.push_back(make_texture_stamp(erhe::graphics::Ktx2_block_target::astc_4x4));
    // PNG / JPEG images held as they are (texture_encoding source) decode everywhere.
    build.texture_stamps.push_back(make_source_texture_stamp());
    return build;
}

// Where an Export stands between its worker runs.
enum class Export_phase : unsigned int
{
    ensure = 0,     // worker: the mesh, World-space mesh and Texture items (and the form for pvs_mode rebake)
    pvs_bake,       // main thread: the PVS bake on the world-space form
    scene_and_copy  // worker: the Scene item, then Copy
};

class Scene_export::Impl
{
public:
    explicit Impl(Scene_root& scene_root_in)
        : scene_root{scene_root_in}
    {
    }

    Scene_root&                                                              scene_root;
    std::string                                                              scene_name; // read at begin: a key check runs beside the live scene
    Scene_export_mode                                                        mode{Scene_export_mode::export_items};
    Prepared_drop                                                            drop{Prepared_drop::none};
    std::string                                                              target;
    std::string                                                              scene_id;
    std::filesystem::path                                                    source_path;
    std::filesystem::path                                                    prepared_asset_path;
    std::shared_ptr<Editor_asset>                                            editor_asset; // the scene's (files[0])
    // R10.1: the scene (index 0), its prefab sources and material containers, and the Ensure order (R10.7).
    std::vector<Export_file>                                                 files;
    std::map<std::filesystem::path, std::size_t>                             file_indices;
    std::vector<std::size_t>                                                 ensure_order;
    std::vector<std::string>                                                 unresolved; // R10.8: content the open scene lacks
    Scene_export_settings                                                    settings;
    Editor_asset_registry*                                                   editor_asset_registry{nullptr};
    std::string                                                              error;
    std::unique_ptr<Gltf_scene_save>                                         save;
    tf::Executor*                                                            executor{nullptr};
    const erhe::gltf::Gltf_gpu_vertex_formats*                               gpu_vertex_formats{nullptr};
    std::string                                                              mesh_stamp;
    std::string                                                              bvh_stamp;
    std::string                                                              scene_stamp;
    // Scene_export_settings::raytrace_bvh, where the raytrace backend exports a BVH: each mesh item has a BVH item.
    bool                                                                     bvh_items{true};
    std::vector<Export_shape>                                                shapes;
    std::unordered_map<const erhe::primitive::Primitive_shape*, std::size_t> shape_indices;
    std::map<Key, std::vector<std::size_t>>                                  shapes_by_key; // the shapes of each mesh key
    // The world-space bake (R4.6): its primitives, by (mesh, primitive index) and by key.
    std::vector<Export_world_primitive>                                      world_primitives;
    std::map<std::pair<const erhe::scene::Mesh*, std::size_t>, std::size_t> world_primitive_indices;
    std::map<Key, std::vector<std::size_t>>                                  world_primitives_by_key;
    // Bake World-Space Meshes: the admitted meshes and what loading their form needs.
    std::vector<std::shared_ptr<erhe::scene::Mesh>>                          bake_meshes;
    std::optional<erhe::primitive::Build_info>                               form_build_info;
    std::optional<erhe::primitive::Build_info>                               form_skinned_build_info;
    erhe::graphics::Device*                                                  graphics_device{nullptr};
    erhe::scene_renderer::Mesh_memory*                                       mesh_memory{nullptr};
    erhe::gltf::Gltf_device_options                                          form_device_options{};
    erhe::gltf::Triangle_soup_reload                                         form_triangle_soup_reload{erhe::gltf::Triangle_soup_reload::none};
    Base_variant_load                                                        form_base_variant{};
    erhe::gltf::Gltf_data                                                    form_gltf_data; // keeps the form's primitives alive until finish
    std::vector<World_space_form_mesh>                                       form_meshes;
    std::uint64_t                                                            form_ticket{0};
    std::vector<Export_texture>                                              textures;
    std::unordered_map<const erhe::graphics::Texture*, std::size_t>         texture_indices;
    std::map<Key, std::vector<std::size_t>>                                  textures_by_key; // the textures of each texture key
    erhe::graphics::Ktx2_block_target                                        block_target{erhe::graphics::Ktx2_block_target::bc};
    erhe::graphics::Ktx2_supercompression                                    supercompression{erhe::graphics::Ktx2_supercompression::zstd};
    std::string                                                              texture_stamp;
    // R7.5: what the Texture item of a PNG / JPEG image holds, and the Basis texture items it is made from.
    Texture_encoding                                                         texture_encoding{Texture_encoding::target};
    erhe::graphics::Basis_encode_settings                                    basis_settings{};
    std::string                                                              basis_stamp;
    std::map<Key, std::vector<std::size_t>>                                  textures_by_basis_key;
    // One build of a Geometry at a time: the content build writes its facet ids.
    std::unordered_map<const erhe::geometry::Geometry*, std::unique_ptr<std::mutex>> geometry_mutexes;
    Scene_export_report                                                      report;
    Prepared_status_report                                                   status;
    Pvs_mode                                                                 pvs_mode{Pvs_mode::off};
    // The world-space form is loaded from the items: Bake World-Space Meshes, and an Export with pvs_mode rebake.
    bool                                                                     load_form{false};
    bool                                                                     form_installed{false};
    Export_phase                                                             phase{Export_phase::ensure};
    // pvs_mode rebake: the PVS item the bake stored.
    std::optional<Stored_pvs_item>                                           baked_pvs;
    std::chrono::steady_clock::time_point                                    pvs_start;
    // The PVS item the Scene item names (R9.3), none for pvs_mode off.
    std::optional<Key>                                                       pvs_key;
    // compute_mesh_keys(): key check shapes whose derived content hash is left to a status job, and hashes computed.
    std::size_t                                                              hash_pending_count {0};
    std::size_t                                                              computed_hash_count{0};
    // Key check: the Scene key of the Export that wrote the scene as it is (set_scene_export_known_scene_key()).
    std::optional<Key>                                                       known_scene_key;

    mutable std::mutex          progress_mutex;
    Scene_export_progress_state progress;

    void begin_stage(const Scene_export_stage stage, const std::size_t total)
    {
        std::lock_guard<std::mutex> lock{progress_mutex};
        progress = Scene_export_progress_state{.stage = stage, .done = 0, .total = total};
    }
    void advance()
    {
        std::lock_guard<std::mutex> lock{progress_mutex};
        ++progress.done;
    }

    // The index of a source file, added at first sight.
    auto add_file(const std::filesystem::path& source_path, Export_file_role role, std::size_t depth) -> std::size_t;
    void fail(const std::string& message);
    // R10.8, R2.5: every file the scene depends on is read and has an editor asset; logs R10.5.
    [[nodiscard]] auto open_dependency_files() -> bool;
    [[nodiscard]] auto get_store(std::size_t file_index) -> Asset_store&;
    void compute_mesh_keys();
    void compute_world_keys();
    [[nodiscard]] auto ensure_local_mesh_items(std::size_t file_index) -> bool;
    void ensure_file_texture_items(std::size_t file_index);
    void log_inline_shapes();
    [[nodiscard]] auto ensure_world_mesh_items() -> bool;
    [[nodiscard]] auto is_local_key_asked(const std::vector<std::size_t>& indices) const -> bool;
    [[nodiscard]] auto bake_world_meshes() -> bool;
    void load_world_form();
    void compute_texture_keys();
    [[nodiscard]] auto ensure_mesh_items() -> bool;
    // The Basis texture items the missing Texture items of target-encoded images are transcoded from.
    void ensure_basis_texture_items(std::size_t file_index, const std::vector<const std::vector<std::size_t>*>& missing);
    [[nodiscard]] auto make_texture_payload(const Asset_store& store, const Export_texture& first, std::string& out_meta) const -> std::vector<std::uint8_t>;
    [[nodiscard]] auto compute_status() -> bool;
    [[nodiscard]] auto ensure_scene_item(std::string& out_scene_item, Key& out_scene_key) -> bool;
    [[nodiscard]] auto copy(const std::string& scene_item, const Key& scene_key) -> bool;
    // The PVS item pvs_mode names, set on the Scene item's export arguments.
    [[nodiscard]] auto resolve_pvs_item(Scene_export_mode job) -> bool;
    // Main thread: the loaded form replaces the scene's; the admitted nodes it draws.
    auto install_form() -> std::size_t;
};

Scene_export::Scene_export() = default;
Scene_export::~Scene_export() noexcept = default;

auto begin_scene_export(
    App_context&                 context,
    Scene_root&                  scene_root,
    const std::string_view       target,
    const std::filesystem::path& prepared_asset_path,
    const Scene_export_mode      mode,
    const Prepared_drop          drop
) -> std::unique_ptr<Scene_export>
{
    const char* const verb =
        ((mode == Scene_export_mode::status) || (mode == Scene_export_mode::key_check)) ? "Status" :
        (mode == Scene_export_mode::bake_world_meshes) ? "Bake World-Space Meshes" : "Export";
    const std::optional<Scene_item_textures> textures = get_target_scene_item_textures(target);
    if (!textures.has_value()) {
        log_export->error("{} of '{}' refused: the store container exports targets pc and quest, not '{}'", verb, scene_root.get_name(), target);
        return {};
    }
    // R11.2: no editor asset is made for a prepared run; R1.7, R2.7, R7.3.
    const std::string refusal = get_export_refusal(scene_root, verb);
    if (!refusal.empty()) {
        log_export->warn("{}", refusal);
        return {};
    }
    const std::filesystem::path& source_path = scene_root.get_source_path();
    ERHE_VERIFY(context.editor_asset_registry != nullptr);
    // R10.8: an editor asset that cannot be written fails the job on its worker, naming the file.
    std::shared_ptr<Editor_asset> editor_asset = context.editor_asset_registry->get(source_path, scene_root.get_editor_asset_record());

    std::unique_ptr<Scene_export> scene_export = std::make_unique<Scene_export>();
    scene_export->impl = std::make_unique<Scene_export::Impl>(scene_root);
    Scene_export::Impl& impl = *scene_export->impl;
    impl.scene_name          = scene_root.get_name();
    impl.mode                = mode;
    impl.drop                = drop;
    impl.target              = std::string{target};
    // As stored: get_scene_id() would generate one, changing the scene and its Scene item.
    impl.scene_id            = scene_root.get_scene_settings().scene_id;
    impl.source_path         = normalize_asset_path(source_path);
    if (!editor_asset) {
        impl.error = fmt::format(
            "{} of '{}' failed: the editor asset of the scene {}", verb, scene_root.get_name(),
            context.editor_asset_registry->describe_unwritable(source_path, scene_root.get_editor_asset_record())
        );
    }
    impl.prepared_asset_path = prepared_asset_path.empty() ? get_default_prepared_asset_path(impl.source_path, target) : prepared_asset_path;
    impl.editor_asset        = std::move(editor_asset);
    impl.executor            = context.executor;
    impl.editor_asset_registry = context.editor_asset_registry;
    impl.files.push_back(Export_file{.source_path = impl.source_path, .role = Export_file_role::scene, .depth = 0, .editor_asset = impl.editor_asset});
    impl.file_indices.emplace(impl.source_path, 0);

    // The scene's settings are the only settings input (R7.2). Bake World-Space
    // Meshes plans the bake whatever bake_static_to_world_space says.
    Scene_export_settings& settings = impl.settings;
    settings = scene_root.get_scene_settings().export_settings.value();
    if (mode == Scene_export_mode::bake_world_meshes) {
        settings.bake_static_to_world_space = true;
    }
    impl.pvs_mode = settings.pvs_mode;
    if ((mode == Scene_export_mode::export_items) && (settings.pvs_mode == Pvs_mode::rebake)) {
        // R7.6: refused before anything is made.
        if ((settings.pvs_region_min == glm::vec3{0.0f}) && (settings.pvs_region_max == glm::vec3{0.0f})) {
            log_export->warn(
                "Export of '{}' refused: pvs_mode is rebake and the PVS region is unset (pvs_region_min and pvs_region_max are zero) - "
                "set the region (Properties > Export, or MCP set_scene_settings) or choose pvs_mode off or reuse",
                scene_root.get_name()
            );
            return {};
        }
        if (!settings.bake_static_to_world_space) {
            log_export->warn(
                "Export of '{}' refused: pvs_mode rebake bakes the PVS on the world-space form, which a Scene item writes only with "
                "bake_static_to_world_space on - turn it on or choose pvs_mode off or reuse",
                scene_root.get_name()
            );
            return {};
        }
        if ((context.pvs_baker == nullptr) || context.pvs_baker->is_running()) {
            log_export->warn("Export of '{}' refused: pvs_mode rebake needs the PVS baker, which {}", scene_root.get_name(), (context.pvs_baker == nullptr) ? "this editor has not" : "is running another bake");
            return {};
        }
    }
    impl.load_form = (mode == Scene_export_mode::bake_world_meshes) || ((mode == Scene_export_mode::export_items) && (settings.pvs_mode == Pvs_mode::rebake));
    impl.scene_stamp = make_scene_item_stamp(textures.value());
    // A quest Scene item carries its images as raw ASTC 4x4 KTX2 (ERHE_texture_ktx2).
    const erhe::gltf::Gltf_texture_export texture_export = (textures.value() == Scene_item_textures::astc_4x4)
        ? get_quest_scene_item_texture_export(settings.texture_supercompression)
        : erhe::gltf::Gltf_texture_export::source;
    impl.bvh_items = settings.raytrace_bvh && !erhe::raytrace::get_prebuilt_bvh_backend().empty();
    if (settings.raytrace_bvh && !impl.bvh_items) {
        log_export->info("{} of '{}': the raytrace backend exports no BVH, so no Raytrace BVH items are made", verb, scene_root.get_name());
    }
    // Basis Universal sources become Texture items of the target's block format (D8 step 6b).
    impl.block_target     = (target == "quest") ? erhe::graphics::Ktx2_block_target::astc_4x4 : erhe::graphics::Ktx2_block_target::bc;
    impl.supercompression = (settings.texture_supercompression == Texture_supercompression::zstd)
        ? erhe::graphics::Ktx2_supercompression::zstd
        : erhe::graphics::Ktx2_supercompression::none;
    impl.texture_stamp    = make_texture_stamp(impl.block_target);
    impl.texture_encoding = settings.texture_encoding;
    impl.basis_stamp      = make_basis_texture_stamp(erhe::graphics::get_basis_encoder_version());
    impl.save = begin_scene_save_gltf(context, scene_root, impl.prepared_asset_path, Gltf_scene_save_kind::scene_item, texture_export, &settings);
    if (!impl.save) {
        return {};
    }
    erhe::gltf::Gltf_export_arguments& arguments = get_scene_save_export_arguments(*impl.save);
    // The Scene item names the PVS item pvs_mode chooses, never the open scene's database (resolve_pvs_item()).
    impl.gpu_vertex_formats = arguments.gpu_vertex_formats;
    impl.mesh_stamp         = make_running_mesh_stamp(impl.gpu_vertex_formats);
    impl.bvh_stamp          = make_running_bvh_stamp();
    std::vector<const erhe::primitive::Material*>        materials;
    std::vector<std::size_t>                             material_files;
    std::unordered_set<const erhe::primitive::Material*> material_set;
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root.item_host_mutex};
        Scene_export::Impl* const collecting = &impl;
        const Export_file_of file_of = [collecting](const std::filesystem::path& path, const std::size_t depth) -> std::size_t {
            return collecting->add_file(path, Export_file_role::prefab, depth);
        };
        collect_export_shapes(
            arguments.root_node, arguments, file_of, 0, 0, impl.shapes, impl.shape_indices, impl.world_primitives,
            materials, material_files, material_set
        );
        for (std::size_t i = 0, end = impl.world_primitives.size(); i < end; ++i) {
            impl.world_primitive_indices.emplace(std::pair{impl.world_primitives[i].mesh, impl.world_primitives[i].primitive_index}, i);
        }
        if (arguments.world_bake != nullptr) {
            impl.report.world_mesh_node_count = arguments.world_bake->baked_meshes.size();
        }
        // The admitted meshes in walk order (the order of their world primitives).
        if (impl.load_form) {
            std::unordered_set<const erhe::scene::Mesh*> seen;
            for (const Export_world_primitive& world_primitive : impl.world_primitives) {
                if (seen.insert(world_primitive.mesh).second) {
                    impl.bake_meshes.push_back(std::static_pointer_cast<erhe::scene::Mesh>(
                        const_cast<erhe::scene::Mesh*>(world_primitive.mesh)->shared_from_this()
                    ));
                }
            }
        }
    }
    // A material a container defines is that container's content (R10.2); a stub of an unreadable one is refused (R10.8).
    const std::shared_ptr<Content_library>& content_library = scene_root.get_content_library();
    for (std::size_t m = 0, end = materials.size(); m < end; ++m) {
        const std::filesystem::path container_path = (context.asset_manager != nullptr)
            ? context.asset_manager->get_defining_container_path(*materials[m])
            : std::filesystem::path{};
        if (!container_path.empty()) {
            material_files[m] = impl.add_file(container_path, Export_file_role::material_container, 1);
            continue;
        }
        if (!content_library) {
            continue;
        }
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> library_lock{content_library->mutex};
        const Resource_metadata* const metadata = content_library->find_metadata(*materials[m]);
        if (
            (metadata != nullptr) && metadata->asset_key.has_value() && (metadata->asset_key->scope == Asset_scope::file) &&
            !metadata->asset_key->path.empty() &&
            (normalize_asset_path(std::filesystem::path{metadata->asset_key->path}) != impl.source_path)
        ) {
            impl.unresolved.push_back(fmt::format(
                "the material container '{}' of material '{}' was not read when the scene was opened - the scene holds a stub of the material",
                metadata->asset_key->path, materials[m]->get_name()
            ));
        }
    }
    collect_export_textures(materials, material_files, impl.textures, impl.texture_indices);
    if (impl.load_form) {
        ERHE_VERIFY((context.graphics_device != nullptr) && (context.mesh_memory != nullptr));
        impl.graphics_device = context.graphics_device;
        impl.mesh_memory     = context.mesh_memory;
        // Made here: make_primitive_buffer_info() can create GPU objects.
        impl.form_build_info.emplace(make_import_build_info(context, erhe::scene_renderer::Mesh_memory_queue::loader));
        impl.form_skinned_build_info.emplace(erhe::primitive::Build_info{
            .primitive_types = impl.form_build_info->primitive_types,
            .buffer_info     = context.mesh_memory->make_skinned_primitive_buffer_info(erhe::scene_renderer::Mesh_memory_queue::loader),
            .constant_color  = impl.form_build_info->constant_color,
            .keep_geometry   = impl.form_build_info->keep_geometry,
            .normal_style    = impl.form_build_info->normal_style,
            .vertex_id_vec3  = impl.form_build_info->vertex_id_vec3,
            .autocolor       = impl.form_build_info->autocolor
        });
        impl.form_device_options         = make_gltf_device_options(context, *context.graphics_device);
        impl.form_triangle_soup_reload   = get_triangle_soup_reload(context);
        impl.form_base_variant           = get_base_variant_load(context);
    }
    // The images of prefab templates and material containers are in their parses, not the scene's library (R10.3).
    if ((impl.files.size() > 1) && ((context.prefab_library != nullptr) || (context.asset_manager != nullptr))) {
        using Source_map = std::unordered_map<const erhe::graphics::Texture*, std::shared_ptr<const erhe::gltf::Gltf_image_source>>;
        std::shared_ptr<Source_map> sources = std::make_shared<Source_map>();
        if (context.asset_manager != nullptr) {
            context.asset_manager->collect_container_image_sources(*sources);
        }
        if (context.prefab_library != nullptr) {
            for (const auto& [prefab_key, prefab] : context.prefab_library->get_prefabs()) {
                static_cast<void>(prefab_key);
                const erhe::gltf::Gltf_data& gltf_data = prefab->gltf_data;
                for (std::size_t i = 0, end = std::min(gltf_data.images.size(), gltf_data.image_sources.size()); i < end; ++i) {
                    if (gltf_data.images[i] && gltf_data.image_sources[i]) {
                        sources->try_emplace(gltf_data.images[i].get(), gltf_data.image_sources[i]);
                    }
                }
            }
        }
        std::function<std::shared_ptr<const erhe::gltf::Gltf_image_source>(const erhe::graphics::Texture*)> scene_sources = arguments.image_source_provider;
        arguments.image_source_provider = [scene_sources, sources](const erhe::graphics::Texture* texture) -> std::shared_ptr<const erhe::gltf::Gltf_image_source> {
            std::shared_ptr<const erhe::gltf::Gltf_image_source> source = scene_sources ? scene_sources(texture) : std::shared_ptr<const erhe::gltf::Gltf_image_source>{};
            if (!source && (texture != nullptr)) {
                const auto i = sources->find(texture);
                if (i != sources->end()) {
                    source = i->second;
                }
            }
            return source;
        };
    }
    // R10.8: references the open could not resolve leave carriers without content.
    for (const std::filesystem::path& unresolved : scene_root.get_unresolved_sources()) {
        impl.unresolved.push_back(fmt::format("the prefab source '{}' could not be read when the scene was opened - its instances hold no content", erhe::file::to_string(unresolved)));
    }
    if (context.prefab_library != nullptr) {
        for (const Export_file& file : impl.files) {
            if (file.role != Export_file_role::prefab) {
                continue;
            }
            for (const auto& [prefab_key, prefab] : context.prefab_library->get_prefabs()) {
                if ((normalize_asset_path(prefab_key.source_path) != file.source_path) || !prefab) {
                    continue;
                }
                for (const std::filesystem::path& unresolved : prefab->unresolved_sources) {
                    impl.unresolved.push_back(fmt::format(
                        "the prefab source '{}' that '{}' instantiates could not be read - its instances hold no content",
                        erhe::file::to_string(unresolved), erhe::file::to_string(file.source_path)
                    ));
                }
            }
        }
    }
    // R10.7: the files the scene depends on first, deepest prefab first; the scene last.
    for (std::size_t i = 1, end = impl.files.size(); i < end; ++i) {
        impl.ensure_order.push_back(i);
    }
    std::stable_sort(impl.ensure_order.begin(), impl.ensure_order.end(), [&impl](const std::size_t a, const std::size_t b) {
        return impl.files[a].depth > impl.files[b].depth;
    });
    impl.ensure_order.push_back(0);
    for (Export_texture& entry : impl.textures) {
        entry.source = arguments.image_source_provider ? arguments.image_source_provider(entry.texture) : std::shared_ptr<const erhe::gltf::Gltf_image_source>{};
    }
    for (Export_shape& shape : impl.shapes) {
        if (!shape.lod_settings.has_value()) {
            shape.lod_settings = arguments.default_lod_settings;
        }
        // A shape without a soup is geometry-normative: its item is made from its derived triangles (R4.4).
        if (!shape.shape->has_triangle_soup()) {
            shape.source   = Local_mesh_source::derived_triangles;
            shape.geometry = shape.shape->get_geometry_const().get();
            if ((shape.geometry == nullptr) || (shape.geometry->get_mesh().facets.nb() == 0)) {
                shape.exported = false;
            } else {
                impl.geometry_mutexes.try_emplace(shape.geometry, std::make_unique<std::mutex>());
            }
        }
    }
    Scene_export::Impl* const impl_pointer = &impl;
    arguments.mesh_item_provider = [impl_pointer](const erhe::primitive::Primitive_shape& shape) -> std::optional<erhe::gltf::Gltf_mesh_item_reference> {
        const auto i = impl_pointer->shape_indices.find(&shape);
        if (i == impl_pointer->shape_indices.end()) {
            return std::nullopt;
        }
        const Export_shape& entry = impl_pointer->shapes[i->second];
        if (!entry.has_item) {
            return std::nullopt;
        }
        erhe::gltf::Gltf_mesh_item_reference reference{.uri = make_item_uri(entry.key), .meta = entry.meta};
        if (entry.has_bvh_item) {
            reference.raytrace_bvh = erhe::gltf::Gltf_item_reference{.uri = make_item_uri(entry.bvh_key), .meta = entry.bvh_meta};
        }
        return reference;
    };
    arguments.world_mesh_item_provider = [impl_pointer](const erhe::scene::Mesh& mesh, const std::size_t primitive_index) -> std::optional<erhe::gltf::Gltf_mesh_item_reference> {
        const auto i = impl_pointer->world_primitive_indices.find(std::pair{&mesh, primitive_index});
        if (i == impl_pointer->world_primitive_indices.end()) {
            return std::nullopt;
        }
        const Export_world_primitive& entry = impl_pointer->world_primitives[i->second];
        if (!entry.has_item) {
            return std::nullopt;
        }
        erhe::gltf::Gltf_mesh_item_reference reference{.uri = make_item_uri(entry.key), .meta = entry.meta};
        if (entry.has_bvh_item) {
            reference.raytrace_bvh = erhe::gltf::Gltf_item_reference{.uri = make_item_uri(entry.bvh_key), .meta = entry.bvh_meta};
        }
        return reference;
    };
    arguments.texture_item_provider = [impl_pointer](const erhe::graphics::Texture& texture) -> std::optional<erhe::gltf::Gltf_item_reference> {
        const auto i = impl_pointer->texture_indices.find(&texture);
        if (i == impl_pointer->texture_indices.end()) {
            return std::nullopt;
        }
        const Export_texture& entry = impl_pointer->textures[i->second];
        if (!entry.has_item) {
            return std::nullopt;
        }
        return erhe::gltf::Gltf_item_reference{.uri = make_item_uri(entry.key), .meta = entry.meta};
    };
    impl.report.editor_asset_path   = impl.editor_asset ? impl.editor_asset->get_asset_path() : std::filesystem::path{};
    impl.report.prepared_asset_path = impl.prepared_asset_path;
    impl.report.target              = impl.target;
    log_export->info(
        "{} of '{}' ({}): editor asset '{}', prepared asset '{}', {} primitive shapes, {} textures, {} world-space bake mesh nodes ({} primitives), pvs_mode {}",
        verb, scene_root.get_name(), impl.target,
        erhe::file::to_string(impl.report.editor_asset_path), erhe::file::to_string(impl.prepared_asset_path), impl.shapes.size(), impl.textures.size(),
        impl.report.world_mesh_node_count, impl.world_primitives.size(), to_string(impl.pvs_mode)
    );
    for (std::size_t i = 1, end = impl.files.size(); i < end; ++i) {
        log_export->info("{} of '{}': depends on the {} '{}' (depth {})", verb, scene_root.get_name(), c_str(impl.files[i].role), erhe::file::to_string(impl.files[i].source_path), impl.files[i].depth);
    }
    return scene_export;
}

auto Scene_export::Impl::add_file(const std::filesystem::path& path, const Export_file_role role, const std::size_t depth) -> std::size_t
{
    const std::filesystem::path normalized = normalize_asset_path(path);
    const auto i = file_indices.find(normalized);
    if (i != file_indices.end()) {
        Export_file& file = files[i->second];
        if (i->second == 0) {
            return 0; // the scene's own content
        }
        file.depth = std::max(file.depth, depth);
        if ((role == Export_file_role::prefab) && (file.role == Export_file_role::material_container)) {
            file.role = Export_file_role::prefab;
        }
        return i->second;
    }
    const std::size_t index = files.size();
    files.push_back(Export_file{.source_path = normalized, .role = role, .depth = depth});
    file_indices.emplace(normalized, index);
    return index;
}

void Scene_export::Impl::fail(const std::string& message)
{
    log_export->error("{}", message);
    if (error.empty()) {
        error = message;
    }
}

auto Scene_export::Impl::get_store(const std::size_t file_index) -> Asset_store&
{
    return files[file_index].editor_asset->get_store();
}

auto Scene_export::Impl::open_dependency_files() -> bool
{
    ERHE_PROFILE_FUNCTION();
    const bool        status_job = (mode == Scene_export_mode::status) || (mode == Scene_export_mode::key_check);
    const char* const verb       = status_job ? "Status" : "Export";
    if (!unresolved.empty()) {
        fail(fmt::format("{} of '{}' failed: {}", verb, scene_name, unresolved.front()));
        return false;
    }
    // The fields that change a prefab's own items (R10.5).
    static constexpr std::array<const char*, 4> c_item_fields{"lod", "texture_encoding", "texture_supercompression", "raytrace_bvh"};
    const nlohmann::json host_json = nlohmann::json::parse(serialize(settings, 0), nullptr, false);
    for (const std::size_t i : ensure_order) {
        Export_file& file = files[i];
        if (i == 0) {
            continue;
        }
        const char* const role_text = (file.role == Export_file_role::prefab) ? "prefab source" : "material container";
        const std::optional<Source_file_state> state = read_source_file_state(file.source_path);
        if (!state.has_value()) {
            fail(fmt::format("{} of '{}' failed: the {} '{}' cannot be read", verb, scene_name, role_text, erhe::file::to_string(file.source_path)));
            return false;
        }
        file.editor_asset = editor_asset_registry->get(file.source_path, state->record_path);
        if (!file.editor_asset) {
            fail(fmt::format(
                "{} of '{}' failed: no editor asset of the {} '{}' can be written (details above)",
                verb, scene_name, role_text, erhe::file::to_string(file.source_path)
            ));
            return false;
        }
        if (!status_job && (file.role == Export_file_role::prefab) && state->export_settings.has_value()) {
            const nlohmann::json prefab_json = nlohmann::json::parse(serialize(state->export_settings.value(), 0), nullptr, false);
            for (const char* const field : c_item_fields) {
                const nlohmann::json host_value   = host_json.is_object()   && host_json.contains(field)   ? host_json[field]   : nlohmann::json{};
                const nlohmann::json prefab_value = prefab_json.is_object() && prefab_json.contains(field) ? prefab_json[field] : nlohmann::json{};
                if (host_value != prefab_value) {
                    log_export->warn(
                        "Export of '{}': the prefab '{}' has export setting {} = {} where the scene has {} - the scene's value applies",
                        scene_name, erhe::file::to_string(file.source_path), field, prefab_value.dump(), host_value.dump()
                    );
                }
            }
        }
        if (status_job) {
            continue;
        }
        Asset_store& store = file.editor_asset->get_store();
        erhe::asset_store::Write_transaction transaction{store};
        if (
            !transaction.is_active() ||
            !transaction.set_source_record(erhe::asset_store::Source_record{
                .relative_path = relative_path_text(file.source_path, file.editor_asset->get_asset_path().parent_path()),
                .scene_id      = state->scene_id
            }) ||
            !transaction.commit()
        ) {
            fail(fmt::format("Export of '{}' failed: the editor asset '{}' takes no writes", scene_name, erhe::file::to_string(file.editor_asset->get_asset_path())));
            return false;
        }
    }
    return true;
}

// The keys of the shapes from their content hashes (R3.5); Ensure, the status and the key check share it.
void Scene_export::Impl::compute_mesh_keys()
{
    ERHE_PROFILE_FUNCTION();
    begin_stage(Scene_export_stage::mesh_keys, shapes.size());
    run_parallel(executor, shapes.size(), [this](const std::size_t i) {
        Export_shape& entry = shapes[i];
        entry.key_known     = false;
        entry.hash_pending  = false;
        entry.hash_computed = false;
        if (!entry.exported) {
            advance();
            return;
        }
        std::optional<erhe::hash::Content_hash> content_hash = entry.shape->get_content_hash();
        if (!content_hash.has_value()) {
            if (entry.source == Local_mesh_source::soup) {
                // A soup loaded without one (a parse that computed none): hashed once, from the soup in memory or read back.
                const std::shared_ptr<erhe::primitive::Triangle_soup> soup = entry.shape->get_triangle_soup();
                if (soup) {
                    content_hash = erhe::primitive::compute_triangle_soup_content_hash(*soup);
                    entry.shape->set_content_hash(content_hash.value());
                    entry.hash_computed = true;
                }
            } else if (mode == Scene_export_mode::key_check) {
                // Deriving writes the Geometry's facet ids: only a job that holds the scene read-only does it.
                entry.hash_pending = true;
            } else {
                std::lock_guard<std::mutex> geometry_lock{*geometry_mutexes.at(entry.geometry)};
                erhe::primitive::Derived_content_hash derived = erhe::primitive::compute_derived_content_hash(*entry.shape);
                if (derived.content_hash.has_value()) {
                    content_hash = derived.content_hash;
                    entry.shape->set_content_hash(content_hash.value());
                    entry.hash_computed = true;
                } else {
                    entry.skip_reason = std::move(derived.refusal);
                }
            }
        }
        if (content_hash.has_value()) {
            entry.key = make_shape_local_mesh_key(entry.source, content_hash.value(), entry.lod_settings, mesh_stamp);
            entry.bvh_key   = make_raytrace_bvh_key(entry.key, bvh_stamp);
            entry.key_known = true;
        }
        advance();
    });

    shapes_by_key.clear();
    hash_pending_count  = 0;
    computed_hash_count = 0;
    for (std::size_t i = 0, end = shapes.size(); i < end; ++i) {
        if (shapes[i].key_known) {
            shapes_by_key[shapes[i].key].push_back(i);
        }
        if (shapes[i].hash_pending) {
            ++hash_pending_count;
        }
        if (shapes[i].hash_computed) {
            ++computed_hash_count;
        }
    }
}

// The World-space mesh keys from the shapes' Local-space mesh keys (R4.7).
void Scene_export::Impl::compute_world_keys()
{
    world_primitives_by_key.clear();
    for (std::size_t i = 0, end = world_primitives.size(); i < end; ++i) {
        Export_world_primitive& entry = world_primitives[i];
        const Export_shape&     shape = shapes[entry.shape_index];
        entry.key_known = shape.key_known;
        if (!entry.key_known) {
            continue;
        }
        entry.key = make_world_mesh_key(World_mesh_key_inputs{
            .local_mesh_key  = shape.key,
            .world_from_node = entry.world_from_node,
            .stamp           = mesh_stamp
        });
        entry.bvh_key = make_raytrace_bvh_key(entry.key, bvh_stamp);
        world_primitives_by_key[entry.key].push_back(i);
    }
}

// A Local-space mesh key is asked for when a mesh the bake does not admit draws one of its shapes (R9.3).
auto Scene_export::Impl::is_local_key_asked(const std::vector<std::size_t>& indices) const -> bool
{
    return std::any_of(indices.begin(), indices.end(), [this](const std::size_t i) { return shapes[i].unbaked_user; });
}

// Ensure, World-space mesh items: per shape, one preparation baked once per missing key.
auto Scene_export::Impl::ensure_world_mesh_items() -> bool
{
    ERHE_PROFILE_FUNCTION();
    if (world_primitives_by_key.empty()) {
        return true;
    }
    Asset_store& store = editor_asset->get_store();
    Export_kind_counts& counts     = report.counts[Item_kind::world_mesh];
    Export_kind_counts* bvh_counts = bvh_items ? &report.counts[Item_kind::raytrace_bvh] : nullptr;
    // R4.8: every node of a key, as it is looked up now.
    const auto make_info = [this](const std::vector<std::size_t>& indices) -> std::string {
        std::vector<std::string> records;
        for (const std::size_t i : indices) {
            records.push_back(world_primitives[i].node_record);
        }
        std::sort(records.begin(), records.end());
        records.erase(std::unique(records.begin(), records.end()), records.end());
        std::string info = "{\"nodes\":[";
        for (std::size_t i = 0, end = records.size(); i < end; ++i) {
            info += (i == 0) ? records[i] : ("," + records[i]);
        }
        return info + "]}";
    };
    class Missing_key
    {
    public:
        const Key*                      key{nullptr};
        const std::vector<std::size_t>* indices{nullptr};
        std::string                     info;
        bool                            mesh{false};
        bool                            bvh {false};
    };
    // Keyed by shape: one preparation serves every key of the shape.
    std::map<std::size_t, std::vector<Missing_key>> missing_by_shape;
    std::vector<std::pair<Key, std::string>>        info_updates;
    std::size_t                                     world_bvh_present = 0;
    for (const auto& [key, indices] : world_primitives_by_key) {
        Missing_key entry{.key = &key, .indices = &indices, .info = make_info(indices)};
        const std::optional<erhe::asset_store::Item_info> info = store.get_info(key);
        if (info.has_value()) {
            ++counts.present;
            for (const std::size_t i : indices) {
                world_primitives[i].meta     = info->meta;
                world_primitives[i].has_item = true;
            }
            if (info->info != entry.info) {
                info_updates.emplace_back(key, entry.info);
            }
        } else {
            entry.mesh = true;
        }
        if (bvh_items) {
            const Key& bvh_key = world_primitives[indices.front()].bvh_key;
            const std::optional<std::string> bvh_meta = store.contains(bvh_key) ? store.read_meta(bvh_key) : std::optional<std::string>{};
            if (bvh_meta.has_value()) {
                ++bvh_counts->present;
                ++world_bvh_present;
                for (const std::size_t i : indices) {
                    world_primitives[i].bvh_meta     = bvh_meta.value();
                    world_primitives[i].has_bvh_item = true;
                }
            } else {
                entry.bvh = true;
            }
        }
        if (entry.mesh || entry.bvh) {
            missing_by_shape[world_primitives[indices.front()].shape_index].push_back(std::move(entry));
        }
    }
    if (!info_updates.empty()) {
        erhe::asset_store::Write_transaction transaction{store};
        if (transaction.is_active()) {
            for (const auto& [key, info] : info_updates) {
                static_cast<void>(transaction.set_info(key, info));
            }
            static_cast<void>(transaction.commit());
        }
    }
    std::vector<std::pair<std::size_t, std::vector<Missing_key>*>> groups;
    std::size_t missing_key_count = 0;
    for (auto& [shape_index, keys] : missing_by_shape) {
        groups.emplace_back(shape_index, &keys);
        missing_key_count += keys.size();
    }
    begin_stage(Scene_export_stage::world_mesh_items, missing_key_count);
    std::vector<std::size_t> made    (groups.size(), 0);
    std::vector<std::size_t> bvh_made(groups.size(), 0);
    std::vector<std::string> failures(groups.size());
    run_parallel(executor, groups.size(), [this, &store, &groups, &made, &bvh_made, &failures](const std::size_t g) {
        const std::size_t         shape_index = groups[g].first;
        std::vector<Missing_key>& keys        = *groups[g].second;
        Export_shape&             shape       = shapes[shape_index];
        erhe::primitive::Prepare_primitive_result              result;
        std::optional<erhe::primitive::Primitive_render_shape> authoring_shape;
        if (shape.source == Local_mesh_source::soup) {
            result = prepare_shape_for_items(*shape.shape, shape.source, authoring_shape);
        } else {
            std::lock_guard<std::mutex> geometry_lock{*geometry_mutexes.at(shape.geometry)};
            result = prepare_shape_for_items(*shape.shape, shape.source, authoring_shape);
        }
        if (!result.prepared.has_value()) {
            failures[g] = result.skip_reason;
            for (std::size_t k = 0; k < keys.size(); ++k) {
                advance();
            }
            return;
        }
        for (Missing_key& missing_key : keys) {
            const std::vector<std::size_t>& indices = *missing_key.indices;
            erhe::primitive::Prepared_primitive_export baked = result.prepared.value();
            static_cast<void>(erhe::primitive::bake_prepared_primitive_transform(baked, world_primitives[indices.front()].world_from_node));
            if (missing_key.mesh) {
                // The chain is generated in world space: a stored chain is in object space.
                if (shape.lod_settings.has_value()) {
                    static_cast<void>(erhe::primitive::make_export_lod_chain(baked, authoring_shape.has_value() ? authoring_shape.value() : *shape.shape, shape.lod_settings.value()));
                }
                const erhe::gltf::Gltf_mesh_item item = erhe::gltf::write_mesh_item(baked, gpu_vertex_formats);
                const bool stored = store.put(erhe::asset_store::Item_data{
                    .key     = *missing_key.key,
                    .kind    = Item_kind::world_mesh,
                    .target  = {},
                    .stamp   = mesh_stamp,
                    .meta    = item.meta,
                    .info    = missing_key.info,
                    .payload = item.payload
                });
                if (!stored) {
                    failures[g] = "the editor asset did not store the item";
                    advance();
                    continue;
                }
                ++made[g];
                for (const std::size_t i : indices) {
                    world_primitives[i].meta     = item.meta;
                    world_primitives[i].has_item = true;
                }
            }
            if (missing_key.bvh && baked.bvh.has_value()) {
                const erhe::gltf::Gltf_raytrace_bvh_item bvh_item = erhe::gltf::write_raytrace_bvh_item(baked.bvh.value());
                const Key& bvh_key = world_primitives[indices.front()].bvh_key;
                const bool stored = store.put(erhe::asset_store::Item_data{
                    .key     = bvh_key,
                    .kind    = Item_kind::raytrace_bvh,
                    .target  = {},
                    .stamp   = bvh_stamp,
                    .meta    = bvh_item.meta,
                    .info    = {},
                    .payload = bvh_item.payload
                });
                if (stored) {
                    ++bvh_made[g];
                    for (const std::size_t i : indices) {
                        world_primitives[i].bvh_meta     = bvh_item.meta;
                        world_primitives[i].has_bvh_item = true;
                    }
                }
            }
            advance();
        }
    });
    for (std::size_t g = 0, end = groups.size(); g < end; ++g) {
        counts.made += made[g];
        if (bvh_counts != nullptr) {
            bvh_counts->made += bvh_made[g];
        }
    }
    // The scene's own items (R10.2).
    files[0].counts[Item_kind::world_mesh] = counts;
    if (bvh_counts != nullptr) {
        Export_kind_counts& host_bvh_counts = files[0].counts[Item_kind::raytrace_bvh];
        for (std::size_t g = 0, end = groups.size(); g < end; ++g) {
            host_bvh_counts.made += bvh_made[g];
        }
        host_bvh_counts.present += world_bvh_present;
    }
    std::map<std::string, std::size_t> failure_counts;
    for (const std::string& failure : failures) {
        if (!failure.empty()) {
            ++failure_counts[failure];
        }
    }
    for (const auto& [failure, count] : failure_counts) {
        log_export->warn("Export: {} shape(s) without World-space mesh items: {}", count, failure);
    }
    return true;
}

// The keys of the Basis Universal and PNG / JPEG sources from their content
// hashes (R3.5); a source loaded without one is read and hashed once.
void Scene_export::Impl::compute_texture_keys()
{
    ERHE_PROFILE_FUNCTION();
    begin_stage(Scene_export_stage::texture_keys, textures.size());
    run_parallel(executor, textures.size(), [this](const std::size_t i) {
        Export_texture& entry = textures[i];
        entry.keyed = false;
        // The header tells the kind of image (KTX2: 80 bytes; PNG / JPEG: their signatures).
        constexpr std::size_t header_byte_count = 128;
        std::vector<std::byte> bytes;
        if (!entry.source || !entry.source->read_prefix(header_byte_count, bytes) || bytes.empty()) {
            entry.skip_reason = "the texture has no readable source image";
            advance();
            return;
        }
        std::optional<erhe::hash::Content_hash> source_hash = entry.source->get_content_hash();
        if (!source_hash.has_value()) {
            std::vector<std::byte> stream;
            if (!entry.source->read(stream) || stream.empty()) {
                entry.skip_reason = "the texture has no readable source image";
                advance();
                return;
            }
            source_hash = erhe::hash::Xxh3_128_hasher::hash(stream);
            entry.source->set_content_hash(source_hash.value());
        }
        const std::span<const std::uint8_t> encoded{reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()};
        const Key content_hash = to_key(source_hash.value());
        if (erhe::graphics::is_ktx2_basis(encoded)) {
            entry.keyed       = true;
            entry.item_source = Texture_item_source::basis_source;
            entry.key = make_texture_key(Texture_key_inputs{
                .source            = Texture_item_source::basis_source,
                .content_hash      = content_hash,
                .transfer_function = entry.properties.transfer_function,
                .normal_encoding   = entry.properties.normal_encoding,
                .supercompression  = supercompression,
                .stamp             = texture_stamp
            });
        } else if (!erhe::gltf::make_source_image_item_meta(encoded).empty()) {
            entry.keyed = true;
            if (texture_encoding == Texture_encoding::source) {
                entry.item_source = Texture_item_source::source_bytes;
                entry.key = make_texture_key(Texture_key_inputs{
                    .source       = Texture_item_source::source_bytes,
                    .content_hash = content_hash,
                    .stamp        = make_source_texture_stamp()
                });
            } else {
                entry.item_source = Texture_item_source::basis_texture_item;
                entry.basis_key = make_basis_texture_key(Basis_texture_key_inputs{
                    .content_hash = content_hash,
                    .properties   = entry.properties,
                    .settings     = basis_settings,
                    .stamp        = basis_stamp
                });
                entry.key = make_texture_key(Texture_key_inputs{
                    .source            = Texture_item_source::basis_texture_item,
                    .content_hash      = entry.basis_key,
                    .transfer_function = entry.properties.transfer_function,
                    .normal_encoding   = entry.properties.normal_encoding,
                    .supercompression  = supercompression,
                    .stamp             = texture_stamp
                });
            }
        } else {
            entry.skip_reason = "neither a Basis Universal nor a PNG / JPEG image";
        }
        advance();
    });
    textures_by_key.clear();
    textures_by_basis_key.clear();
    for (std::size_t i = 0, end = textures.size(); i < end; ++i) {
        if (textures[i].keyed) {
            textures_by_key[textures[i].key].push_back(i);
            if (textures[i].item_source == Texture_item_source::basis_texture_item) {
                textures_by_basis_key[textures[i].basis_key].push_back(i);
            }
        }
    }
}

// Ensure, Basis texture items: encodes each one a missing Texture item needs and the editor asset lacks (R4.1, R7.5).
void Scene_export::Impl::ensure_basis_texture_items(const std::size_t file_index, const std::vector<const std::vector<std::size_t>*>& missing)
{
    ERHE_PROFILE_FUNCTION();
    Asset_store& store = get_store(file_index);
    std::set<Key> needed;
    for (const std::vector<std::size_t>* const indices : missing) {
        const Export_texture& first = textures[indices->front()];
        if (first.item_source == Texture_item_source::basis_texture_item) {
            needed.insert(first.basis_key);
        }
    }
    if (needed.empty()) {
        return;
    }
    Export_kind_counts& counts      = report.counts[Item_kind::basis_texture];
    Export_kind_counts& file_counts = files[file_index].counts[Item_kind::basis_texture];
    std::vector<Key> to_encode;
    for (const Key& key : needed) {
        if (store.contains(key)) {
            ++counts.present;
            ++file_counts.present;
        } else {
            to_encode.push_back(key);
        }
    }
    begin_stage(Scene_export_stage::basis_texture_items, to_encode.size());
    std::vector<char> made(to_encode.size(), 0);
    run_parallel(executor, to_encode.size(), [this, &store, &to_encode, &made](const std::size_t m) {
        const Key&            key   = to_encode[m];
        const Export_texture& first = textures[textures_by_basis_key.at(key).front()];
        std::vector<std::byte> bytes;
        std::vector<std::uint8_t> payload;
        if (first.source->read(bytes)) {
            payload = erhe::graphics::encode_image_to_basis_ktx2(
                std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()},
                first.properties,
                basis_settings
            );
        }
        bytes = std::vector<std::byte>{};
        const std::string meta = payload.empty()
            ? std::string{}
            : fmt::format("{{\"version\":1,\"byteLength\":{},\"mimeType\":\"image/ktx2\",\"basis\":\"uastc_ldr_4x4\"}}", payload.size());
        const bool stored = !meta.empty() && store.put(erhe::asset_store::Item_data{
            .key     = key,
            .kind    = Item_kind::basis_texture,
            .target  = {},
            .stamp   = basis_stamp,
            .meta    = meta,
            .info    = {},
            .payload = std::as_bytes(std::span<const std::uint8_t>{payload.data(), payload.size()})
        });
        if (!stored) {
            log_export->warn(
                "Export: the Basis texture item of '{}' was not made ({})", first.texture->get_name(),
                payload.empty() ? (erhe::graphics::is_basis_encoder_available() ? "the encode failed" : "this build has no Basis Universal encoder") : "the editor asset did not store it"
            );
        }
        made[m] = stored ? 1 : 0;
        advance();
    });
    const std::size_t made_count = static_cast<std::size_t>(std::count(made.begin(), made.end(), char{1}));
    counts.made      += made_count;
    file_counts.made += made_count;
}

// The payload (and its meta) of the Texture item of a texture; empty on failure.
auto Scene_export::Impl::make_texture_payload(const Asset_store& store, const Export_texture& first, std::string& out_meta) const -> std::vector<std::uint8_t>
{
    out_meta.clear();
    const erhe::graphics::Ktx2_color_space color_space = (first.properties.transfer_function == erhe::graphics::Texture_transfer_function::linear)
        ? erhe::graphics::Ktx2_color_space::linear
        : erhe::graphics::Ktx2_color_space::srgb;
    const erhe::graphics::Ktx2_normal_layout normal_layout = (first.properties.normal_encoding == erhe::graphics::Texture_normal_encoding::two_component)
        ? erhe::graphics::Ktx2_normal_layout::two_component
        : erhe::graphics::Ktx2_normal_layout::standard;
    std::vector<std::uint8_t> payload;
    switch (first.item_source) {
        case Texture_item_source::basis_source: {
            std::vector<std::byte> bytes;
            if (first.source->read(bytes)) {
                payload = erhe::graphics::transcode_ktx2_basis_to_raw_blocks(
                    std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()},
                    block_target, color_space, supercompression, normal_layout
                );
            }
            break;
        }
        case Texture_item_source::basis_texture_item: {
            // The Basis texture item, made by ensure_basis_texture_items() or present (R9.2).
            const std::optional<erhe::asset_store::Item_info> info = store.get_info(first.basis_key);
            if (info.has_value() && (info->payload_size > 0)) {
                std::vector<std::uint8_t> basis(static_cast<std::size_t>(info->payload_size));
                if (store.read_payload(first.basis_key, 0, std::as_writable_bytes(std::span<std::uint8_t>{basis}))) {
                    payload = erhe::graphics::transcode_ktx2_basis_to_raw_blocks(basis, block_target, color_space, supercompression, normal_layout);
                }
            }
            break;
        }
        case Texture_item_source::source_bytes: {
            std::vector<std::byte> bytes;
            if (first.source->read(bytes)) {
                const std::span<const std::uint8_t> image{reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()};
                out_meta = erhe::gltf::make_source_image_item_meta(image);
                if (!out_meta.empty()) {
                    payload.assign(image.begin(), image.end());
                }
            }
            return payload;
        }
        default: {
            break;
        }
    }
    if (!payload.empty()) {
        out_meta = erhe::gltf::make_texture_item_meta(payload);
    }
    return payload;
}

// Ensure, Texture items of one file: the first texture of a missing key is made (make_texture_payload()).
void Scene_export::Impl::ensure_file_texture_items(const std::size_t file_index)
{
    ERHE_PROFILE_FUNCTION();
    Asset_store&        store       = get_store(file_index);
    Export_kind_counts& counts      = report.counts[Item_kind::texture];
    Export_kind_counts& file_counts = files[file_index].counts[Item_kind::texture];
    std::vector<const std::vector<std::size_t>*> missing;
    for (const auto& [key, indices] : textures_by_key) {
        if (textures[indices.front()].file_index != file_index) {
            continue;
        }
        const std::optional<std::string> meta = store.contains(key) ? store.read_meta(key) : std::optional<std::string>{};
        if (meta.has_value()) {
            ++counts.present;
            ++file_counts.present;
            for (const std::size_t i : indices) {
                textures[i].meta     = meta.value();
                textures[i].has_item = true;
            }
        } else {
            missing.push_back(&indices);
        }
    }
    if (missing.empty()) {
        return;
    }
    ensure_basis_texture_items(file_index, missing);
    begin_stage(Scene_export_stage::texture_items, missing.size());
    std::vector<char> made(missing.size(), 0);
    run_parallel(executor, missing.size(), [this, &store, &missing, &made](const std::size_t m) {
        const std::vector<std::size_t>& indices = *missing[m];
        Export_texture& first = textures[indices.front()];
        std::string meta;
        const std::vector<std::uint8_t> payload = make_texture_payload(store, first, meta);
        // A PNG / JPEG image as it is is the same for every target (R3.10).
        const bool shared = (first.item_source == Texture_item_source::source_bytes);
        const bool stored = !meta.empty() && store.put(erhe::asset_store::Item_data{
            .key     = first.key,
            .kind    = Item_kind::texture,
            .target  = shared ? std::string{} : target,
            .stamp   = shared ? make_source_texture_stamp() : texture_stamp,
            .meta    = meta,
            .info    = {},
            .payload = std::as_bytes(std::span<const std::uint8_t>{payload.data(), payload.size()})
        });
        for (const std::size_t i : indices) {
            if (stored) {
                textures[i].meta     = meta;
                textures[i].has_item = true;
            } else {
                textures[i].skip_reason =
                    !meta.empty() ? "the editor asset did not store the item" :
                    (first.item_source == Texture_item_source::basis_texture_item) ? "its Basis texture item was not made or did not transcode" :
                    (first.item_source == Texture_item_source::source_bytes) ? "the source image could not be read" : "the transcode failed";
            }
        }
        made[m] = stored ? 1 : 0;
        advance();
    });
    const std::size_t made_count = static_cast<std::size_t>(std::count(made.begin(), made.end(), char{1}));
    counts.made      += made_count;
    file_counts.made += made_count;
}

auto Scene_export::Impl::ensure_mesh_items() -> bool
{
    ERHE_PROFILE_FUNCTION();
    Asset_store& store = editor_asset->get_store();
    {
        erhe::asset_store::Write_transaction transaction{store};
        if (
            !transaction.is_active() ||
            !transaction.set_source_record(erhe::asset_store::Source_record{
                .relative_path = relative_path_text(source_path, editor_asset->get_asset_path().parent_path()),
                .scene_id      = scene_id
            }) ||
            !transaction.commit()
        ) {
            log_export->error("Export: the source record of '{}' was not written", erhe::file::to_string(editor_asset->get_asset_path()));
            return false;
        }
    }

    if (!open_dependency_files()) {
        return false;
    }
    compute_mesh_keys();
    compute_world_keys();
    compute_texture_keys();
    // R10.7: the files the scene depends on first, deepest prefab first; R10.2: each file's items in its own editor asset.
    for (const std::size_t file_index : ensure_order) {
        if (!ensure_local_mesh_items(file_index)) {
            return false;
        }
        ensure_file_texture_items(file_index);
        if (files.size() > 1) {
            const Export_file& file = files[file_index];
            const auto counts_of = [&file](const Item_kind kind) -> Export_kind_counts {
                const auto i = file.counts.find(kind);
                return (i != file.counts.end()) ? i->second : Export_kind_counts{};
            };
            log_export->info(
                "Export of '{}': Ensure of the {} '{}' (depth {}) in '{}': mesh items made {} present {}, BVH items made {} present {}, texture items made {} present {}, basis texture items made {} present {}",
                scene_name, c_str(file.role), erhe::file::to_string(file.source_path), file.depth, erhe::file::to_string(file.editor_asset->get_asset_path()),
                counts_of(Item_kind::local_mesh).made, counts_of(Item_kind::local_mesh).present,
                counts_of(Item_kind::raytrace_bvh).made, counts_of(Item_kind::raytrace_bvh).present,
                counts_of(Item_kind::texture).made, counts_of(Item_kind::texture).present,
                counts_of(Item_kind::basis_texture).made, counts_of(Item_kind::basis_texture).present
            );
        }
    }
    // The use-dependent items are the scene's own (R10.2).
    if (!ensure_world_mesh_items()) {
        return false;
    }
    log_inline_shapes();
    std::map<std::string, std::size_t> inline_reasons;
    for (const Export_texture& entry : textures) {
        if (!entry.has_item && entry.source) {
            ++report.inline_image_count;
            ++inline_reasons[entry.skip_reason];
        }
    }
    for (const auto& [reason, count] : inline_reasons) {
        log_export->info("Export: {} image(s) written inside the Scene item: {}", count, reason);
    }
    for (const std::size_t file_index : ensure_order) {
        const Export_file& file = files[file_index];
        report.files.push_back(Scene_export_file_report{
            .source_path       = file.source_path,
            .editor_asset_path = file.editor_asset->get_asset_path(),
            .role              = file.role,
            .depth             = file.depth,
            .location          = c_str(file.editor_asset->get_location()),
            .counts            = file.counts
        });
    }
    return true;
}

// Ensure, Local-space mesh items of one file (R10.2).
auto Scene_export::Impl::ensure_local_mesh_items(const std::size_t file_index) -> bool
{
    ERHE_PROFILE_FUNCTION();
    Asset_store& store = get_store(file_index);
    // One item per key; the first shape of a key makes it, and its BVH item with it.
    class Missing_items
    {
    public:
        const std::vector<std::size_t>* indices{nullptr};
        bool                            mesh{false};
        bool                            bvh {false};
    };
    std::vector<Missing_items> missing;
    Export_kind_counts& counts          = report.counts[Item_kind::local_mesh];
    Export_kind_counts* bvh_counts      = bvh_items ? &report.counts[Item_kind::raytrace_bvh] : nullptr;
    Export_kind_counts& file_counts     = files[file_index].counts[Item_kind::local_mesh];
    Export_kind_counts* file_bvh_counts = bvh_items ? &files[file_index].counts[Item_kind::raytrace_bvh] : nullptr;
    for (const auto& [key, indices] : shapes_by_key) {
        if (shapes[indices.front()].file_index != file_index) {
            continue;
        }
        if (!is_local_key_asked(indices)) {
            continue; // drawn only by meshes the bake admits: their World-space mesh items serve them
        }
        Missing_items entry{.indices = &indices};
        const std::optional<std::string> meta = store.contains(key) ? store.read_meta(key) : std::optional<std::string>{};
        if (meta.has_value()) {
            ++counts.present;
            ++file_counts.present;
            for (const std::size_t i : indices) {
                shapes[i].meta     = meta.value();
                shapes[i].has_item = true;
            }
        } else {
            entry.mesh = true;
        }
        if (bvh_items) {
            const Key& bvh_key = shapes[indices.front()].bvh_key;
            const std::optional<std::string> bvh_meta = store.contains(bvh_key) ? store.read_meta(bvh_key) : std::optional<std::string>{};
            if (bvh_meta.has_value()) {
                ++bvh_counts->present;
                ++file_bvh_counts->present;
                for (const std::size_t i : indices) {
                    shapes[i].bvh_meta     = bvh_meta.value();
                    shapes[i].has_bvh_item = true;
                }
            } else {
                entry.bvh = true;
            }
        }
        if (entry.mesh || entry.bvh) {
            missing.push_back(entry);
        }
    }
    begin_stage(Scene_export_stage::mesh_items, missing.size());
    std::vector<char>        made    (missing.size(), 0);
    std::vector<char>        bvh_made(missing.size(), 0);
    std::vector<std::string> bvh_failures(missing.size());
    run_parallel(executor, missing.size(), [this, &store, &missing, &made, &bvh_made, &bvh_failures](const std::size_t m) {
        const Missing_items&            entry   = missing[m];
        const std::vector<std::size_t>& indices = *entry.indices;
        Export_shape& first = shapes[indices.front()];
        const Local_mesh_items_wanted wanted = !entry.bvh
            ? Local_mesh_items_wanted::mesh
            : entry.mesh ? Local_mesh_items_wanted::mesh_and_bvh : Local_mesh_items_wanted::bvh;
        Local_mesh_items items;
        if (first.source == Local_mesh_source::soup) {
            items = make_local_mesh_items(*first.shape, first.source, first.lod_settings, gpu_vertex_formats, wanted);
        } else {
            std::lock_guard<std::mutex> geometry_lock{*geometry_mutexes.at(first.geometry)};
            items = make_local_mesh_items(*first.shape, first.source, first.lod_settings, gpu_vertex_formats, wanted);
        }
        if (!items.skip_reason.empty()) {
            if (entry.mesh) {
                for (const std::size_t i : indices) {
                    shapes[i].skip_reason = items.skip_reason;
                }
            } else {
                bvh_failures[m] = items.skip_reason;
            }
            advance();
            return;
        }
        if (entry.mesh) {
            const erhe::gltf::Gltf_mesh_item& item = items.mesh.value();
            const bool stored = store.put(erhe::asset_store::Item_data{
                .key     = first.key,
                .kind    = Item_kind::local_mesh,
                .target  = {},
                .stamp   = mesh_stamp,
                .meta    = item.meta,
                .info    = {},
                .payload = item.payload
            });
            if (!stored) {
                for (const std::size_t i : indices) {
                    shapes[i].skip_reason = "the editor asset did not store the item";
                }
                advance();
                return;
            }
            made[m] = 1;
            for (const std::size_t i : indices) {
                shapes[i].meta     = item.meta;
                shapes[i].has_item = true;
            }
        }
        if (entry.bvh) {
            if (!items.bvh.has_value()) {
                bvh_failures[m] = items.bvh_failure;
            } else {
                const erhe::gltf::Gltf_raytrace_bvh_item& bvh_item = items.bvh.value();
                const bool stored = store.put(erhe::asset_store::Item_data{
                    .key     = first.bvh_key,
                    .kind    = Item_kind::raytrace_bvh,
                    .target  = {},
                    .stamp   = bvh_stamp,
                    .meta    = bvh_item.meta,
                    .info    = {},
                    .payload = bvh_item.payload
                });
                if (stored) {
                    bvh_made[m] = 1;
                    for (const std::size_t i : indices) {
                        shapes[i].bvh_meta     = bvh_item.meta;
                        shapes[i].has_bvh_item = true;
                    }
                } else {
                    bvh_failures[m] = "the editor asset did not store the item";
                }
            }
        }
        advance();
    });
    const std::size_t made_count     = static_cast<std::size_t>(std::count(made.begin(), made.end(), char{1}));
    const std::size_t bvh_made_count = static_cast<std::size_t>(std::count(bvh_made.begin(), bvh_made.end(), char{1}));
    counts.made      += made_count;
    file_counts.made += made_count;
    if (bvh_counts != nullptr) {
        bvh_counts->made      += bvh_made_count;
        file_bvh_counts->made += bvh_made_count;
    }
    std::map<std::string, std::size_t> bvh_failure_counts;
    for (const std::string& failure : bvh_failures) {
        if (!failure.empty()) {
            ++bvh_failure_counts[failure];
        }
    }
    for (const auto& [failure, count] : bvh_failure_counts) {
        log_export->warn("Export: {} mesh item(s) written without a Raytrace BVH item: {}", count, failure);
    }
    return true;
}

void Scene_export::Impl::log_inline_shapes()
{
    std::map<std::pair<Local_mesh_source, std::string>, std::size_t> skip_reasons;
    for (const Export_shape& entry : shapes) {
        if (entry.exported && entry.unbaked_user && !entry.has_item) {
            ++report.inline_primitive_count;
            ++skip_reasons[{entry.source, entry.skip_reason}];
        }
    }
    // A baked mesh primitive without a World-space mesh item: its mesh is written by the exporter's own bake.
    for (const Export_world_primitive& entry : world_primitives) {
        if (!entry.has_item && shapes[entry.shape_index].exported) {
            ++report.inline_primitive_count;
        }
    }
    for (const auto& [source_and_reason, count] : skip_reasons) {
        log_export->warn(
            "Export: {} {} primitive(s) written inside the Scene item: {}",
            count,
            (source_and_reason.first == Local_mesh_source::soup) ? "soup" : "geometry-normative (ERHE_geometry)",
            source_and_reason.second
        );
    }
}

auto Scene_export::Impl::ensure_scene_item(std::string& out_scene_item, Key& out_scene_key) -> bool
{
    ERHE_PROFILE_FUNCTION();
    begin_stage(Scene_export_stage::scene_item, 0);
    out_scene_item = export_scene_save_gltf(*save);
    if (out_scene_item.empty()) {
        log_export->error("Export: the Scene item of '{}' was not written", scene_name);
        return false;
    }
    const std::span<const std::byte> bytes = std::as_bytes(std::span<const char>{out_scene_item.data(), out_scene_item.size()});
    out_scene_key = make_scene_item_key(target, scene_stamp, bytes);
    Asset_store&        store  = editor_asset->get_store();
    Export_kind_counts& counts = report.counts[Item_kind::scene];
    if (store.contains(out_scene_key)) {
        ++counts.present;
        return true;
    }
    const bool stored = store.put(erhe::asset_store::Item_data{
        .key     = out_scene_key,
        .kind    = Item_kind::scene,
        .target  = target,
        .stamp   = scene_stamp,
        .meta    = "{}",
        .info    = {},
        .payload = bytes
    });
    if (!stored) {
        log_export->error("Export: the editor asset did not store the Scene item of '{}'", scene_name);
        return false;
    }
    ++counts.made;
    return true;
}

auto Scene_export::Impl::copy(const std::string& scene_item, const Key& scene_key) -> bool
{
    ERHE_PROFILE_FUNCTION();
    const std::optional<std::vector<Key>> item_keys = collect_scene_item_keys(std::as_bytes(std::span<const char>{scene_item.data(), scene_item.size()}));
    if (!item_keys.has_value()) {
        log_export->error("Export: the Scene item of '{}' does not parse", scene_name);
        return false;
    }
    std::set<Key> referenced{item_keys->begin(), item_keys->end()};
    referenced.insert(scene_key);
    if (pvs_key.has_value() && !referenced.contains(pvs_key.value())) {
        // The exporter left ERHE_pvs out: a column names a node the Scene item does not hold (logged by erhe.gltf).
        if (pvs_mode == Pvs_mode::rebake) {
            log_export->error("Export of '{}': the baked PVS names nodes the Scene item does not hold - Export failed", scene_name);
            return false;
        }
        log_export->warn("Export of '{}': pvs_mode reuse: the PVS item names nodes the Scene item does not hold - the prepared asset holds no PVS", scene_name);
        report.counts[Item_kind::pvs].present = 0;
        pvs_key.reset();
    }
    begin_stage(Scene_export_stage::copy, referenced.size());

    Asset_store& source = editor_asset->get_store();
    // R10.3: each item comes from the editor asset that holds it (R10.2); the rest are the scene's own.
    std::map<Key, Asset_store*> item_stores;
    for (const Export_shape& shape : shapes) {
        if (shape.has_item) {
            item_stores.try_emplace(shape.key, &get_store(shape.file_index));
        }
        if (shape.has_bvh_item) {
            item_stores.try_emplace(shape.bvh_key, &get_store(shape.file_index));
        }
    }
    for (const Export_texture& texture : textures) {
        if (texture.has_item) {
            item_stores.try_emplace(texture.key, &get_store(texture.file_index));
        }
    }
    // The kind of each key Ensure asked for, for the error naming a key Copy does not find.
    std::map<Key, Item_kind> item_kinds{{scene_key, Item_kind::scene}};
    if (pvs_key.has_value()) {
        item_kinds.emplace(pvs_key.value(), Item_kind::pvs);
    }
    for (const Export_shape& shape : shapes) {
        item_kinds.emplace(shape.key, Item_kind::local_mesh);
        item_kinds.emplace(shape.bvh_key, Item_kind::raytrace_bvh);
    }
    for (const Export_world_primitive& entry : world_primitives) {
        item_kinds.emplace(entry.key, Item_kind::world_mesh);
        item_kinds.emplace(entry.bvh_key, Item_kind::raytrace_bvh);
    }
    for (const Export_texture& texture : textures) {
        item_kinds.emplace(texture.key, Item_kind::texture);
    }
    // R9.3: Copy writes the prepared asset anew from the editor assets alone; nothing of an earlier one is read.
    Prepared_write_request request{
        .prepared_asset_path = prepared_asset_path,
        .items               = {},
        .source              = erhe::asset_store::Source_record{
            .relative_path = relative_path_text(source_path, prepared_asset_path.parent_path()),
            .scene_id      = scene_id
        },
        .scene               = erhe::asset_store::Prepared_scene_record{.target = target, .scene_key = scene_key},
        .stamps              = Prepared_stamp_records{
            .mesh_stamp     = mesh_stamp,
            .bvh_stamp      = bvh_items ? bvh_stamp : std::string{c_prepared_no_bvh_stamp},
            .scene_stamp    = scene_stamp,
            .texture_stamps = {}
        },
        .advance             = [this]() { advance(); }
    };
    request.items.reserve(referenced.size());
    for (const Key& key : referenced) {
        const auto store_i = item_stores.find(key);
        const auto kind_i  = item_kinds.find(key);
        request.items.push_back(Prepared_write_item{
            .key   = key,
            .kind  = (kind_i != item_kinds.end()) ? kind_i->second : Item_kind::editor_data,
            .store = (store_i != item_stores.end()) ? store_i->second : &source
        });
    }
    // R11.4, R11.6: the stamps of every item put in, read by a prepared run's open in place of the items.
    for (const Export_texture& texture : textures) {
        if (texture.has_item && referenced.contains(texture.key)) {
            request.stamps.texture_stamps.push_back(
                (texture.item_source == Texture_item_source::source_bytes) ? make_source_texture_stamp() : texture_stamp
            );
        }
    }
    const Prepared_write_result written = write_prepared_asset(request);
    if (!written.error.empty()) {
        fail(fmt::format("Export of '{}' failed: {}; '{}' is unchanged", scene_name, written.error, erhe::file::to_string(prepared_asset_path)));
        return false;
    }
    for (const auto& [kind, count] : written.written) {
        report.counts[kind].written += count;
    }
    report.prepared_item_count  = written.item_count;
    report.prepared_asset_bytes = written.bytes;

    erhe::asset_store::Write_transaction transaction{source};
    if (
        !transaction.is_active() ||
        !transaction.set_prepared_path(target, relative_path_text(prepared_asset_path, editor_asset->get_asset_path().parent_path())) ||
        !transaction.commit()
    ) {
        log_export->warn("Export: the prepared path record of '{}' was not written", erhe::file::to_string(editor_asset->get_asset_path()));
    }
    report.scene_key = scene_key.to_hex();
    return true;
}

auto Scene_export::Impl::resolve_pvs_item(const Scene_export_mode job) -> bool
{
    erhe::gltf::Gltf_export_arguments& arguments = get_scene_save_export_arguments(*save);
    arguments.pvs_item.reset();
    pvs_key.reset();
    if (pvs_mode == Pvs_mode::off) {
        return true;
    }
    Asset_store& store = editor_asset->get_store();
    std::optional<Key> key;
    if ((job == Scene_export_mode::export_items) && (pvs_mode == Pvs_mode::rebake)) {
        if (!baked_pvs.has_value()) {
            log_export->error("Export of '{}': pvs_mode rebake made no PVS item", scene_name);
            return false;
        }
        key = baked_pvs->key;
    } else {
        const std::optional<std::string> record = store.get_record(c_pvs_key_record);
        key = record.has_value() ? Key::from_hex(record.value()) : std::nullopt;
    }
    const std::optional<std::string> meta = (key.has_value() && store.contains(key.value())) ? store.read_meta(key.value()) : std::nullopt;
    if (!meta.has_value()) {
        if (job == Scene_export_mode::export_items) {
            log_export->info("Export of '{}': pvs_mode reuse: the editor asset holds no PVS item - the prepared asset holds none", scene_name);
        }
        return true;
    }
    if ((job == Scene_export_mode::export_items) && (pvs_mode == Pvs_mode::reuse)) {
        log_export->warn(
            "Export of '{}': pvs_mode reuse: the PVS item {} is copied as it is, with no check that it matches the exported geometry",
            scene_name, key->to_hex()
        );
    }
    if (job == Scene_export_mode::export_items) {
        Export_kind_counts& counts = report.counts[Item_kind::pvs];
        if (baked_pvs.has_value() && baked_pvs->made) {
            ++counts.made;
        } else {
            ++counts.present;
        }
    }
    arguments.pvs_item = erhe::gltf::Gltf_item_reference{.uri = make_item_uri(key.value()), .meta = meta.value()};
    pvs_key = key;
    return true;
}

auto Scene_export::Impl::install_form() -> std::size_t
{
    std::size_t form_mesh_count = 0;
    for (const World_space_form_mesh& form_mesh : form_meshes) {
        bool complete = true;
        for (std::size_t i = 0, end = form_mesh.shapes.size(); i < end; ++i) {
            if ((form_mesh.shapes[i] != nullptr) && !form_mesh.world_primitives[i]) {
                complete = false;
            }
        }
        form_mesh_count += complete ? 1 : 0;
    }
    report.world_form_mesh_count = form_mesh_count;
    scene_root.get_world_space_form().install(scene_root, std::move(form_meshes));
    form_meshes.clear();
    form_installed = true;
    return form_mesh_count;
}

auto store_pvs_item(App_context& context, const Scene_root& scene_root, const erhe::scene::Pvs_database& database) -> std::optional<Stored_pvs_item>
{
    ERHE_PROFILE_FUNCTION();
    const std::filesystem::path& source_path = scene_root.get_source_path();
    if (source_path.empty() || (context.editor_asset_registry == nullptr)) {
        log_export->error("PVS item of '{}': the scene has no source file, so no editor asset", scene_root.get_name());
        return std::nullopt;
    }
    const std::shared_ptr<Editor_asset> editor_asset = context.editor_asset_registry->get(source_path, scene_root.get_editor_asset_record());
    if (!editor_asset) {
        return std::nullopt;
    }
    const std::optional<erhe::gltf::Gltf_pvs_item> item = erhe::gltf::write_pvs_item(database);
    if (!item.has_value()) {
        return std::nullopt;
    }
    const std::string stamp = make_pvs_stamp();
    const Key         key   = make_pvs_key(stamp, item->meta, std::span<const std::byte>{item->payload});
    Asset_store&      store = editor_asset->get_store();
    std::vector<Key>  previous;
    if (!store.for_each_item([&previous, &key](const erhe::asset_store::Item_info& info) {
        if ((info.kind == Item_kind::pvs) && (info.key != key)) {
            previous.push_back(info.key);
        }
    })) {
        log_export->error("PVS item of '{}': the items of '{}' cannot be listed", scene_root.get_name(), erhe::file::to_string(editor_asset->get_asset_path()));
        return std::nullopt;
    }
    const bool made = !store.contains(key);
    {
        erhe::asset_store::Write_transaction transaction{store};
        bool ok = transaction.is_active();
        if (ok && made) {
            ok = transaction.put(erhe::asset_store::Item_data{
                .key     = key,
                .kind    = Item_kind::pvs,
                .target  = {},
                .stamp   = stamp,
                .meta    = item->meta,
                .info    = {},
                .payload = std::span<const std::byte>{item->payload}
            });
        }
        // R4.10: the last bake replaces the scene's PVS item.
        for (const Key& old_key : previous) {
            ok = ok && transaction.remove(old_key);
        }
        ok = ok && transaction.set_record(c_pvs_key_record, key.to_hex());
        const bool committed = ok && transaction.commit();
        editor_asset->invalidate_item_catalog();
        if (!committed) {
            log_export->error("PVS item of '{}': '{}' took no write", scene_root.get_name(), erhe::file::to_string(editor_asset->get_asset_path()));
            return std::nullopt;
        }
    }
    log_export->info(
        "PVS item {} of '{}' {} '{}' ({} bytes; {} previous PVS item(s) removed)",
        key.to_hex(), scene_root.get_name(), made ? "stored in" : "already in", erhe::file::to_string(editor_asset->get_asset_path()),
        item->payload.size(), previous.size()
    );
    return Stored_pvs_item{.key = key, .made = made, .payload_bytes = item->payload.size()};
}

// The status (doc/editor/prepared_assets.md "Status"): the keys the scene asks
// for, the Scene item made in memory when every mesh item's meta is in either
// store, then the drop, then the stores measured against the keys.
auto Scene_export::Impl::compute_status() -> bool
{
    ERHE_PROFILE_FUNCTION();
    using Clock = std::chrono::steady_clock;
    const Clock::time_point start = Clock::now();
    status.scene_name          = scene_name;
    status.target              = target;
    status.editor_asset_path   = editor_asset->get_asset_path();
    status.prepared_asset_path = prepared_asset_path;
    status.drop                = drop;

    Asset_store& store = editor_asset->get_store();
    std::unique_ptr<Asset_store> prepared;
    {
        std::error_code error_code;
        if (std::filesystem::is_regular_file(prepared_asset_path, error_code)) {
            prepared = Asset_store::open(prepared_asset_path, erhe::asset_store::Open_mode::prepared_read_only);
        }
    }

    if (!open_dependency_files()) {
        return false;
    }
    compute_mesh_keys();
    compute_world_keys();
    status.hash_pending_count  = hash_pending_count;
    status.computed_hash_count = computed_hash_count;

    // What each file's editor asset holds, as one listing read it (re-read after a write, Editor_asset::invalidate_item_catalog()).
    const bool key_check = (mode == Scene_export_mode::key_check);
    std::vector<std::shared_ptr<const Item_catalog>> catalogs(files.size());
    for (const std::size_t file_index : ensure_order) {
        catalogs[file_index] = files[file_index].editor_asset->get_item_catalog();
        if (!catalogs[file_index]) {
            log_export->error("Status: the items of '{}' cannot be listed", erhe::file::to_string(files[file_index].editor_asset->get_asset_path()));
            return false;
        }
    }

    // The keys asked of each file's editor asset (R10.2), and all of them.
    std::vector<std::map<Item_kind, std::set<Key>>> asked_by_file(files.size());
    const auto ask = [&asked_by_file](const std::size_t file_index, const Item_kind kind, const Key& key) {
        asked_by_file[file_index][kind].insert(key);
    };
    std::map<Item_kind, std::set<Key>> asked;
    std::set<Key>& mesh_keys  = asked[Item_kind::local_mesh];
    std::set<Key>& scene_keys = asked[Item_kind::scene];
    std::set<Key>* bvh_keys   = bvh_items ? &asked[Item_kind::raytrace_bvh] : nullptr;
    // A key check asks presence alone (the metas serve the Scene item export of a status job): an empty meta.
    const auto read_meta = [this, &prepared, &catalogs, key_check](const std::size_t file_index, const Key& key) -> std::optional<std::string> {
        if (key_check) {
            if (catalogs[file_index]->items.contains(key) || (prepared && prepared->contains(key))) {
                return std::string{};
            }
            return std::nullopt;
        }
        std::optional<std::string> meta = get_store(file_index).read_meta(key);
        if (!meta.has_value() && prepared) {
            meta = prepared->read_meta(key);
        }
        return meta;
    };
    std::size_t unknown_meta_count     = 0;
    std::size_t unknown_bvh_meta_count = 0;
    for (const auto& [key, indices] : shapes_by_key) {
        if (!is_local_key_asked(indices)) {
            continue;
        }
        const std::size_t file_index = shapes[indices.front()].file_index;
        mesh_keys.insert(key);
        ask(file_index, Item_kind::local_mesh, key);
        const std::optional<std::string> meta = read_meta(file_index, key);
        if (meta.has_value()) {
            for (const std::size_t i : indices) {
                shapes[i].meta     = meta.value();
                shapes[i].has_item = true;
            }
        } else {
            ++unknown_meta_count;
        }
        if (bvh_keys != nullptr) {
            const Key& bvh_key = shapes[indices.front()].bvh_key;
            bvh_keys->insert(bvh_key);
            ask(file_index, Item_kind::raytrace_bvh, bvh_key);
            const std::optional<std::string> bvh_meta = read_meta(file_index, bvh_key);
            if (bvh_meta.has_value()) {
                for (const std::size_t i : indices) {
                    shapes[i].bvh_meta     = bvh_meta.value();
                    shapes[i].has_bvh_item = true;
                }
            } else {
                ++unknown_bvh_meta_count;
            }
        }
    }
    // The World-space mesh items of the admitted nodes (R13.1: how many are valid).
    std::size_t unknown_world_meta_count = 0;
    if (!world_primitives_by_key.empty()) {
        std::set<Key>& world_keys = asked[Item_kind::world_mesh];
        for (const auto& [key, indices] : world_primitives_by_key) {
            world_keys.insert(key);
            ask(0, Item_kind::world_mesh, key);
            const std::optional<std::string> meta = read_meta(0, key);
            if (meta.has_value()) {
                for (const std::size_t i : indices) {
                    world_primitives[i].meta     = meta.value();
                    world_primitives[i].has_item = true;
                }
            } else {
                ++unknown_world_meta_count;
            }
            if (bvh_keys != nullptr) {
                const Key& bvh_key = world_primitives[indices.front()].bvh_key;
                bvh_keys->insert(bvh_key);
                ask(0, Item_kind::raytrace_bvh, bvh_key);
                const std::optional<std::string> bvh_meta = read_meta(0, bvh_key);
                if (bvh_meta.has_value()) {
                    for (const std::size_t i : indices) {
                        world_primitives[i].bvh_meta     = bvh_meta.value();
                        world_primitives[i].has_bvh_item = true;
                    }
                } else {
                    ++unknown_bvh_meta_count;
                }
            }
        }
    }
    for (const Export_shape& entry : shapes) {
        if (entry.exported && entry.unbaked_user && !entry.key_known && !entry.hash_pending) {
            ++status.inline_primitive_count;
        }
    }
    compute_texture_keys();
    std::set<Key>& texture_keys       = asked[Item_kind::texture];
    std::size_t    unknown_texture_meta_count = 0;
    for (const auto& [key, indices] : textures_by_key) {
        const std::size_t file_index = textures[indices.front()].file_index;
        texture_keys.insert(key);
        ask(file_index, Item_kind::texture, key);
        const std::optional<std::string> meta = read_meta(file_index, key);
        if (meta.has_value()) {
            for (const std::size_t i : indices) {
                textures[i].meta     = meta.value();
                textures[i].has_item = true;
            }
        } else {
            ++unknown_texture_meta_count;
        }
    }
    if (texture_keys.empty()) {
        asked.erase(Item_kind::texture);
    }
    // Editor asset only (R4.1): counted per kind and kept by drops, never compared with the prepared asset.
    if (!textures_by_basis_key.empty()) {
        std::set<Key>& basis_keys = asked[Item_kind::basis_texture];
        for (const auto& [key, indices] : textures_by_basis_key) {
            basis_keys.insert(key);
            ask(textures[indices.front()].file_index, Item_kind::basis_texture, key);
        }
    }
    // The PVS item the Scene item would name: the editor asset's (reuse; rebake: the last bake's).
    if (!resolve_pvs_item(Scene_export_mode::status)) {
        return false;
    }
    std::size_t unknown_pvs_count = 0;
    if (pvs_key.has_value()) {
        asked[Item_kind::pvs].insert(pvs_key.value());
        ask(0, Item_kind::pvs, pvs_key.value());
    } else if (pvs_mode == Pvs_mode::rebake) {
        ++unknown_pvs_count;
    }

    // The Scene item Export would write, in memory; a key check leaves it to a status job, which holds the scene read-only.
    std::optional<Key> scene_key;
    const bool every_item_known =
        (hash_pending_count == 0) &&
        (unknown_meta_count == 0) && (unknown_world_meta_count == 0) && (unknown_bvh_meta_count == 0) && (unknown_texture_meta_count == 0) && (unknown_pvs_count == 0);
    if (key_check && every_item_known && known_scene_key.has_value()) {
        scene_key = known_scene_key;
        scene_keys.insert(scene_key.value());
        ask(0, Item_kind::scene, scene_key.value());
        status.scene_key = scene_key->to_hex();
    } else if (key_check && (every_item_known || (hash_pending_count > 0))) {
        status.scene_key_checked = false;
        status.mismatch = (hash_pending_count > 0)
            ? fmt::format("the content hashes of {} geometry-normative primitive(s) are being computed", hash_pending_count)
            : std::string{"the Scene item is being checked"};
    } else if (every_item_known) {
        begin_stage(Scene_export_stage::scene_item, 0);
        const std::string scene_item = export_scene_save_gltf(*save);
        if (scene_item.empty()) {
            log_export->error("Status: the Scene item of '{}' was not written", scene_name);
            return false;
        }
        scene_key = make_scene_item_key(target, scene_stamp, std::as_bytes(std::span<const char>{scene_item.data(), scene_item.size()}));
        scene_keys.insert(scene_key.value());
        ask(0, Item_kind::scene, scene_key.value());
        status.scene_key = scene_key->to_hex();
    } else {
        status.mismatch = (unknown_pvs_count > 0)
            ? std::string{"pvs_mode is rebake and the editor asset holds no PVS item: Export bakes it"}
            : fmt::format(
                "{} mesh item(s), {} world-space mesh item(s), {} BVH item(s) and {} texture item(s) are in neither store: Export makes them",
                unknown_meta_count, unknown_world_meta_count, unknown_bvh_meta_count, unknown_texture_meta_count
            );
    }
    if (prepared) {
        const std::optional<erhe::asset_store::Prepared_scene_record> record = prepared->get_prepared_scene();
        if (record.has_value()) {
            status.prepared_scene_key = record->scene_key.to_hex();
        }
    }

    // R8.13
    if (drop != Prepared_drop::none) {
        begin_stage(Scene_export_stage::drop_items, 0);
    }
    if (drop == Prepared_drop::prepared_asset) {
        if (prepared) {
            status.dropped_count     = prepared->get_item_count().value_or(0);
            status.bytes_before_drop = prepared->get_database_size().value_or(0);
            prepared.reset();
        }
        std::error_code error_code;
        std::filesystem::remove(prepared_asset_path, error_code);
        if (error_code) {
            log_export->error("Drop: '{}' was not removed: {}", erhe::file::to_string(prepared_asset_path), error_code.message());
            return false;
        }
        log_export->info("Drop: prepared asset '{}' removed", erhe::file::to_string(prepared_asset_path));
    } else if (drop != Prepared_drop::none) {
        std::set<Key> all_asked;
        for (const auto& [kind, keys] : asked) {
            all_asked.insert(keys.begin(), keys.end());
        }
        std::vector<Key> drop_keys;
        if (!select_drop_keys(store, drop, all_asked, target, drop_keys)) {
            log_export->error("Drop: the items of '{}' cannot be listed", erhe::file::to_string(status.editor_asset_path));
            return false;
        }
        status.bytes_before_drop = store.get_database_size().value_or(0);
        const bool dropped = drop_store_items(store, drop_keys);
        editor_asset->invalidate_item_catalog();
        catalogs[0] = editor_asset->get_item_catalog();
        if (!dropped || !catalogs[0]) {
            return false;
        }
        status.dropped_count = drop_keys.size();
        log_export->info(
            "Drop ({}): {} items removed from '{}'", c_str(drop), drop_keys.size(), erhe::file::to_string(status.editor_asset_path)
        );
    }

    begin_stage(Scene_export_stage::check_items, 0);
    // Each file's editor asset against what the scene asks of it; a shared file's other items are unreferenced from this scene's view (R10.5).
    for (const std::size_t file_index : ensure_order) {
        Asset_store& file_store = get_store(file_index);
        measure_catalog_items(*catalogs[file_index], asked_by_file[file_index], status.kinds);
        const Export_file& file = files[file_index];
        status.files.push_back(Prepared_file_status{
            .source_path        = file.source_path,
            .editor_asset_path  = file.editor_asset->get_asset_path(),
            .role               = c_str(file.role),
            .depth              = file.depth,
            .location           = c_str(file.editor_asset->get_location()),
            .editor_asset_bytes = file_store.get_database_size().value_or(0)
        });
    }
    if (!scene_key.has_value() && status.scene_key_checked) {
        ++status.kinds[Item_kind::scene].missing;
    }
    for (const auto& [kind, keys] : asked) {
        status.asked_key_count += keys.size();
    }
    status.editor_asset_bytes = store.get_database_size().value_or(0);
    if (prepared) {
        status.prepared_asset_exists = true;
        status.prepared_asset_bytes  = prepared->get_database_size().value_or(0);
        status.prepared_item_count   = prepared->get_item_count().value_or(0);
        if (scene_key.has_value()) {
            std::set<Key> all_asked;
            for (const auto& [kind, keys] : asked) {
                if (kind != Item_kind::basis_texture) {
                    all_asked.insert(keys.begin(), keys.end());
                }
            }
            status.matches = compare_prepared_items(*prepared, all_asked, scene_key.value(), target, status.mismatch);
        }
    } else if (status.mismatch.empty()) {
        status.mismatch = "there is no prepared asset";
    }
    status.compute_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return true;
}

// Bake World-Space Meshes (R8.7): the missing World-space mesh items, then the form drawn from them.
auto Scene_export::Impl::bake_world_meshes() -> bool
{
    ERHE_PROFILE_FUNCTION();
    using Clock = std::chrono::steady_clock;
    const Clock::time_point start = Clock::now();
    Asset_store& store = editor_asset->get_store();
    {
        erhe::asset_store::Write_transaction transaction{store};
        if (
            !transaction.is_active() ||
            !transaction.set_source_record(erhe::asset_store::Source_record{
                .relative_path = relative_path_text(source_path, editor_asset->get_asset_path().parent_path()),
                .scene_id      = scene_id
            }) ||
            !transaction.commit()
        ) {
            log_export->error("Bake World-Space Meshes: the source record of '{}' was not written", erhe::file::to_string(editor_asset->get_asset_path()));
            return false;
        }
    }
    compute_mesh_keys();
    compute_world_keys();
    const bool ok = ensure_world_mesh_items();
    if (!ok) {
        return false;
    }
    report.ensure_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    const Clock::time_point form_start = Clock::now();
    load_world_form();
    report.scene_ms = std::chrono::duration<double, std::milli>(Clock::now() - form_start).count();
    return true;
}

// The world-space form (doc/editor/world_space_bake.md "Two forms"): one
// document naming every World-space mesh item, parsed through the editor
// asset and built like a load (the uploads go through the loader queue).
void Scene_export::Impl::load_world_form()
{
    ERHE_PROFILE_FUNCTION();
    begin_stage(Scene_export_stage::world_form, 0);
    if (!graphics_device->supports_worker_contexts()) {
        log_export->warn("Bake World-Space Meshes: the device builds no meshes on workers - the world-space form is not loaded");
        return;
    }
    std::map<Key, std::size_t>                        document_indices;
    std::vector<erhe::gltf::Gltf_mesh_item_reference> references;
    for (const Export_world_primitive& entry : world_primitives) {
        if (!entry.has_item || document_indices.contains(entry.key)) {
            continue;
        }
        document_indices.emplace(entry.key, references.size());
        erhe::gltf::Gltf_mesh_item_reference reference{.uri = make_item_uri(entry.key), .meta = entry.meta};
        if (entry.has_bvh_item) {
            reference.raytrace_bvh = erhe::gltf::Gltf_item_reference{.uri = make_item_uri(entry.bvh_key), .meta = entry.bvh_meta};
        }
        references.push_back(std::move(reference));
    }
    if (references.empty()) {
        return;
    }
    const std::shared_ptr<erhe::scene::Node> document_root = std::make_shared<erhe::scene::Xform>("World-space mesh items");
    erhe::gltf::Gltf_export_arguments export_arguments{.root_node = *document_root, .binary = true};
    export_arguments.mesh_item_document = std::move(references);
    const std::string document = erhe::gltf::export_gltf(export_arguments);
    if (document.empty()) {
        log_export->error("Bake World-Space Meshes: the document of the World-space mesh items was not written");
        return;
    }
    const std::shared_ptr<erhe::scene::Node> container = std::make_shared<erhe::scene::Xform>("World Space Form");
    const std::shared_ptr<Editor_asset> asset = editor_asset;
    erhe::gltf::Gltf_parse_arguments parse_arguments{
        .executor               = *executor,
        .device_options         = form_device_options,
        .root_node              = container,
        .mesh_layer_id          = Mesh_layer_id::content,
        .path                   = editor_asset->get_asset_path(),
        .parallel               = true,
        .glb_data               = std::as_bytes(std::span<const char>{document.data(), document.size()}),
        .document               = erhe::gltf::Gltf_document::item,
        .triangle_soup_reload   = form_triangle_soup_reload,
        .mesh_payload           = erhe::gltf::Mesh_payload_load::in_parse,
        .buffer_resolver        = [asset](const std::string_view uri) -> std::shared_ptr<const erhe::gltf::Gltf_buffer_reader> {
            const std::optional<Key> key = parse_item_uri(uri);
            if (!key.has_value() || !asset->get_store().contains(key.value())) {
                return {};
            }
            return std::make_shared<Editor_asset_item_reader>(asset, key.value());
        }
    };
    form_gltf_data = erhe::gltf::parse_gltf(parse_arguments);
    if (!form_gltf_data.errors.empty()) {
        log_export->error("Bake World-Space Meshes: the World-space mesh items do not parse: {}", form_gltf_data.errors.front());
        return;
    }
    std::size_t primitive_count = 0;
    const std::vector<Imported_build_job> jobs = collect_imported_build_jobs(
        form_gltf_data, form_build_info.value(), form_skinned_build_info.value(), form_base_variant, true, primitive_count
    );
    run_parallel(executor, jobs.size(), [this, &jobs](const std::size_t i) {
        static_cast<void>(run_imported_build_job(jobs[i], *graphics_device, true, Stored_vertex_streams_use::use));
    });
    erhe::gltf::close_source_files(form_gltf_data);
    form_ticket = mesh_memory->get_loader_transfer_queue().get_last_ticket();

    // Each admitted mesh's primitives, from the document's meshes (node i: item i).
    std::vector<std::shared_ptr<erhe::primitive::Primitive>> document_primitives(form_gltf_data.nodes.size());
    for (std::size_t i = 0, end = form_gltf_data.nodes.size(); i < end; ++i) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(form_gltf_data.nodes[i]);
        if (mesh && !mesh->get_primitives().empty()) {
            document_primitives[i] = mesh->get_primitives().front().primitive;
        }
    }
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : bake_meshes) {
        World_space_form_mesh form_mesh{};
        form_mesh.mesh = mesh;
        const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
        form_mesh.shapes.resize(primitives.size(), nullptr);
        form_mesh.world_primitives.resize(primitives.size());
        for (std::size_t p = 0, end = primitives.size(); p < end; ++p) {
            form_mesh.shapes[p] = primitives[p].primitive ? primitives[p].primitive->render_shape.get() : nullptr;
            const auto i = world_primitive_indices.find(std::pair{static_cast<const erhe::scene::Mesh*>(mesh.get()), p});
            if (i == world_primitive_indices.end()) {
                continue;
            }
            const Export_world_primitive& entry = world_primitives[i->second];
            form_mesh.world_from_node = entry.world_from_node;
            const auto document_index = entry.has_item ? document_indices.find(entry.key) : document_indices.end();
            if ((document_index != document_indices.end()) && (document_index->second < document_primitives.size())) {
                form_mesh.world_primitives[p] = document_primitives[document_index->second];
            }
        }
        form_meshes.push_back(std::move(form_mesh));
    }
}

auto run_scene_export(Scene_export& scene_export) -> bool
{
    ERHE_PROFILE_FUNCTION();
    Scene_export::Impl& impl = *scene_export.impl;
    if (!impl.editor_asset) {
        log_export->error("{}", impl.error);
        return false;
    }
    if ((impl.mode == Scene_export_mode::status) || (impl.mode == Scene_export_mode::key_check)) {
        return impl.compute_status();
    }
    if (impl.mode == Scene_export_mode::bake_world_meshes) {
        return impl.bake_world_meshes();
    }
    using Clock = std::chrono::steady_clock;
    if (impl.phase == Export_phase::ensure) {
        const Clock::time_point ensure_start = Clock::now();
        if (!impl.ensure_mesh_items()) {
            return false;
        }
        if (impl.pvs_mode == Pvs_mode::rebake) {
            // R9.2: the PVS is baked on the world-space form, loaded here from the World-space mesh items.
            impl.load_world_form();
            impl.report.ensure_ms = std::chrono::duration<double, std::milli>(Clock::now() - ensure_start).count();
            impl.phase = Export_phase::pvs_bake;
            return true;
        }
        impl.report.ensure_ms = std::chrono::duration<double, std::milli>(Clock::now() - ensure_start).count();
        impl.phase = Export_phase::scene_and_copy;
    }
    ERHE_VERIFY(impl.phase == Export_phase::scene_and_copy);
    if (!impl.resolve_pvs_item(Scene_export_mode::export_items)) {
        return false;
    }
    const Clock::time_point scene_start = Clock::now();
    std::string scene_item;
    Key         scene_key;
    if (!impl.ensure_scene_item(scene_item, scene_key)) {
        return false;
    }
    const Clock::time_point copy_start = Clock::now();
    if (!impl.copy(scene_item, scene_key)) {
        return false;
    }
    const Clock::time_point end = Clock::now();
    impl.report.scene_ms  = std::chrono::duration<double, std::milli>(copy_start - scene_start).count();
    impl.report.copy_ms   = std::chrono::duration<double, std::milli>(end - copy_start).count();
    return true;
}

auto get_scene_export_progress(const Scene_export& scene_export) -> Scene_export_progress_state
{
    const Scene_export::Impl& impl = *scene_export.impl;
    Scene_export_progress_state state{};
    {
        std::lock_guard<std::mutex> lock{impl.progress_mutex};
        state = impl.progress;
    }
    if (state.stage == Scene_export_stage::scene_item) {
        const erhe::gltf::Gltf_export_progress_state gltf_progress = get_scene_save_progress(*impl.save);
        state.done  = gltf_progress.done;
        state.total = gltf_progress.total;
    }
    return state;
}

auto is_scene_export_ready(const App_context& context, const Scene_export& scene_export) -> bool
{
    const Scene_export::Impl& impl = *scene_export.impl;
    const bool form_pending =
        (impl.mode == Scene_export_mode::bake_world_meshes) ||
        ((impl.mode == Scene_export_mode::export_items) && (impl.phase == Export_phase::pvs_bake) && !impl.form_installed);
    if (!form_pending || (context.mesh_memory == nullptr)) {
        return true;
    }
    // The form draws once its vertex and index bytes are uploaded (the publish gate of a load).
    return context.mesh_memory->get_loader_transfer_queue().get_watermark() >= impl.form_ticket;
}

void finish_scene_export(App_context& context, Scene_export& scene_export, Scene_export_report* const report)
{
    Scene_export::Impl& impl = *scene_export.impl;
    if (impl.mode == Scene_export_mode::bake_world_meshes) {
        const std::size_t form_mesh_count = impl.install_form();
        const Export_kind_counts worlds = impl.report.counts.contains(Item_kind::world_mesh) ? impl.report.counts.at(Item_kind::world_mesh) : Export_kind_counts{};
        const Export_kind_counts bvhs   = impl.report.counts.contains(Item_kind::raytrace_bvh) ? impl.report.counts.at(Item_kind::raytrace_bvh) : Export_kind_counts{};
        log_export->info(
            "Bake World-Space Meshes of '{}' done: {} mesh nodes admitted, {} drawn by the world-space form; world-space mesh items made {} present {}; "
            "BVH items made {} present {}; items {:.1f} ms, form {:.1f} ms",
            impl.scene_root.get_name(), impl.report.world_mesh_node_count, form_mesh_count,
            worlds.made, worlds.present, bvhs.made, bvhs.present, impl.report.ensure_ms, impl.report.scene_ms
        );
        if (report != nullptr) {
            *report = impl.report;
        }
        return;
    }
    finish_scene_save_gltf(context, *impl.save, nullptr);
    const Export_kind_counts meshes = impl.report.counts[Item_kind::local_mesh];
    const Export_kind_counts bvhs   = impl.report.counts.contains(Item_kind::raytrace_bvh) ? impl.report.counts.at(Item_kind::raytrace_bvh) : Export_kind_counts{};
    const Export_kind_counts textures = impl.report.counts.contains(Item_kind::texture) ? impl.report.counts.at(Item_kind::texture) : Export_kind_counts{};
    const Export_kind_counts scenes = impl.report.counts[Item_kind::scene];
    const Export_kind_counts worlds = impl.report.counts.contains(Item_kind::world_mesh) ? impl.report.counts.at(Item_kind::world_mesh) : Export_kind_counts{};
    const Export_kind_counts pvss = impl.report.counts.contains(Item_kind::pvs) ? impl.report.counts.at(Item_kind::pvs) : Export_kind_counts{};
    log_export->info(
        "Export of '{}' ({}): {} mesh nodes in world space; world-space mesh items made {} present {} written {}; "
        "pvs_mode {}: PVS items made {} present {} written {}{}",
        impl.scene_root.get_name(), impl.target, impl.report.world_mesh_node_count, worlds.made, worlds.present, worlds.written,
        to_string(impl.pvs_mode), pvss.made, pvss.present, pvss.written,
        (impl.pvs_mode == Pvs_mode::rebake) ? fmt::format("; PVS bake {:.1f} ms", impl.report.pvs_ms) : std::string{}
    );
    log_export->info(
        "Export of '{}' ({}) done: mesh items made {} present {} written {}; BVH items made {} present {} written {}; "
        "texture items made {} present {} written {}; scene items made {} present {} written {}; "
        "{} primitives and {} images inside the Scene item; prepared asset '{}' {} items {} bytes; ensure {:.1f} ms, scene {:.1f} ms, copy {:.1f} ms",
        impl.scene_root.get_name(), impl.target,
        meshes.made, meshes.present, meshes.written,
        bvhs.made, bvhs.present, bvhs.written,
        textures.made, textures.present, textures.written,
        scenes.made, scenes.present, scenes.written,
        impl.report.inline_primitive_count, impl.report.inline_image_count,
        erhe::file::to_string(impl.prepared_asset_path), impl.report.prepared_item_count, impl.report.prepared_asset_bytes,
        impl.report.ensure_ms, impl.report.scene_ms, impl.report.copy_ms
    );
    if (report != nullptr) {
        *report = impl.report;
    }
}

auto get_scene_export_mode(const Scene_export& scene_export) -> Scene_export_mode
{
    return scene_export.impl->mode;
}

auto finish_scene_status(Scene_export& scene_export) -> Prepared_status_report
{
    const Prepared_status_report& status = scene_export.impl->status;
    const Prepared_kind_status&   meshes = scene_export.impl->status.kinds[Item_kind::local_mesh];
    const Prepared_kind_status    bvhs   = status.kinds.contains(Item_kind::raytrace_bvh) ? status.kinds.at(Item_kind::raytrace_bvh) : Prepared_kind_status{};
    const Prepared_kind_status    textures = status.kinds.contains(Item_kind::texture) ? status.kinds.at(Item_kind::texture) : Prepared_kind_status{};
    const Prepared_kind_status&   scenes = scene_export.impl->status.kinds[Item_kind::scene];
    log_export->info(
        "{} of '{}' ({}): mesh items present {} missing {} unreferenced {}; BVH items present {} missing {} unreferenced {}; "
        "texture items present {} missing {} unreferenced {}; scene items present {} missing {} unreferenced {}; "
        "editor asset {} bytes; prepared asset {}; {}; drop {} removed {}; {} keys, {} content hashes computed; {:.1f} ms",
        (scene_export.impl->mode == Scene_export_mode::key_check) ? "Key check" : "Status",
        status.scene_name, status.target,
        meshes.present, meshes.missing, meshes.unreferenced_count,
        bvhs.present, bvhs.missing, bvhs.unreferenced_count,
        textures.present, textures.missing, textures.unreferenced_count,
        scenes.present, scenes.missing, scenes.unreferenced_count,
        status.editor_asset_bytes,
        status.prepared_asset_exists ? fmt::format("{} items {} bytes", status.prepared_item_count, status.prepared_asset_bytes) : std::string{"absent"},
        status.matches ? std::string{"matches"} : ("does not match: " + status.mismatch),
        c_str(status.drop), status.dropped_count,
        status.asked_key_count, status.computed_hash_count,
        status.compute_ms
    );
    return status;
}

auto get_scene_export_prepared_asset_path(const Scene_export& scene_export) -> const std::filesystem::path&
{
    return scene_export.impl->prepared_asset_path;
}

auto get_scene_export_error(const Scene_export& scene_export) -> const std::string&
{
    return scene_export.impl->error;
}

void set_scene_export_known_scene_key(Scene_export& scene_export, const Key& scene_key)
{
    scene_export.impl->known_scene_key = scene_key;
}

void collect_scene_export_world_meshes(const Scene_export& scene_export, std::vector<const erhe::scene::Mesh*>& out_meshes)
{
    out_meshes.clear();
    for (const Export_world_primitive& entry : scene_export.impl->world_primitives) {
        out_meshes.push_back(entry.mesh);
    }
}

void invalidate_scene_export_catalogs(Scene_export& scene_export)
{
    for (const Export_file& file : scene_export.impl->files) {
        if (file.editor_asset) {
            file.editor_asset->invalidate_item_catalog();
        }
    }
}

auto get_scene_export_continuation(const Scene_export& scene_export) -> Scene_export_continuation
{
    const Scene_export::Impl& impl = *scene_export.impl;
    return ((impl.mode == Scene_export_mode::export_items) && (impl.phase == Export_phase::pvs_bake))
        ? Scene_export_continuation::pvs_bake
        : Scene_export_continuation::done;
}

auto start_scene_export_pvs_bake(App_context& context, Scene_export& scene_export, const std::shared_ptr<Scene_root>& scene_root) -> bool
{
    Scene_export::Impl& impl = *scene_export.impl;
    ERHE_VERIFY((impl.phase == Export_phase::pvs_bake) && (scene_root.get() == &impl.scene_root));
    impl.begin_stage(Scene_export_stage::pvs_bake, 0);
    impl.pvs_start = std::chrono::steady_clock::now();
    const std::size_t form_mesh_count = impl.install_form();
    const World_space_form_status form = impl.scene_root.get_world_space_form().get_status();
    if (!form.is_available()) {
        log_export->error(
            "Export of '{}': the world-space form is not available ({} of {} admitted mesh node(s) missing) - the PVS cannot be baked",
            impl.scene_root.get_name(), form.missing_count, form.admitted_count
        );
        return false;
    }
    // The PVS window's bake (doc/editor/pvs.md "Bake" defaults), over the scene's region and cell size.
    Pvs_bake_request request{};
    request.method          = erhe::scene::Pvs_bake_method::disocclusion;
    request.face_resolution = 1024;
    ERHE_VERIFY(context.pvs_baker != nullptr);
    const std::string error = context.pvs_baker->start_bake(scene_root, request, Pvs_world_space_bake::done);
    if (!error.empty()) {
        log_export->error("Export of '{}': the PVS bake was refused: {}", impl.scene_root.get_name(), error);
        return false;
    }
    log_export->info("Export of '{}': PVS bake on the world-space form ({} mesh node(s) drawn from World-space mesh items) started", impl.scene_root.get_name(), form_mesh_count);
    return true;
}

auto update_scene_export_pvs_bake(App_context& context, Scene_export& scene_export) -> Scene_export_pvs_bake_state
{
    Scene_export::Impl& impl = *scene_export.impl;
    ERHE_VERIFY(context.pvs_baker != nullptr);
    const Pvs_bake_summary summary = context.pvs_baker->get_summary();
    {
        std::lock_guard<std::mutex> lock{impl.progress_mutex};
        impl.progress = Scene_export_progress_state{.stage = Scene_export_stage::pvs_bake, .done = summary.cells_done, .total = summary.cell_count};
    }
    if (summary.state == Pvs_bake_state::running) {
        return Scene_export_pvs_bake_state::running;
    }
    impl.report.pvs_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - impl.pvs_start).count();
    const Pvs_bake_status status = context.pvs_baker->get_status();
    const std::optional<Key> key = Key::from_hex(status.pvs_item_key);
    if ((summary.state != Pvs_bake_state::done) || !key.has_value()) {
        log_export->error(
            "Export of '{}': the PVS bake {}{}", impl.scene_root.get_name(), c_str(summary.state),
            status.error.empty() ? std::string{key.has_value() ? "" : " and stored no PVS item"} : (": " + status.error)
        );
        return Scene_export_pvs_bake_state::failed;
    }
    impl.baked_pvs = Stored_pvs_item{.key = key.value(), .made = status.pvs_item_made, .payload_bytes = status.pvs_item_bytes};
    impl.report.pvs_cell_count = status.cell_count;
    impl.phase = Export_phase::scene_and_copy;
    return Scene_export_pvs_bake_state::done;
}

}
