// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "parsers/gltf.hpp"

#include "parsers/gltf_extensions_export.hpp"
#include "parsers/gltf_extensions_import.hpp"
#include "parsers/gltf_physics_export.hpp"
#include "parsers/gltf_physics_import.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_scenes.hpp"
#include "assets/asset_load_task.hpp"
#include "assets/asset_manager.hpp"
#include "assets/asset_paths.hpp"
#include "content_library/content_library.hpp"
#include "scene/scene_root.hpp"
#include "operations/async_raytrace_kickoff_operation.hpp"
#include "operations/compound_operation.hpp"
#include "operations/content_library_attach_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/operation_stack.hpp"
#include "prefabs/prefab_library.hpp"

#include "scene/generated/gltf_source_reference.hpp"
#include "config/generated/editor_settings_config.hpp"

#include "items.hpp"

#include "erhe_file/file.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_gltf/gltf.hpp"
#include "erhe_gltf/image_transfer.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"

#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"

#include "scene/generated/scene_settings_serialization.hpp"

#include <fmt/format.h>
#include <simdjson.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

namespace editor {

namespace {

void color_graph(
    erhe::scene::Node*                           node,
    std::unordered_map<erhe::scene::Node*, int>& node_colors,
    const std::unordered_set<int>&               available_colors
) {
    std::unordered_set<int> colors_for_node = available_colors;
    auto parent = node->get_parent_node();
    if (parent) {
        auto i = node_colors.find(parent.get());
        if (i != node_colors.end()) {
            int parent_color = i->second;
            colors_for_node.erase(parent_color);
        }
    }
    for (const auto& child : node->get_children()) {
        auto child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
        if (!child_node) {
            continue;
        }
        auto i = node_colors.find(child_node.get());
        if (i != node_colors.end()) {
            int child_color = i->second;
            colors_for_node.erase(child_color);
        }
    }

    int node_color = *colors_for_node.begin();
    node_colors.emplace(node, node_color);

    for (auto& child : node->get_children()) {
        auto child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
        if (!child_node) {
            continue;
        }
        color_graph(child_node.get(), node_colors, available_colors);
    }
}

// Substitutes THE managed object for a parsed material everywhere in the
// parse: the materials vector (index-based consumers - ERHE_brushes,
// ERHE_node_graphs - see the shared object too) and every mesh primitive.
void substitute_material_in_parse(
    erhe::gltf::Gltf_data&                            gltf_data,
    const std::size_t                                 material_index,
    const std::shared_ptr<erhe::primitive::Material>& parsed,
    const std::shared_ptr<erhe::primitive::Material>& resolved
)
{
    gltf_data.materials[material_index] = resolved;
    for (const std::shared_ptr<erhe::scene::Node>& node : gltf_data.nodes) {
        if (!node) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
        if (!mesh) {
            continue;
        }
        const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_primitives();
        for (std::size_t i = 0, end = mesh_primitives.size(); i < end; ++i) {
            if (mesh_primitives[i].material == parsed) {
                mesh->set_primitive_material(i, resolved);
            }
        }
    }
}

// ERHE_asset_reference import (asset-manager plan phase R6): materials
// carrying the extension are proxies whose definition lives in another
// container. Each proxy resolves through Asset_manager::acquire (uid first,
// unique conforming name fallback - plan decision 11); on success the
// managed object substitutes the stub everywhere in the parse (materials
// vector + every mesh primitive), so ERHE_brushes / ERHE_node_graphs
// index-based consumers see the shared object too. On failure the stub is
// KEPT (default-material appearance) with a warning - the returned key
// makes the library list it as a reference entry either way, so the key is
// re-emitted on every save and a broken reference is never silently
// converted into a local definition or dropped.
//
// Nested references are not resolved transitively here by construction:
// Asset_manager::get_or_load_container does not resolve its parse's
// proxies (documented v1 restriction), so reference chains cannot recurse
// and cycles cannot hang the import.
auto resolve_material_asset_references(
    App_context&           context,
    erhe::gltf::Gltf_data& gltf_data
) -> std::unordered_map<const erhe::primitive::Material*, Asset_key>
{
    std::unordered_map<const erhe::primitive::Material*, Asset_key> reference_keys;
    if (context.asset_manager == nullptr) {
        return reference_keys;
    }
    simdjson::dom::parser extension_parser;
    for (std::size_t i = 0; i < gltf_data.material_extensions.size(); ++i) {
        const std::shared_ptr<erhe::primitive::Material> material = (i < gltf_data.materials.size()) ? gltf_data.materials[i] : std::shared_ptr<erhe::primitive::Material>{};
        if (!material) {
            continue;
        }
        for (const auto& [extension_name, extension_json] : gltf_data.material_extensions[i].entries) {
            if (extension_name != "ERHE_asset_reference") {
                continue;
            }
            simdjson::dom::element extension_root;
            simdjson::dom::object  extension_object;
            if (
                (extension_parser.parse(simdjson::padded_string{extension_json}).get(extension_root) != simdjson::SUCCESS) ||
                (extension_root.get_object().get(extension_object) != simdjson::SUCCESS)
            ) {
                log_parsers->error("ERHE_asset_reference on material '{}': unparsable extension JSON", material->get_name());
                continue;
            }
            std::uint64_t file_index{0};
            if (extension_object.at_key("file").get_uint64().get(file_index) != simdjson::SUCCESS) {
                log_parsers->error("ERHE_asset_reference on material '{}': missing required 'file' index", material->get_name());
                continue;
            }
            if (file_index >= gltf_data.files.size()) {
                log_parsers->error(
                    "ERHE_asset_reference on material '{}': file index {} out of range ({} files entries)",
                    material->get_name(), file_index, gltf_data.files.size()
                );
                continue;
            }
            const erhe::gltf::Gltf_file_reference& file = gltf_data.files[file_index];
            if (file.embedded || file.resolved_path.empty()) {
                log_parsers->error(
                    "ERHE_asset_reference on material '{}': files entry {} is embedded or has no resolvable path",
                    material->get_name(), file_index
                );
                continue;
            }
            std::string_view uid;
            static_cast<void>(extension_object.at_key("uid").get_string().get(uid));

            Asset_key key{
                .scope = Asset_scope::file,
                .type  = Asset_type::material,
                .path  = asset_path_to_string(normalize_asset_path(file.resolved_path)),
                .uid   = std::string{uid},
                // The proxy's own name doubles as the 2.1 conforming-name
                // fallback identifier (see the extension spec).
                .name  = material->get_name(),
            };

            std::string error;
            const std::shared_ptr<erhe::Item_base> resolved_item = context.asset_manager->acquire(key, error);
            const std::shared_ptr<erhe::primitive::Material> resolved_material = std::dynamic_pointer_cast<erhe::primitive::Material>(resolved_item);
            if (resolved_material) {
                // Self-heal upward: a name-resolved key learns the uid.
                if (key.uid.empty() && !resolved_material->get_gltf_uid().empty()) {
                    key.uid = resolved_material->get_gltf_uid();
                }
                substitute_material_in_parse(gltf_data, i, material, resolved_material);
                reference_keys.emplace(resolved_material.get(), key);
                log_parsers->info(
                    "ERHE_asset_reference: material '{}' resolved to container '{}'",
                    resolved_material->get_name(), key.path
                );
            } else if (resolved_item) {
                log_parsers->error(
                    "ERHE_asset_reference on material '{}': key {} resolved to an object of another kind - keeping the stub",
                    material->get_name(), key.describe()
                );
                reference_keys.emplace(material.get(), key);
            } else {
                log_parsers->warn(
                    "ERHE_asset_reference: material '{}' could not be resolved ({}) - keeping the stub; the key survives re-save",
                    material->get_name(), error
                );
                reference_keys.emplace(material.get(), key);
            }
        }
    }
    return reference_keys;
}

// R7 import-as-reference: route every parsed material that is not already a
// resolved R6 reference through the manager. The import's own parse
// provides the scene structure, but the material OBJECTS are acquired from
// the source container - a later import or reference of the same file
// shares them instead of duplicating definitions (import-as-copy stays the
// default). A material that cannot be acquired (no uid and an ambiguous or
// missing name - decision 11) keeps its parsed definition with a warning.
void acquire_import_materials_as_references(
    App_context&                 context,
    erhe::gltf::Gltf_data&       gltf_data,
    const std::filesystem::path& gltf_path,
    std::unordered_map<const erhe::primitive::Material*, Asset_key>& reference_keys
)
{
    if (context.asset_manager == nullptr) {
        return;
    }
    const std::string container_path = asset_path_to_string(normalize_asset_path(gltf_path));
    for (std::size_t i = 0; i < gltf_data.materials.size(); ++i) {
        const std::shared_ptr<erhe::primitive::Material> material = gltf_data.materials[i];
        if (!material || reference_keys.contains(material.get())) {
            continue; // absent, or already substituted by the R6 resolver
        }
        const Asset_key key{
            .scope = Asset_scope::file,
            .type  = Asset_type::material,
            .path  = container_path,
            .uid   = material->get_gltf_uid(),
            .name  = material->get_name(),
        };
        std::string error;
        const std::shared_ptr<erhe::Item_base> resolved_item = context.asset_manager->acquire(key, error);
        const std::shared_ptr<erhe::primitive::Material> resolved_material = std::dynamic_pointer_cast<erhe::primitive::Material>(resolved_item);
        if (!resolved_material) {
            log_parsers->warn(
                "import-as-reference: material '{}' of '{}' could not be acquired ({}) - keeping the imported definition",
                material->get_name(), container_path, error
            );
            continue;
        }
        substitute_material_in_parse(gltf_data, i, material, resolved_material);
        reference_keys.emplace(resolved_material.get(), context.asset_manager->make_key(*resolved_material));
        log_parsers->info(
            "import-as-reference: material '{}' acquired from container '{}'",
            resolved_material->get_name(), container_path
        );
    }
}

// Content-library attach operations for everything the parsed glTF carries
// besides the node tree: textures (with retained source image bytes),
// materials, skins and animations, each tagged with a Gltf_source_reference.
// Shared by the undoable import compound (make_import_gltf_operation) and
// the not-undoable scene open path (open_scene_gltf), which executes them
// inline and drops them. Materials listed in material_reference_keys (R6
// asset references, resolved or stub-fallback) attach as REFERENCE entries
// carrying their asset key instead of owning definition entries.
void append_content_library_attach_operations(
    const std::shared_ptr<Content_library>&  content_library,
    const erhe::gltf::Gltf_data&             gltf_data,
    const std::string&                       gltf_path_str,
    const std::unordered_map<const erhe::primitive::Material*, Asset_key>& material_reference_keys,
    std::vector<std::shared_ptr<Operation>>& operations
)
{
    log_parsers->info("Processing {} textures", gltf_data.images.size());
    for (size_t i = 0; i < gltf_data.images.size(); ++i) {
        const std::shared_ptr<erhe::graphics::Texture>& image = gltf_data.images[i];
        if (image) {
            operations.push_back(
                std::make_shared<Content_library_attach_operation<erhe::graphics::Texture>>(
                    content_library,
                    content_library->textures,
                    image,
                    Gltf_source_reference{
                        .gltf_path  = gltf_path_str,
                        .item_name  = image->get_name(),
                        .item_index = static_cast<int>(i),
                        .item_type  = "texture",
                    },
                    (i < gltf_data.image_sources.size()) ? gltf_data.image_sources[i] : std::shared_ptr<erhe::gltf::Gltf_image_source>{}
                )
            );
        }
    }

    log_parsers->info("Processing {} materials", gltf_data.materials.size());
    for (size_t i = 0; i < gltf_data.materials.size(); ++i) {
        const std::shared_ptr<erhe::primitive::Material>& material = gltf_data.materials[i];
        if (material) {
            const auto reference_it = material_reference_keys.find(material.get());
            const bool is_reference = (reference_it != material_reference_keys.end());
            operations.push_back(
                std::make_shared<Content_library_attach_operation<erhe::primitive::Material>>(
                    content_library,
                    content_library->materials,
                    material,
                    Gltf_source_reference{
                        .gltf_path  = gltf_path_str,
                        .item_name  = material->get_name(),
                        .item_index = static_cast<int>(i),
                        .item_type  = "material",
                    },
                    std::shared_ptr<erhe::gltf::Gltf_image_source>{},
                    is_reference,
                    is_reference ? std::optional<Asset_key>{reference_it->second} : std::optional<Asset_key>{}
                )
            );
        }
    }

    log_parsers->info("Processing {} skins", gltf_data.skins.size());
    for (size_t i = 0; i < gltf_data.skins.size(); ++i) {
        const std::shared_ptr<erhe::scene::Skin>& skin = gltf_data.skins[i];
        if (skin) {
            operations.push_back(
                std::make_shared<Content_library_attach_operation<erhe::scene::Skin>>(
                    content_library,
                    content_library->skins,
                    skin,
                    Gltf_source_reference{
                        .gltf_path  = gltf_path_str,
                        .item_name  = skin->get_name(),
                        .item_index = static_cast<int>(i),
                        .item_type  = "skin",
                    }
                )
            );
        }
    }

    log_parsers->info("Processing {} animations", gltf_data.animations.size());
    for (size_t i = 0; i < gltf_data.animations.size(); ++i) {
        const std::shared_ptr<erhe::scene::Animation>& animation = gltf_data.animations[i];
        if (animation) {
            operations.push_back(
                std::make_shared<Content_library_attach_operation<erhe::scene::Animation>>(
                    content_library,
                    content_library->animations,
                    animation,
                    Gltf_source_reference{
                        .gltf_path  = gltf_path_str,
                        .item_name  = animation->get_name(),
                        .item_index = static_cast<int>(i),
                        .item_type  = "animation",
                    }
                )
            );
        }
    }
}

// World-space bounds of the parsed glTF content: the primitive bounding
// boxes of the mesh attachments, transformed by their node's world
// transform. At the point this runs the parsed nodes still hang under the
// (unparented, identity) import root, so this is the space they enter the
// scene in. Invalid (default-constructed) when the file has no meshes.
// exclude_unlit skips primitives with an unlit (KHR_materials_unlit)
// material: sky domes and backdrops surround the scene, so framing the
// camera on them frames nothing.
[[nodiscard]] auto compute_content_world_bounds(
    const erhe::gltf::Gltf_data& gltf_data,
    const bool                   exclude_unlit
) -> erhe::math::Aabb
{
    erhe::math::Aabb bounds{};
    for (const std::shared_ptr<erhe::scene::Node>& node : gltf_data.nodes) {
        if (!node) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
        if (!mesh) {
            continue;
        }
        const glm::mat4 world_from_node = node->world_from_node();
        for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
            if (!mesh_primitive.primitive) {
                continue;
            }
            const erhe::primitive::Material* material = mesh_primitive.material.get();
            if (
                exclude_unlit &&
                (material != nullptr) &&
                (material->data.bxdf_model == erhe::primitive::Bxdf_model::unlit)
            ) {
                continue;
            }
            const erhe::math::Aabb primitive_bounds = mesh_primitive.primitive->get_bounding_box();
            if (!primitive_bounds.is_valid()) {
                continue;
            }
            bounds.include(primitive_bounds.transformed_by(world_from_node));
        }
    }
    return bounds;
}

// Camera depth range and shadow range derived from the content bounds, so
// that opening a scene (--scene / File > Open Scene / the asset browser)
// shows the whole file instead of clipping it away at the fixed defaults
// (z_far 80, shadow range 22) that only suit a room-sized scene.
class Content_fit
{
public:
    glm::vec3 center       {0.0f};
    float     radius       {0.0f}; // bounding sphere of the framing bounds
    float     view_distance{0.0f}; // camera distance from center that frames it
    float     z_near       {0.03f};
    float     z_far        {80.0f};
    float     shadow_range {22.0f};
};

// Vertical fov the injected default camera is created with; the fit needs it
// to know from how far away the content is viewed.
constexpr float c_default_camera_fov_y = glm::radians(35.0f);

// Distance the default camera needs to keep from the content center for a
// bounding sphere of the given radius to fit the vertical field of view.
[[nodiscard]] auto fit_distance_for_fov_y(const float radius, const float fov_y) -> float
{
    const float tan_half = std::tan(0.5f * fov_y);
    if (!std::isfinite(tan_half) || (tan_half <= 0.0f)) {
        return 2.0f * radius;
    }
    return radius / tan_half;
}

// depth_bounds covers ALL content including unlit backdrops - the far plane
// must reach the sky dome or it clips away. framing_bounds is what the camera
// is placed to look at (and what the shadow range is sized to); it is the
// same box unless unlit primitives are excluded, in which case it is the lit
// content alone. An all-unlit file has no lit content, so framing falls back
// to the depth bounds rather than framing nothing.
[[nodiscard]] auto make_content_fit(
    const erhe::math::Aabb& depth_bounds,
    const erhe::math::Aabb& framing_bounds
) -> std::optional<Content_fit>
{
    if (!depth_bounds.is_valid()) {
        return {};
    }
    const float depth_radius = 0.5f * glm::length(depth_bounds.diagonal());
    if (!std::isfinite(depth_radius) || (depth_radius <= 0.0f)) {
        return {};
    }
    const bool  use_framing_bounds = framing_bounds.is_valid() && (glm::length(framing_bounds.diagonal()) > 0.0f);
    const erhe::math::Aabb& fit_bounds = use_framing_bounds ? framing_bounds : depth_bounds;
    const float radius = use_framing_bounds ? (0.5f * glm::length(framing_bounds.diagonal())) : depth_radius;
    if (!std::isfinite(radius) || (radius <= 0.0f)) {
        return {};
    }

    Content_fit fit;
    fit.center        = fit_bounds.center();
    fit.radius        = radius;
    fit.view_distance = fit_distance_for_fov_y(radius, c_default_camera_fov_y);
    // Far enough to see the content from well outside it (the viewer can
    // back off / orbit without the far plane eating the geometry). Never
    // below the room-sized defaults: a small model still wants room to zoom
    // out into, so the fit only ever widens what the import would have set.
    // Sized from the full content (unlit backdrops included) plus the framing
    // distance, so the camera sees the backdrop from where it is placed.
    const float far_extent = std::max(depth_radius, glm::length(fit.center - depth_bounds.center()) + depth_radius);
    fit.z_far        = std::clamp(8.0f * far_extent, 80.0f, 100000.0f);
    // Near plane grows (mildly) with the content so the depth range stays
    // usable without reverse depth, capped at 10 cm so inspecting detail in
    // a large scene does not clip.
    fit.z_near       = std::clamp(0.0005f * depth_radius, 0.01f, 0.1f);
    // Shadow range is a radius around the VIEW CAMERA, not around the
    // content: from where the camera is placed the far side of the content is
    // view_distance + radius away, and anything past the range gets no shadow
    // at all. Sized to the framing (lit) content - shadows are cast by the lit
    // scene, not by the backdrop - and kept inside the [1, 1000] range the
    // Properties UI slider covers.
    fit.shadow_range = std::clamp(fit.view_distance + radius, 22.0f, 1000.0f);
    return fit;
}

}

auto make_import_build_info(
    App_context&                              context,
    const erhe::scene_renderer::Mesh_memory_queue queue
) -> erhe::primitive::Build_info
{
    return erhe::primitive::Build_info{
        .primitive_types = {
            .fill_triangles          = true,
            .fill_triangles_expanded = true,
            .edge_lines              = true,
            .corner_points           = true,
            .centroid_points         = true
        },
        .buffer_info = context.mesh_memory->make_primitive_buffer_info(queue)
    };
}

// Worker-side half of finalize_imported_meshes (async-asset-loading plan
// phase 3a): builds every imported primitive's Buffer_mesh. Everything else
// finalize_imported_meshes does - the raytrace proxy, update_rt_primitives,
// collecting the mesh node items - mutates scene-side state and stays on the
// main thread. make_renderable_mesh is idempotent (it returns true when the
// shape already has buffer-mesh triangles), so the later main-thread pass
// simply fast-paths over what this built.
//
// Deliberately SERIAL over the primitives rather than a per-mesh fan-out:
// the parse clones a mesh per instantiating node and the clones share
// Primitive objects, and make_buffer_mesh has no per-shape serialization of
// its own, so building two meshes concurrently could build one shared
// primitive twice at once. The point here is to get the work off the main
// thread; parallelising within it needs that serialization first.
//
// build_info / skinned_build_info must be constructed by the CALLER on the
// main thread: make_primitive_buffer_info can create a Vertex_input_state.
void build_imported_buffer_meshes(
    const erhe::primitive::Build_info& build_info,
    const erhe::primitive::Build_info& skinned_build_info,
    const erhe::gltf::Gltf_data&       gltf_data
)
{
    ERHE_PROFILE_FUNCTION();

    const std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
    std::size_t built_count = 0;
    for (const std::shared_ptr<erhe::scene::Node>& node : gltf_data.nodes) {
        if (!node) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
        if (!mesh) {
            continue;
        }
        // Same choice finalize_imported_meshes makes: a skinned mesh needs
        // joint_indices + joint_weights in the GPU vertex buffer, and getting
        // this wrong here would be invisible - the main-thread pass would
        // fast-path over the wrongly-built mesh and skinning would silently
        // not happen.
        const erhe::primitive::Build_info& mesh_build_info = mesh->skin ? skinned_build_info : build_info;
        for (erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_mutable_primitives()) {
            erhe::primitive::Primitive& primitive = *mesh_primitive.primitive.get();
            if (!primitive.make_renderable_mesh(mesh_build_info, erhe::primitive::Normal_style::corner_normals)) {
                log_parsers->error(
                    "async glTF load: failed to build renderable mesh for '{}' (out of GPU mesh memory?)",
                    mesh->get_name()
                );
            }
            ++built_count;
        }
    }
    log_parsers->info(
        "build_imported_buffer_meshes: {} primitives, {} ms",
        built_count,
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
    );
}

void finalize_imported_meshes(
    App_context&                                   context,
    const erhe::primitive::Build_info&             build_info,
    const erhe::gltf::Gltf_data&                   gltf_data,
    std::vector<std::shared_ptr<erhe::Item_base>>* out_mesh_node_items
)
{
    ERHE_PROFILE_FUNCTION();

    const std::chrono::steady_clock::time_point finalize_start_time = std::chrono::steady_clock::now();

    // Load-speedup options (doc/gltf-load-speedup-plan.md). When deferred,
    // the load path builds a fill-only buffer mesh straight from the
    // triangle soup plus an AABB proxy raytrace; the per-mesh tasks of the
    // Async_raytrace_kickoff_operation build the Geometry (edges, smooth
    // normals), the full buffer mesh and the real triangle raytrace after
    // the glTF has finished loading. Without editor settings (or with the
    // options disabled) everything is built here, synchronously.
    const bool defer_edge_lines = (context.editor_settings != nullptr) && context.editor_settings->load.deferred_edge_lines;
    const bool defer_raytrace   = (context.editor_settings != nullptr) && context.editor_settings->load.deferred_raytrace;

    // Build_info variant for skinned meshes -- same as the caller's
    // build_info but with vertex_format_skinned so the GPU vertex buffer
    // carries joint_indices + joint_weights. Without this, Shader_key::derive
    // won't set USE_SKINNING (it checks the vertex_format for joint
    // attributes), and the standard.vert skinning branch is dead code.
    const erhe::primitive::Build_info skinned_build_info{
        .primitive_types = build_info.primitive_types,
        .buffer_info     = context.mesh_memory->make_skinned_primitive_buffer_info(),
        .constant_color  = build_info.constant_color,
        .keep_geometry   = build_info.keep_geometry,
        .normal_style    = build_info.normal_style,
        .vertex_id_vec3  = build_info.vertex_id_vec3,
        .autocolor       = build_info.autocolor
    };

    for (const std::shared_ptr<erhe::scene::Node>& node : gltf_data.nodes) {
        if (!node) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
        if (!mesh) {
            continue;
        }
        if (out_mesh_node_items != nullptr) {
            out_mesh_node_items->push_back(node);
        }

        // Skinned meshes need joint_indices + joint_weights in the GPU
        // vertex buffer so the standard.vert skinning branch can read
        // them via a_joint_indices_0 / a_joint_weights_0. Pick the
        // build_info accordingly; the rest of the build flow is
        // identical.
        const erhe::primitive::Build_info& mesh_build_info = mesh->skin ? skinned_build_info : build_info;
        std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_mutable_primitives();
        for (erhe::scene::Mesh_primitive& mesh_primitive : mesh_primitives) {
            erhe::primitive::Primitive& primitive = *mesh_primitive.primitive.get();
            // glTF arrives with facets + vertices but no edges. Build edges
            // (and the smooth vertex normals used for wireframe bias) so
            // the content wide-line renderer has something to draw.
            // Geometry restored from the ERHE_geometry extension already
            // carries edges (and every other attribute) byte-exact from the
            // dump - reprocessing would overwrite it and break the
            // bit-exact round-trip, so process only when edges are
            // genuinely missing.
            if (primitive.render_shape) {
                // Deferred edge lines: leave the (expensive) triangle-soup ->
                // Geometry conversion to the per-mesh background task; only a
                // Geometry that already exists (ERHE_geometry restore) but
                // genuinely lacks edges is processed here. The eager path
                // uses get_geometry(), which lazily converts the soup.
                const std::shared_ptr<erhe::geometry::Geometry>& geometry = defer_edge_lines
                    ? primitive.render_shape->get_geometry_const()
                    : primitive.render_shape->get_geometry();
                if (geometry && (geometry->get_mesh().edges.nb() == 0)) {
                    geometry->process({.flags =
                        erhe::geometry::Geometry::process_flag_connect                       |
                        erhe::geometry::Geometry::process_flag_build_edges                   |
                        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals
                    });
                }
            }
            const bool renderable_ok = primitive.make_renderable_mesh(mesh_build_info, erhe::primitive::Normal_style::corner_normals);
            if (!renderable_ok) {
                log_parsers->error(
                    "glTF import: failed to build renderable mesh for '{}' (out of GPU mesh memory?) - primitive skipped",
                    mesh->get_name()
                );
            }
            if (defer_raytrace) {
                // Proxy over the renderable-mesh bounds so picking works the
                // moment the scene appears; replaced by the real triangle
                // raytrace when the deferred task commits.
                static_cast<void>(primitive.make_raytrace_proxy());
            } else {
                if (!primitive.make_raytrace()) {
                    log_parsers->warn("glTF import: failed to build raytrace for '{}'", mesh->get_name());
                }
            }
        }

        mesh->update_rt_primitives();
    }

    const std::chrono::steady_clock::duration finalize_duration = std::chrono::steady_clock::now() - finalize_start_time;
    log_parsers->info(
        "finalize_imported_meshes: {} ms (deferred_edge_lines = {}, deferred_raytrace = {})",
        std::chrono::duration_cast<std::chrono::milliseconds>(finalize_duration).count(),
        defer_edge_lines,
        defer_raytrace
    );
}

auto make_import_gltf_operation(
    App_context&                       context,
    erhe::primitive::Build_info        build_info,
    const std::shared_ptr<Scene_root>& scene_root,
    const std::filesystem::path&       path,
    const bool                         materials_as_references,
    const bool                         fit_view_to_content,
    Prepared_gltf_parse* const         prepared_parse
) -> std::shared_ptr<Operation>
{
    ERHE_VERIFY(scene_root);
    erhe::graphics::Device& graphics_device = *context.graphics_device;
    tf::Executor&           executor        = *context.executor;

    // R5.7 record adoption (plan resolution 3): when registering scene_root
    // adopted a loaded container record for this path (the scene-open flow:
    // Scene_open_operation registers before building this import), reuse the
    // record's parse instead of parsing the file again - one parse, one set
    // of asset objects, both directions.
    std::shared_ptr<erhe::scene::Node> root_node;
    erhe::gltf::Gltf_data              gltf_data;
    std::optional<Adopted_container_parse> adopted_parse;
    // An asynchronously prepared parse wins over everything: the caller
    // already checked adoptability before queueing the load, and re-checking
    // here would take a record whose parse we would then throw away.
    if ((prepared_parse == nullptr) && (context.asset_manager != nullptr)) {
        adopted_parse = context.asset_manager->take_adopted_parse(*scene_root, path);
    }
    if (prepared_parse != nullptr) {
        gltf_data = std::move(prepared_parse->gltf_data);
        root_node = prepared_parse->root_node;
        log_parsers->info(
            "import '{}': consuming asynchronously prepared parse",
            erhe::file::to_string(path.filename())
        );
    } else if (adopted_parse.has_value()) {
        gltf_data = std::move(adopted_parse->gltf_data);
        root_node = adopted_parse->root_node;
        // The container's free root node becomes the import_root wrapper:
        // implicit container, not file content; glTF export writes its
        // children in its place so open/save cycles do not nest wrappers.
        root_node->set_name(erhe::file::to_string(path.filename()));
        root_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui | erhe::Item_flags::import_root);
        // Container parses use mesh layer 0 (their node trees are never
        // rendered); scene content draws the content layer. Walk the node
        // attachments, not gltf_data.meshes: the parse clones the template
        // mesh per instantiating node, and the clones are what enter the
        // scene.
        const erhe::scene::Layer_id content_layer_id = scene_root->layers().content()->id;
        for (const std::shared_ptr<erhe::scene::Node>& node : gltf_data.nodes) {
            if (!node) {
                continue;
            }
            const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
            if (mesh) {
                mesh->layer_id = content_layer_id;
            }
        }
    } else {
        erhe::scene::Scene temp_scene{"temp scene", nullptr};
        const std::shared_ptr<erhe::scene::Node> temp_scene_root_node = temp_scene.get_root_node();
        root_node = std::make_shared<erhe::scene::Node>(erhe::file::to_string(path.filename()));
        // import_root: implicit container, not file content; glTF export writes
        // its children in its place so open/save cycles do not nest wrappers.
        root_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui | erhe::Item_flags::import_root);
        root_node->set_parent(temp_scene_root_node);

        erhe::gltf::Image_transfer image_transfer{graphics_device};
        erhe::gltf::Gltf_parse_arguments parse_arguments{
            .executor        = executor,
            .device_options  = erhe::gltf::query_gltf_device_options(graphics_device),
            .root_node       = root_node,
            .mesh_layer_id   = scene_root->layers().content()->id,
            .path            = path,
            .parallel        = (context.editor_settings == nullptr) || context.editor_settings->load.parallel_gltf_parse,
            .fix_spot_lights = context.fix_gltf_spot_lights,
        };
        const std::chrono::steady_clock::time_point parse_start_time = std::chrono::steady_clock::now();
        gltf_data = erhe::gltf::parse_gltf(parse_arguments);
        // parse_gltf creates no GPU objects (async-asset-loading plan step 3):
        // it leaves decoded pixels in Gltf_data::image_residency. Drain it in
        // full here, so this path behaves exactly as it did before the split.
        // Step 6 replaces this blocking drain with a budgeted one.
        gltf_data.image_residency.drain(gltf_data, graphics_device, image_transfer);
        const std::chrono::steady_clock::duration parse_duration = std::chrono::steady_clock::now() - parse_start_time;
        log_parsers->info(
            "parse_gltf '{}': {} ms",
            erhe::file::to_string(path.filename()),
            std::chrono::duration_cast<std::chrono::milliseconds>(parse_duration).count()
        );

        // Detach root_node from temp_scene before it goes out of scope. The
        // Item_insert_remove_operation sub-op below will attach root_node to
        // scene_root_node on execute().
        root_node->set_parent({});
    }

    // ERHE_asset_reference proxies (R6): substitute manager-acquired
    // objects for reference stubs before anything consumes the materials;
    // the returned keys make the library attach below list them as
    // reference entries. Import-as-reference (R7) additionally routes the
    // remaining parsed materials through the manager.
    std::unordered_map<const erhe::primitive::Material*, Asset_key> material_reference_keys =
        resolve_material_asset_references(context, gltf_data);
    if (materials_as_references) {
        acquire_import_materials_as_references(context, gltf_data, path, material_reference_keys);
    }

    // Color-graph computation (currently unused but preserved as in the
    // previous implementation; the wireframe-color application is commented
    // out below).
    std::vector<glm::vec4> colors;
    colors.emplace_back(0.0f, 1.0f, 1.0f, 1.0f);
    colors.emplace_back(0.0f, 1.0f, 0.0f, 1.0f);
    colors.emplace_back(1.0f, 1.0f, 0.0f, 1.0f);
    colors.emplace_back(1.0f, 0.0f, 0.0f, 1.0f);
    colors.emplace_back(1.0f, 0.0f, 1.0f, 1.0f);
    std::unordered_set<int> available_colors;
    for (int i = 0; i < static_cast<int>(colors.size()); ++i) {
        available_colors.insert(i);
    }
    std::unordered_map<erhe::scene::Node*, int> node_colors;

    bool add_default_camera = true;
    bool add_default_light  = true;

    log_parsers->info("Processing {} nodes", gltf_data.nodes.size());

    size_t mesh_count = 0;
    size_t primitive_count = 0;
    for (const auto& node : gltf_data.nodes) {
        if (!node) {
            continue;
        }
        std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
        if (mesh) {
            ++mesh_count;
            std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_mutable_primitives();
            primitive_count += mesh_primitives.size();
        }
    }
    log_parsers->info("Processing {} nodes, {} meshes, {} primitives", gltf_data.nodes.size(), mesh_count, primitive_count);

    std::vector<std::shared_ptr<erhe::Item_base>> mesh_node_items;
    finalize_imported_meshes(context, build_info, gltf_data, &mesh_node_items);

    // glTF 2.1 external assets: instantiate each referenced asset under its
    // carrier node (recursively resolved and cached by Prefab_library). The
    // scene's content library receives the template resources as reference
    // entries, like the interactive instantiate_prefab.
    if (context.prefab_library != nullptr) {
        resolve_external_assets(*context.prefab_library, gltf_data, scene_root->layers().content()->id, &mesh_node_items, scene_root->get_content_library().get());
    }

    // Content extent of the file, used below to size the view (camera depth
    // range and shadow range) to the scene instead of leaving the fixed
    // room-sized defaults. Computed after finalize_imported_meshes(), which
    // is what gives the primitives their bounding boxes.
    // Unlit (KHR_materials_unlit) primitives - sky domes, backdrops - still
    // set the far plane (they must stay visible) but are left out of the
    // framing so the camera frames the lit scene, not the horizon.
    const bool exclude_unlit = (context.editor_settings != nullptr) && context.editor_settings->exclude_unlit_primitives;
    const std::optional<Content_fit> content_fit = fit_view_to_content
        ? make_content_fit(
            compute_content_world_bounds(gltf_data, false),
            exclude_unlit ? compute_content_world_bounds(gltf_data, true) : erhe::math::Aabb{}
        )
        : std::optional<Content_fit>{};
    if (fit_view_to_content) {
        if (content_fit.has_value()) {
            log_parsers->info(
                "Scene content fit: center {} {} {} radius {:.3f} view_distance {:.1f} -> z_near {:.4f} z_far {:.1f} shadow_range {:.1f}",
                content_fit->center.x, content_fit->center.y, content_fit->center.z, content_fit->radius,
                content_fit->view_distance, content_fit->z_near, content_fit->z_far, content_fit->shadow_range
            );
        } else {
            log_parsers->info("Scene content fit: no valid content bounds - keeping default camera / shadow ranges");
        }
    }

    std::vector<std::shared_ptr<erhe::scene::Camera>> imported_cameras;
    for (const std::shared_ptr<erhe::scene::Node>& node : gltf_data.nodes) {
        if (!node) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Camera> camera = erhe::scene::get_attachment<erhe::scene::Camera>(node.get());
        if (camera) {
            add_default_camera = false;
            imported_cameras.push_back(camera);
        }

        const std::shared_ptr<erhe::scene::Light> light = erhe::scene::get_attachment<erhe::scene::Light>(node.get());
        if (light) {
            add_default_light = false;
        }

        if (node->get_parent_node() == root_node) {
            color_graph(node.get(), node_colors, available_colors);
        }
    }

    erhe::scene::Scene* scene = scene_root->get_hosted_scene();
    const std::shared_ptr<erhe::scene::Node> scene_root_node = scene->get_root_node();

    if (!scene->get_cameras().empty()) {
        add_default_camera = false;
    }
    for (const auto& layer : scene->get_light_layers()) {
        if (!layer->lights.empty()) {
            add_default_light = false;
            break;
        }
    }

    // Build default camera and light nodes but do NOT parent them; the
    // Item_insert_remove_operation sub-ops below will parent them to
    // scene_root_node on execute() and detach on undo().
    std::shared_ptr<erhe::scene::Node> default_camera_node;
    std::shared_ptr<erhe::scene::Node> default_key_light_node;
    std::shared_ptr<erhe::scene::Node> default_fill_light_node;

    // The implicit defaults are editor conveniences, not authored content:
    // exclude_from_prefab keeps them out of prefab instances (the flag
    // persists in node extras and instantiation filters flagged items).
    if (add_default_camera) {
        default_camera_node = std::make_shared<erhe::scene::Node>("Camera");
        std::shared_ptr<erhe::scene::Camera> default_camera = std::make_shared<erhe::scene::Camera>("Camera");
        default_camera->projection()->fov_y           = c_default_camera_fov_y;
        default_camera->projection()->projection_type = erhe::scene::Projection::Type::perspective_vertical;
        default_camera->projection()->z_near          = 0.03f;
        default_camera->projection()->z_far           = 80.0f;
        default_camera->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui | erhe::Item_flags::exclude_from_prefab);
        default_camera_node->attach(default_camera);

        // Default framing: 8 units back from the origin, looking at it. When
        // the view is fitted to the content (scene open), back off far enough
        // for the content's bounding sphere to fill the vertical fov instead,
        // and take the fitted depth / shadow ranges - a 200 m scene is not
        // visible at all through a 80 m far plane.
        glm::vec3 eye_position{0.0f, 0.0f, 8.0f};
        glm::vec3 target_position{0.0f, 0.0f, 0.0f};
        if (content_fit.has_value()) {
            default_camera->projection()->z_near = content_fit->z_near;
            default_camera->projection()->z_far  = content_fit->z_far;
            default_camera->set_shadow_range(content_fit->shadow_range);
            target_position = content_fit->center;
            eye_position    = content_fit->center + glm::vec3{0.0f, 0.0f, 1.0f} * content_fit->view_distance;
        }
        const glm::mat4 m = erhe::math::create_look_at(
            eye_position,                 // eye
            target_position,              // center
            glm::vec3{0.0f, 1.00f, 0.0f}  // up
        );
        default_camera_node->set_parent_from_node(m);
        default_camera_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui | erhe::Item_flags::exclude_from_prefab);
    }

    // Cameras the file itself carries: their fov / near plane are authored
    // content and stay as they are, but shadow range is not a glTF concept
    // (it defaults to a room-sized 22) and an authored far plane is often
    // too near to show the whole scene. Only ever widened, never narrowed,
    // so a deliberately larger authored value survives.
    if (content_fit.has_value()) {
        for (const std::shared_ptr<erhe::scene::Camera>& camera : imported_cameras) {
            erhe::scene::Projection* projection = camera->projection();
            if (projection != nullptr) {
                projection->z_far = std::max(projection->z_far, content_fit->z_far);
            }
            camera->set_shadow_range(std::max(camera->get_shadow_range(), content_fit->shadow_range));
        }
    }

    if (add_default_light) {
        default_key_light_node = std::make_shared<erhe::scene::Node>("Key Light");
        std::shared_ptr<erhe::scene::Light> key_light = std::make_shared<erhe::scene::Light>("Key Light");
        key_light->type      = erhe::scene::Light::Type::directional;
        key_light->color     = glm::vec3{1.0f, 1.0f, 1.0};
        key_light->intensity = 1.0f;
        key_light->range     = 0.0f;
        key_light->layer_id  = scene_root->layers().light()->id;
        key_light->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui | erhe::Item_flags::exclude_from_prefab);
        default_key_light_node->attach          (key_light);
        default_key_light_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui | erhe::Item_flags::exclude_from_prefab);
        const glm::quat key_quat{0.8535534f, -0.3535534f, -0.353553385f, -0.146446586f};
        default_key_light_node->set_parent_from_node(glm::mat4{key_quat});

        default_fill_light_node = std::make_shared<erhe::scene::Node>("Fill Light Node");
        std::shared_ptr<erhe::scene::Light> fill_light = std::make_shared<erhe::scene::Light>("Fill Light");
        fill_light->type      = erhe::scene::Light::Type::directional;
        fill_light->color     = glm::vec3{1.0f, 1.0f, 1.0};
        fill_light->intensity = 0.5f;
        fill_light->range     = 0.0f;
        fill_light->layer_id  = scene_root->layers().light()->id;
        fill_light->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui | erhe::Item_flags::exclude_from_prefab);
        default_fill_light_node->attach          (fill_light);
        default_fill_light_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui | erhe::Item_flags::exclude_from_prefab);
        const glm::quat fill_quat{-0.353553444f, -0.8535534f, 0.146446645f, -0.353553325f};
        default_fill_light_node->set_parent_from_node(glm::mat4{fill_quat});
    }

    std::vector<std::shared_ptr<Operation>> operations;
    append_content_library_attach_operations(scene_root->get_content_library(), gltf_data, path.generic_string(), material_reference_keys, operations);

    // KHR_physics_rigid_bodies / KHR_implicit_shapes: shared physics items go
    // through content-library attach operations; Node_physics / Node_joint
    // attachments are attached directly to the imported nodes (like meshes)
    // and enter the scene with the insert operation below. Must run after
    // mesh finalization above (mesh-sourced collision shapes need Geometry).
    import_gltf_physics(context, gltf_data, scene_root, path, operations);

    // Editor-domain ERHE_* extensions (doc/gltf-scene-roundtrip-plan.md
    // phase 3): ERHE_layout / ERHE_collections onto the imported nodes,
    // ERHE_brushes / ERHE_node_graphs into the content library. ERHE_scene
    // is deliberately NOT applied here - importing an asset must not
    // clobber the target scene's settings (the phase-4 Open-Scene path
    // consumes it).
    import_gltf_editor_state(context, gltf_data, scene_root, path, operations);

    operations.push_back(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = context,
                .item    = root_node,
                .parent  = scene_root_node,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );

    if (default_camera_node) {
        operations.push_back(
            std::make_shared<Item_insert_remove_operation>(
                Item_insert_remove_operation::Parameters{
                    .context = context,
                    .item    = default_camera_node,
                    .parent  = scene_root_node,
                    .mode    = Item_insert_remove_operation::Mode::insert
                }
            )
        );
    }

    if (default_key_light_node) {
        operations.push_back(
            std::make_shared<Item_insert_remove_operation>(
                Item_insert_remove_operation::Parameters{
                    .context = context,
                    .item    = default_key_light_node,
                    .parent  = scene_root_node,
                    .mode    = Item_insert_remove_operation::Mode::insert
                }
            )
        );
    }

    if (default_fill_light_node) {
        operations.push_back(
            std::make_shared<Item_insert_remove_operation>(
                Item_insert_remove_operation::Parameters{
                    .context = context,
                    .item    = default_fill_light_node,
                    .parent  = scene_root_node,
                    .mode    = Item_insert_remove_operation::Mode::insert
                }
            )
        );
    }

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
        fmt::format("[{}] Import glTF {}", compound->get_serial(), erhe::file::to_string(path.filename()))
    );
    return compound;
}

void import_gltf(
    App_context&                       context,
    erhe::primitive::Build_info        build_info,
    const std::shared_ptr<Scene_root>& scene_root,
    const std::filesystem::path&       path,
    const bool                         materials_as_references
)
{
    // Asynchronous path (doc/async-asset-loading-plan.md step 7): the parse,
    // the Buffer_mesh build and texture residency happen off the tick; the
    // undoable operation is then built from the finished parse and stays the
    // cheap synchronous thing plan 2.8 wants it to be.
    if (context.asset_manager != nullptr) {
        Asset_load_request request{
            .path                    = path,
            .import_target           = scene_root,
            .materials_as_references = materials_as_references
        };
        const std::weak_ptr<Scene_root> weak_scene_root = scene_root;
        std::shared_ptr<Asset_load_handle> handle = context.asset_manager->queue_load(
            std::move(request),
            [&context, weak_scene_root, path, materials_as_references](const Asset_load_result& result) {
                const std::shared_ptr<Scene_root> target = weak_scene_root.lock();
                if (!target) {
                    return; // scene closed while loading; the task cancelled itself
                }
                context.operation_stack->queue(
                    make_import_gltf_operation(
                        context,
                        make_import_build_info(context),
                        target,
                        path,
                        materials_as_references,
                        false,
                        result.prepared_parse.get()
                    )
                );
            }
        );
        if (handle) {
            return;
        }
    }
    context.operation_stack->queue(
        make_import_gltf_operation(context, std::move(build_info), scene_root, path, materials_as_references)
    );
}

auto scan_gltf(const std::filesystem::path& path) -> Gltf_scan_summary
{
    Gltf_scan_summary summary;
    std::vector<std::string>& out = summary.contents;
    erhe::gltf::Gltf_scan scan = erhe::gltf::scan_gltf(path);
    summary.bounding_box = scan.bounding_box;

    if (!scan.errors.empty()) {
        out.push_back("Errors:");
        for (const std::string& error : scan.errors) {
            out.push_back(" - " + error);
        }
    }

    if (!scan.scenes            .empty()) out.push_back(fmt::format("{} scenes",             scan.scenes            .size()));
    if (!scan.meshes            .empty()) out.push_back(fmt::format("{} meshes",             scan.meshes            .size()));
    if (!scan.animations        .empty()) out.push_back(fmt::format("{} animations",         scan.animations        .size()));
    if (!scan.skins             .empty()) out.push_back(fmt::format("{} skins",              scan.skins             .size()));
    if (!scan.materials         .empty()) out.push_back(fmt::format("{} materials",          scan.materials         .size()));
    if (!scan.nodes             .empty()) out.push_back(fmt::format("{} nodes",              scan.nodes             .size()));
    if (!scan.cameras           .empty()) out.push_back(fmt::format("{} cameras",            scan.cameras           .size()));
    if (!scan.directional_lights.empty()) out.push_back(fmt::format("{} directional lights", scan.directional_lights.size()));
    if (!scan.point_lights      .empty()) out.push_back(fmt::format("{} point lights",       scan.point_lights      .size()));
    if (!scan.spot_lights       .empty()) out.push_back(fmt::format("{} spot lights",        scan.spot_lights       .size()));
    if (!scan.images            .empty()) out.push_back(fmt::format("{} images",             scan.images            .size()));
    if (!scan.samplers          .empty()) out.push_back(fmt::format("{} samplers",           scan.samplers          .size()));
    if (!scan.files             .empty()) out.push_back(fmt::format("{} files",              scan.files             .size()));
    if (!scan.external_assets.empty()) {
        out.push_back(fmt::format("{} external assets:", scan.external_assets.size()));
        for (const std::string& external_asset : scan.external_assets) {
            out.push_back(" - " + external_asset);
        }
    }
    if (!scan.extensions_used.empty()) {
        out.push_back("Extensions used:");
        for (const std::string& extension : scan.extensions_used) {
            out.push_back(" - " + extension);
        }
    }
    if (!scan.extensions_required.empty()) {
        out.push_back("Extensions required:");
        for (const std::string& extension : scan.extensions_required) {
            out.push_back(" - " + extension);
        }
    }
    if (scan.bounding_box.has_value() && scan.bounding_box->is_valid()) {
        const glm::vec3 size = scan.bounding_box->diagonal();
        out.push_back(fmt::format("size: {:.2f} x {:.2f} x {:.2f}", size.x, size.y, size.z));
    }
    summary.extensions_used = std::move(scan.extensions_used);
    summary.material_names  = std::move(scan.materials);
    summary.material_uids   = std::move(scan.material_uids);
    return summary;
}

auto is_erhe_scene(const std::vector<std::string>& extensions_used) -> bool
{
    return std::find(extensions_used.begin(), extensions_used.end(), "ERHE_scene") != extensions_used.end();
}

auto make_gltf_image_source_provider(const std::shared_ptr<Content_library>& content_library)
    -> std::function<std::shared_ptr<const erhe::gltf::Gltf_image_source>(const erhe::graphics::Texture*)>
{
    // Snapshot the retained sources under the library mutex; the returned
    // provider then runs lock-free inside export_gltf().
    using Source_map = std::unordered_map<const erhe::graphics::Texture*, std::shared_ptr<const erhe::gltf::Gltf_image_source>>;
    std::shared_ptr<Source_map> sources = std::make_shared<Source_map>();
    if (content_library && content_library->textures) {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{content_library->mutex};
        content_library->textures->for_each_const<Content_library_node>(
            [&sources](const Content_library_node& node) -> bool {
                const std::shared_ptr<erhe::graphics::Texture> texture = std::dynamic_pointer_cast<erhe::graphics::Texture>(node.item);
                if (texture && node.image_source) {
                    (*sources)[texture.get()] = node.image_source;
                }
                return true;
            }
        );
    }
    return [sources](const erhe::graphics::Texture* texture) -> std::shared_ptr<const erhe::gltf::Gltf_image_source> {
        if (texture == nullptr) {
            return {};
        }
        const auto it = sources->find(texture);
        if (it != sources->end()) {
            return it->second;
        }
        // Fallback for textures imported before retention landed: re-read a
        // standalone source image file. Images embedded in a .glb/.gltf
        // cannot be re-extracted here; the exporter warns and skips the slot.
        const std::filesystem::path* source_path = texture->get_source_path();
        if ((source_path != nullptr) && !source_path->empty()) {
            const std::string extension = source_path->extension().generic_string();
            if ((extension != ".glb") && (extension != ".gltf")) {
                const std::optional<std::string> file_content = erhe::file::read("gltf export image source fallback", *source_path);
                if (file_content.has_value() && !file_content->empty()) {
                    std::shared_ptr<erhe::gltf::Gltf_image_source> image_source = std::make_shared<erhe::gltf::Gltf_image_source>();
                    const std::byte* start = reinterpret_cast<const std::byte*>(file_content->data());
                    image_source->encoded_bytes.assign(start, start + file_content->size());
                    image_source->mime_type = erhe::gltf::sniff_image_mime_type(image_source->encoded_bytes);
                    (*sources)[texture] = image_source;
                    return image_source;
                }
            }
        }
        return {};
    };
}

auto collect_gltf_export_animations(const std::shared_ptr<Content_library>& content_library)
    -> std::vector<std::shared_ptr<erhe::scene::Animation>>
{
    if (!content_library || !content_library->animations) {
        return {};
    }
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{content_library->mutex};
    return content_library->animations->get_all<erhe::scene::Animation>();
}

auto save_scene_gltf(Scene_root& scene_root, const std::filesystem::path& path) -> bool
{
    const erhe::scene::Scene& scene = scene_root.get_scene();
    const std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
    if (!root_node) {
        log_parsers->error("save_scene_gltf: scene '{}' has no root node", scene_root.get_name());
        return false;
    }
    const erhe::gltf::Gltf_physics_data physics_data = build_gltf_physics_data(scene, scene_root.get_content_library().get());
    erhe::gltf::Gltf_export_arguments export_arguments{
        .root_node             = *root_node,
        .binary                = path.extension() != std::filesystem::path{".gltf"},
        .physics_data          = &physics_data,
        .external_assets       = collect_prefab_external_assets(*root_node, path.parent_path()),
        .image_source_provider = make_gltf_image_source_provider(scene_root.get_content_library()),
        .animations            = collect_gltf_export_animations(scene_root.get_content_library())
    };
    // Editor-domain ERHE_* extensions + baked graph-mesh exclusion: this is
    // what makes the file a full scene save instead of an interchange export
    // (ERHE_scene in extensionsUsed is the erhe-authored marker).
    add_gltf_editor_state(export_arguments, scene_root, path);
    const std::string gltf = erhe::gltf::export_gltf(export_arguments);
    if (!erhe::file::write_file(path, gltf)) {
        log_parsers->error("save_scene_gltf: failed to write '{}'", erhe::file::to_string(path));
        return false;
    }
    return true;
}

auto save_scene_gltf(App_context& context, Scene_root& scene_root, const std::filesystem::path& path) -> bool
{
    if (!save_scene_gltf(scene_root, path)) {
        return false;
    }
    // R5.8: a successful save clears the scene's container record dirty flag
    // (the file now matches the live asset state).
    if (context.asset_manager != nullptr) {
        context.asset_manager->on_scene_saved(scene_root);
    }
    // Rescan the asset browser so the freshly saved scene appears without a
    // manual Scan (#256).
    context.app_message_bus->scene_saved.send_message(Scene_saved_message{.path = path});
    // Saving over a loaded prefab source refreshes every instance in every
    // scene (this side effect used to be the separate Save Prefab command).
    if (context.prefab_library != nullptr) {
        std::error_code error_code;
        std::filesystem::path canonical_path = std::filesystem::weakly_canonical(path, error_code);
        if (error_code) {
            canonical_path = path;
        }
        if (context.prefab_library->get_prefabs().contains(canonical_path)) {
            context.prefab_library->reload(canonical_path);
        }
    }
    return true;
}

auto default_scene_dir() -> std::filesystem::path
{
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::absolute(std::filesystem::path{"res"} / "editor" / "scenes", ec);
    if (ec) {
        dir = std::filesystem::path{"res"} / "editor" / "scenes";
    }
    static_cast<void>(erhe::file::ensure_directory_exists(dir));
    return dir;
}

auto resolve_scene_save_path(const Scene_root& scene_root) -> std::filesystem::path
{
    const std::filesystem::path& source_path = scene_root.get_source_path();
    if (!source_path.empty()) {
        return source_path;
    }
    return default_scene_dir() / (scene_root.get_name() + ".glb");
}

auto finish_open_scene_gltf(
    App_context&                              context,
    const std::filesystem::path&              path,
    erhe::gltf::Gltf_data&                    gltf_data,
    const std::shared_ptr<erhe::scene::Node>& container_node,
    const bool                                adopted
) -> std::shared_ptr<Scene_root>
{
    ERHE_VERIFY(context.current_command_buffer != nullptr);
    const std::optional<Gltf_scene_state> scene_state = parse_gltf_scene_state(gltf_data);
    if (!scene_state.has_value()) {
        // The callers route only ERHE_scene-marked files here, so a missing
        // payload means the file could not be parsed at all.
        log_parsers->error("open_scene_gltf: '{}' has no ERHE_scene payload - not an erhe-authored scene or parse failed", erhe::file::to_string(path));
        return {};
    }

    // Fresh, EMPTY content library: the file carries the scene's own brushes /
    // materials / textures (a create_scene-style library pre-populated with
    // the standard brushes would duplicate the saved ones).
    std::shared_ptr<Content_library> content_library = std::make_shared<Content_library>();
    const Draw_list_scene_dependencies draw_list_dependencies = make_draw_list_scene_dependencies(context);
    std::shared_ptr<Scene_root> scene_root = std::make_shared<Scene_root>(
        context.app_message_bus,
        content_library,
        erhe::file::to_string(path.stem()),
        scene_state->enable_physics,
        &draw_list_dependencies
    );
    // Remember where the scene came from (same as Scene_open_operation does
    // for foreign glTF): Save Scene writes back here, without confirmation.
    {
        std::error_code error_code;
        const std::filesystem::path canonical_path = std::filesystem::weakly_canonical(path, error_code);
        scene_root->set_source_path(error_code ? path : canonical_path);
    }

    // Apply the ERHE_scene payload - the one thing the import path
    // deliberately leaves alone (importing an asset must not clobber the
    // target scene's settings).
    erhe::scene::Scene& scene = scene_root->get_scene();
    scene.ambient_light = scene_state->ambient_light;
    if (!scene_state->settings_json.empty()) {
        simdjson::ondemand::parser settings_parser;
        simdjson::padded_string    settings_padded{scene_state->settings_json};
        simdjson::ondemand::document settings_document;
        simdjson::ondemand::object   settings_object;
        if (
            (settings_parser.iterate(settings_padded).get(settings_document) == simdjson::SUCCESS) &&
            (settings_document.get_object().get(settings_object) == simdjson::SUCCESS) &&
            (deserialize(settings_object, scene_root->get_scene_settings()) == simdjson::SUCCESS)
        ) {
            log_parsers->info("open_scene_gltf: applied per-scene setting overrides");
        } else {
            log_parsers->error("open_scene_gltf: failed to deserialize ERHE_scene settings payload");
        }
    }

    scene_root->register_to_editor_scenes(*context.app_scenes);

    // ERHE_asset_reference proxies (R6): substitute manager-acquired
    // objects for reference stubs. After registration, so a self-referencing
    // key (prohibited, hand-crafted) resolves against this scene's own
    // still-empty record and falls back to the stub instead of re-parsing
    // the file being opened.
    const std::unordered_map<const erhe::primitive::Material*, Asset_key> material_reference_keys =
        resolve_material_asset_references(context, gltf_data);

    std::vector<std::shared_ptr<erhe::Item_base>> mesh_node_items;
    finalize_imported_meshes(context, make_import_build_info(context), gltf_data, &mesh_node_items);

    // glTF 2.1 external assets: instantiate each referenced prefab under its
    // carrier node. The scene's content library receives the template
    // resources as reference entries, like the interactive
    // instantiate_prefab - without them, register_mesh would mis-adopt the
    // unhosted template materials as scene-owned when the nodes move under
    // the scene root below.
    if (context.prefab_library != nullptr) {
        resolve_external_assets(*context.prefab_library, gltf_data, scene_root->layers().content()->id, &mesh_node_items, content_library.get());
    }

    // Content-library attaches (textures / materials / skins / animations),
    // physics items and editor-domain ERHE_* state. These build undoable
    // operations for the import path; here they are executed inline and
    // dropped - opening a scene is not undoable. Same ordering as the import
    // compound: everything executes before the nodes enter the scene.
    std::vector<std::shared_ptr<Operation>> operations;
    append_content_library_attach_operations(content_library, gltf_data, path.generic_string(), material_reference_keys, operations);
    import_gltf_physics(context, gltf_data, scene_root, path, operations);
    import_gltf_editor_state(context, gltf_data, scene_root, path, operations);
    for (const std::shared_ptr<Operation>& operation : operations) {
        operation->execute(context);
    }

    // Move the parsed top-level nodes directly under the new scene's root:
    // the saved file carries the scene's children in place (import_root
    // wrappers are never written), so open adds no wrapper either. No
    // default camera / lights are injected - an erhe-authored scene carries
    // exactly the cameras and lights it was saved with. Copy the child list:
    // reparenting mutates it.
    const std::shared_ptr<erhe::scene::Node> scene_root_node = scene.get_root_node();
    const std::vector<std::shared_ptr<erhe::Hierarchy>> children = container_node->get_children();
    for (const std::shared_ptr<erhe::Hierarchy>& child : children) {
        child->set_parent(scene_root_node);
    }

    // Raytrace kickoff, mirroring the import compound's final sub-operation.
    Async_raytrace_kickoff_operation raytrace_kickoff{scene_root, std::move(mesh_node_items)};
    raytrace_kickoff.execute(context);

    // Adopted open succeeded: sever the record's structure pins (the parse
    // was consumed in place; the nodes now live in - and must die with -
    // this scene). The record became the scene's record at registration.
    if (adopted && (context.asset_manager != nullptr)) {
        static_cast<void>(context.asset_manager->take_adopted_parse(*scene_root, path));
    }

    log_parsers->info("open_scene_gltf: opened scene '{}' from '{}'", scene_root->get_name(), erhe::file::to_string(path));
    return scene_root;
}

auto open_scene_gltf(
    App_context&                 context,
    const std::filesystem::path& path
) -> std::shared_ptr<Scene_root>
{
    ERHE_VERIFY(context.current_command_buffer != nullptr);

    // R5.7 record adoption (plan resolution 3): a container already loaded
    // for this path lends its parse - read in place, because the ERHE_scene
    // payload (enable_physics) must be known before the Scene_root can be
    // constructed. Registering the scene below adopts the record; the
    // take_adopted_parse call at the end severs the record's structure pins
    // once the open succeeded. Until then the record stays intact, so a
    // failed open leaves the loaded container exactly as it was.
    std::shared_ptr<Asset_container_record> adoptable_record;
    if (context.asset_manager != nullptr) {
        adoptable_record = context.asset_manager->find_adoptable_container(path);
    }

    // Parse into a temporary container when there is nothing to adopt. Mesh
    // layer ids are editor-wide constants (Mesh_layer_id), so parsing before
    // the destination scene exists is safe.
    erhe::scene::Scene temp_scene{"temp scene", nullptr};
    std::shared_ptr<erhe::scene::Node> container_node;
    erhe::gltf::Gltf_data              parsed_gltf_data;
    if (adoptable_record) {
        container_node = adoptable_record->root_node;
        log_parsers->info("open_scene_gltf: adopting loaded container record for '{}'", erhe::file::to_string(path));
    } else {
        const std::shared_ptr<erhe::scene::Node> temp_scene_root_node = temp_scene.get_root_node();
        container_node = std::make_shared<erhe::scene::Node>("open scene container");
        container_node->set_parent(temp_scene_root_node);

        erhe::gltf::Image_transfer image_transfer{*context.graphics_device};
        erhe::gltf::Gltf_parse_arguments parse_arguments{
            .executor        = *context.executor,
            .device_options  = erhe::gltf::query_gltf_device_options(*context.graphics_device),
            .root_node       = container_node,
            .mesh_layer_id   = Mesh_layer_id::content,
            .path            = path,
            .parallel        = (context.editor_settings == nullptr) || context.editor_settings->load.parallel_gltf_parse,
            .fix_spot_lights = context.fix_gltf_spot_lights,
        };
        const std::chrono::steady_clock::time_point parse_start_time = std::chrono::steady_clock::now();
        parsed_gltf_data = erhe::gltf::parse_gltf(parse_arguments);
        // See the note at the other parse_gltf call: residency is a separate
        // step now, drained in full here to keep this path synchronous.
        parsed_gltf_data.image_residency.drain(parsed_gltf_data, *context.graphics_device, image_transfer);
        const std::chrono::steady_clock::duration parse_duration = std::chrono::steady_clock::now() - parse_start_time;
        log_parsers->info(
            "parse_gltf '{}': {} ms",
            erhe::file::to_string(path.filename()),
            std::chrono::duration_cast<std::chrono::milliseconds>(parse_duration).count()
        );
    }
    // Non-const: the R6 asset-reference resolution below substitutes
    // manager-acquired materials into the parse (for the adopted case that
    // mutates the record's data, which the successful open consumes anyway;
    // a failed open bails out before the resolution runs).
    erhe::gltf::Gltf_data& gltf_data = adoptable_record ? adoptable_record->gltf_data : parsed_gltf_data;

    // Container parses use mesh layer 0 (their node trees are never
    // rendered); scene content draws the content layer. Walk the node
    // attachments, not gltf_data.meshes: the parse clones the template mesh
    // per instantiating node, and the clones are what enter the scene.
    if (adoptable_record) {
        for (const std::shared_ptr<erhe::scene::Node>& node : gltf_data.nodes) {
            if (!node) {
                continue;
            }
            const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
            if (mesh) {
                mesh->layer_id = Mesh_layer_id::content;
            }
        }
    }

    // The main-thread tail, shared with the asynchronous path
    // (Gltf_load_task): everything from here on constructs the Scene_root,
    // builds Buffer_meshes and mutates the scene, so it may only run on the
    // main thread with a live command buffer.
    std::shared_ptr<Scene_root> scene_root = finish_open_scene_gltf(
        context, path, gltf_data, container_node, adoptable_record ? true : false
    );
    return scene_root;
}

}
