#include "parsers/gltf_extensions_export.hpp"

#include "parsers/gltf_extensions_names.hpp"

#include "assets/asset_key.hpp"
#include "assets/asset_manager.hpp"
#include "assets/asset_paths.hpp"
#include "brushes/brush.hpp"
#include "content_library/content_library.hpp"
#include "content_library/style.hpp"
#include "editor_log.hpp"
#include "geometry_graph/geometry_graph_mesh.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "geometry_graph/graph_mesh_serialization.hpp"
#include "prefabs/prefab_instance.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "texture_graph/graph_texture.hpp"
#include "texture_graph/graph_texture_serialization.hpp"

#include "scene/generated/scene_settings_serialization.hpp"

#include "erhe_gltf/gltf_item_flags.hpp"
#include "erhe_gltf/gltf_physics.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_physics/physics_material.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/layout.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace editor {

namespace {

// Shortest-round-trip float for JSON payloads: fmt "{}" prints the shortest
// form that reparses to the same float; routing it through json::parse
// preserves that text through nlohmann's dump (which would otherwise print
// the shortest DOUBLE form of the widened value, e.g. 0.05f ->
// "0.05000000074505806").
[[nodiscard]] auto json_float(const float value) -> nlohmann::json
{
    if (!std::isfinite(value)) {
        return nlohmann::json(0.0);
    }
    return nlohmann::json::parse(fmt::format("{}", value));
}

[[nodiscard]] auto json_vec3(const glm::vec3& value) -> nlohmann::json
{
    return nlohmann::json::array({json_float(value.x), json_float(value.y), json_float(value.z)});
}

[[nodiscard]] auto json_vec4(const glm::vec4& value) -> nlohmann::json
{
    return nlohmann::json::array({json_float(value.x), json_float(value.y), json_float(value.z), json_float(value.w)});
}

[[nodiscard]] auto json_ivec3(const glm::ivec3& value) -> nlohmann::json
{
    return nlohmann::json::array({value.x, value.y, value.z});
}

[[nodiscard]] auto json_float_array(const std::vector<float>& values) -> nlohmann::json
{
    nlohmann::json out = nlohmann::json::array();
    for (const float value : values) {
        out.push_back(json_float(value));
    }
    return out;
}

// Persistent Item flags as a JSON array of names (shared name table with
// the erhe::gltf exporter-internal ERHE_node / ERHE_camera / ERHE_light
// writers).
[[nodiscard]] auto json_flags(const erhe::Item_base& item) -> nlohmann::json
{
    return nlohmann::json::parse(erhe::gltf::persistent_item_flags_to_json(item.get_flag_bits()));
}

// Local property values next to the flags (D23; the erhe::gltf writers do
// the same for ERHE_node / ERHE_camera / ERHE_light).
[[nodiscard]] auto json_properties(const erhe::Item_base& item) -> nlohmann::json
{
    return nlohmann::json::parse(erhe::gltf::item_local_properties_to_json(item));
}

// Same guard as the scene.json save passes: nodes inside a prefab instance
// subtree are not exported (the instance root exports as an externalAsset
// reference), so no payloads may be built for them.
[[nodiscard]] auto is_inside_prefab_instance(const erhe::scene::Node* node) -> bool
{
    for (
        std::shared_ptr<erhe::scene::Node> ancestor = node->get_parent_node();
        ancestor;
        ancestor = ancestor->get_parent_node()
    ) {
        if (erhe::scene::get_attachment<Prefab_instance>(ancestor.get())) {
            return true;
        }
    }
    return false;
}

// Appends extension members to a payload slot ("member" form, no
// surrounding braces; see Gltf_export_extension_payloads).
void append_members(std::string& slot, const std::string& members)
{
    if (!slot.empty()) {
        slot += ",";
    }
    slot += members;
}

// Payload data collected up front and captured by the
// asset_extensions_builder callback, which runs inside export_gltf() once
// the glTF indices exist.
class Brush_record
{
public:
    std::string                      name;
    std::size_t                      extra_mesh; // index into Gltf_export_arguments::extra_meshes
    const erhe::primitive::Material* material{nullptr};
    float                            density{1.0f};
    erhe::primitive::Normal_style    normal_style{erhe::primitive::Normal_style::corner_normals};
};

class Material_binding_record
{
public:
    const erhe::primitive::Material* material{nullptr};
    std::string                      slot;
    std::string                      graph_texture_name;
    // Slot sampler state snapshot (graph-texture slots export no glTF
    // texture, so no glTF sampler carries it - without this a wrap=repeat
    // slot reloads as the clamp fallback). Absent when the slot has no
    // explicit sampler.
    bool                                has_sampler{false};
    erhe::graphics::Sampler_address_mode wrap_u{erhe::graphics::Sampler_address_mode::clamp_to_edge};
    erhe::graphics::Sampler_address_mode wrap_v{erhe::graphics::Sampler_address_mode::clamp_to_edge};
    erhe::graphics::Filter               min_filter{erhe::graphics::Filter::linear};
    erhe::graphics::Filter               mag_filter{erhe::graphics::Filter::linear};
};

[[nodiscard]] auto address_mode_name(const erhe::graphics::Sampler_address_mode mode) -> const char*
{
    switch (mode) {
        case erhe::graphics::Sampler_address_mode::repeat:          return "repeat";
        case erhe::graphics::Sampler_address_mode::clamp_to_edge:   return "clamp_to_edge";
        case erhe::graphics::Sampler_address_mode::mirrored_repeat: return "mirrored_repeat";
        default:                                                    return "clamp_to_edge";
    }
}

[[nodiscard]] auto filter_name(const erhe::graphics::Filter filter) -> const char*
{
    return (filter == erhe::graphics::Filter::linear) ? "linear" : "nearest";
}

class Node_binding_record
{
public:
    const erhe::scene::Node* node{nullptr};
    std::string              graph_mesh_name;
};

class Asset_payload_data
{
public:
    std::vector<Brush_record>                                 brushes;
    nlohmann::json                                            graph_textures = nlohmann::json::array();
    nlohmann::json                                            graph_meshes   = nlohmann::json::array();
    std::vector<Material_binding_record>                      material_bindings;
    std::vector<Node_binding_record>                          node_bindings;
    // tag -> tagged nodes; std::map keeps collections sorted by name for
    // deterministic output.
    std::map<std::string, std::vector<const erhe::scene::Node*>> tag_nodes;
};

} // anonymous namespace

namespace {

// ERHE_asset_reference proxies (asset-manager plan phase R6, plan decision
// 3: a reference is never embedded): every library material REFERENCE entry
// with a durable file-scope key exports as a name-only stub + {file, uid}.
// Entries without such a key (scene_local, key-less legacy listings) fall
// back to full-data export with a warning - no data is ever lost, but the
// re-imported file then holds an independent definition. A key pointing at
// the file being written would be a self-reference (prohibited by the
// extension, extending the External Assets cycle rule) and falls back the
// same way.
void add_material_asset_references(
    erhe::gltf::Gltf_export_arguments&      arguments,
    Scene_root&                             scene_root,
    const std::shared_ptr<Content_library>& content_library,
    const std::filesystem::path&            export_path
)
{
    if (!content_library) {
        return;
    }
    const std::filesystem::path export_directory = export_path.parent_path();
    const std::filesystem::path canonical_export_path = normalize_asset_path(export_path);
    Asset_manager* const asset_manager = content_library->get_asset_manager();
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{content_library->mutex};

    // The candidates are every material the written file can name: the
    // resources this scene lists, plus the materials its meshes bind. A
    // material another container defines need not be listed - the mesh
    // binding is what puts it in the file (doc/usd-compatibility-plan.md U4).
    std::vector<std::shared_ptr<erhe::primitive::Material>> candidates =
        content_library->get_all<erhe::primitive::Material>();
    const auto consider_material = [&candidates](const std::shared_ptr<erhe::primitive::Material>& material) {
        if (!material) {
            return;
        }
        if (std::find(candidates.begin(), candidates.end(), material) == candidates.end()) {
            candidates.push_back(material);
        }
    };
    scene_root.get_scene().for_each_node(
        [&consider_material](const std::shared_ptr<erhe::scene::Node>& node) {
            const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(node.get());
            if (mesh) {
                for (const erhe::scene::Mesh_primitive& primitive : mesh->get_primitives()) {
                    consider_material(primitive.material);
                }
            }
            return true;
        }
    );

    for (const std::shared_ptr<erhe::primitive::Material>& material : candidates) {
        {
            // An R6 asset reference: a material the file names but another
            // container DEFINES (the manager's record is the authority,
            // Scene_root::is_asset_definition).
            if (scene_root.is_asset_definition(*material)) {
                continue;
            }
            // The recorded key when the library has one, else the manager's
            // live answer (file scope + uid self-heal).
            const Resource_metadata* const metadata = content_library->find_metadata(*material);
            Resource_metadata live{};
            if ((metadata != nullptr) && metadata->asset_key.has_value()) {
                live.asset_key = metadata->asset_key;
            } else if (asset_manager != nullptr) {
                Asset_key key = asset_manager->make_key(*material);
                if (key.scope == Asset_scope::file) {
                    live.asset_key = std::move(key);
                }
            }
            const Resource_metadata& node = live;
            if (!node.asset_key.has_value() || (node.asset_key->scope != Asset_scope::file) || node.asset_key->path.empty()) {
                log_parsers->warn(
                    "glTF export: reference material '{}' has no file-scope asset key - exporting full data (an independent definition)",
                    material->get_name()
                );
                continue;
            }
            const Asset_key& key = node.asset_key.value();
            const std::filesystem::path container_path{key.path};
            if (normalize_asset_path(container_path) == canonical_export_path) {
                log_parsers->warn(
                    "glTF export: reference material '{}' points at the file being written ('{}') - self-reference prohibited, exporting full data",
                    material->get_name(), key.path
                );
                continue;
            }
            std::error_code error_code;
            const std::filesystem::path relative_path = std::filesystem::relative(container_path, export_directory, error_code);
            const std::string uri = (error_code || relative_path.empty())
                ? container_path.generic_string()
                : relative_path.generic_string();
            const bool is_binary = container_path.extension() == std::filesystem::path{".glb"};
            // Self-heal upward: prefer the live item's uid (learned at
            // resolve) over the key's, so name-resolved references migrate
            // to uid addressing on save.
            const std::string& uid = !material->get_gltf_uid().empty() ? material->get_gltf_uid() : key.uid;
            arguments.material_asset_references.emplace(
                material.get(),
                erhe::gltf::Gltf_export_asset_reference{
                    .uri       = uri,
                    .mime_type = is_binary ? "model/gltf-binary" : "model/gltf+json",
                    .uid       = uid,
                }
            );
        }
    }
}

} // anonymous namespace

void add_gltf_editor_state(
    erhe::gltf::Gltf_export_arguments&                                   arguments,
    Scene_root&                                                          scene_root,
    const std::filesystem::path&                                         export_path,
    const std::vector<std::shared_ptr<erhe::physics::Physics_material>>& physics_material_items
)
{
    add_material_asset_references(arguments, scene_root, scene_root.get_content_library(), export_path);

    const erhe::scene::Scene& scene = scene_root.get_scene();
    const std::shared_ptr<Content_library> content_library = scene_root.get_content_library();
    const std::shared_ptr<erhe::scene::Node> scene_root_node = scene.get_root_node();

    const std::shared_ptr<Asset_payload_data> data = std::make_shared<Asset_payload_data>();

    bool used_physics = false;
    bool used_layout  = false;

    scene.for_each_node([&](const std::shared_ptr<erhe::scene::Node>& node) {
        if (node == scene_root_node) {
            return true; // the scene root itself is not a glTF node
        }
        if ((node->get_flag_bits() & erhe::Item_flags::import_root) != 0) {
            return true; // implicit containers are unwrapped by the exporter
        }
        if (is_inside_prefab_instance(node.get())) {
            return true;
        }

        // Exclusion hook + ERHE_node_graphs node bindings: a Geometry Graph
        // Mesh attachment's controlled products are baked artifacts the
        // graph rebuilds on load.
        std::shared_ptr<Node_physics> node_physics = erhe::scene::get_attachment<Node_physics>(node.get());
        const std::shared_ptr<Geometry_graph_mesh> graph_mesh_attachment = erhe::scene::get_attachment<Geometry_graph_mesh>(node.get());
        if (graph_mesh_attachment) {
            if (graph_mesh_attachment->get_controlled_mesh()) {
                arguments.excluded_meshes.insert(graph_mesh_attachment->get_controlled_mesh().get());
            }
            if (graph_mesh_attachment->get_controlled_ghost_mesh()) {
                arguments.excluded_meshes.insert(graph_mesh_attachment->get_controlled_ghost_mesh().get());
            }
            if (graph_mesh_attachment->get_controlled_node_physics() == node_physics) {
                node_physics.reset(); // build_gltf_physics_data skips it too
            }
            if (graph_mesh_attachment->get_graph_mesh()) {
                data->node_bindings.push_back(
                    Node_binding_record{
                        .node            = node.get(),
                        .graph_mesh_name = graph_mesh_attachment->get_graph_mesh()->get_name(),
                    }
                );
            }
        }

        // ERHE_physics: erhe rigid-body state KHR_physics_rigid_bodies
        // cannot carry, and the attachment's local property values (its
        // complete local set on reload, the ERHE_light rule). Damping,
        // wind receptivity and density ride the physics material
        // (ERHE_scene physics_materials).
        if (node_physics) {
            nlohmann::json physics_json{
                {"motion_mode", motion_mode_name(node_physics->get_motion_mode())},
                {"properties",  json_properties(*node_physics)},
            };
            append_members(arguments.extension_payloads.nodes[node.get()], fmt::format("\"ERHE_physics\":{}", physics_json.dump()));
            used_physics = true;
        }

        // ERHE_layout: the Layout attachment fields with Item flags. A child's
        // layout hints are attached properties (Layout.align_x, ...) and ride
        // the node's ERHE_node properties map like any local value.
        const std::shared_ptr<erhe::scene::Layout> layout = erhe::scene::get_attachment<erhe::scene::Layout>(node.get());
        if (layout) {
            nlohmann::json layout_json = nlohmann::json::object();
            if (layout) {
                layout_json["layout"] = nlohmann::json{
                    {"name",             layout->get_name()},
                    {"type",             layout_type_name(layout->get_layout_type())},
                    {"volume_min",       json_vec3(layout->get_volume().min)},
                    {"volume_max",       json_vec3(layout->get_volume().max)},
                    {"primary",          axis_direction_name(layout->get_primary())},
                    {"secondary",        axis_direction_name(layout->get_secondary())},
                    {"tertiary",         axis_direction_name(layout->get_tertiary())},
                    {"gap",              json_vec3(layout->get_gap())},
                    {"grid_track_count", json_ivec3(layout->get_grid_track_count())},
                    {"grid_track_extent_x", json_float_array(layout->get_grid_track_extent(0))},
                    {"grid_track_extent_y", json_float_array(layout->get_grid_track_extent(1))},
                    {"grid_track_extent_z", json_float_array(layout->get_grid_track_extent(2))},
                    {"flags",            json_flags(*layout)},
                    {"properties",       json_properties(*layout)},
                };
            }
            append_members(arguments.extension_payloads.nodes[node.get()], fmt::format("\"ERHE_layout\":{}", layout_json.dump()));
            used_layout = true;
        }

        // ERHE_collections: item tags (runtime-only Item_base state; never
        // persisted before).
        for (const std::string& tag : node->get_tags()) {
            data->tag_nodes[tag].push_back(node.get());
        }
        return true;
    });

    if (used_physics) {
        arguments.extensions_used.push_back("ERHE_physics");
    }
    if (used_layout) {
        arguments.extensions_used.push_back("ERHE_layout");
    }

    // ERHE_scene: per-scene settings (#239), ambient light (#237),
    // enable_physics. Always emitted - its presence in extensionsUsed marks
    // the file as an erhe-authored scene.
    {
        nlohmann::json scene_json{
            {"ambient_light",  json_vec4(scene.ambient_light)},
            {"enable_physics", scene_root.has_physics_world()},
        };
        const Scene_settings& scene_settings = scene_root.get_scene_settings();
        if (!is_default(scene_settings)) {
            // The codegen serializer pretty-prints; minify through nlohmann.
            const nlohmann::json settings_json = nlohmann::json::parse(serialize(scene_settings, 0), nullptr, false);
            if (!settings_json.is_discarded()) {
                scene_json["settings"] = settings_json;
            } else {
                log_parsers->error("add_gltf_editor_state: Scene_settings serialization did not parse - settings not exported");
            }
        }
        // styles (doc/style-library.md D4): every style item with its target
        // class and local values, before the folders that may name them.
        if (content_library) {
            nlohmann::json styles = nlohmann::json::array();
            for (const std::shared_ptr<Style>& style : content_library->get_all<Style>()) {
                if (!style) {
                    continue;
                }
                nlohmann::json entry{{"name", style->get_name()}};
                const nlohmann::json properties = json_properties(*style);
                if (properties.is_object() && !properties.empty()) {
                    entry["properties"] = properties;
                }
                styles.push_back(std::move(entry));
            }
            if (!styles.empty()) {
                scene_json["styles"] = std::move(styles);
            }
        }
        // physics_materials / collision_filter_names: the KHR_physics_rigid_bodies
        // entries by index (the KHR entries carry no name), so the library
        // items keep their names across a reload; a physics material entry
        // also carries the material's local property values (the KHR entry
        // has no carrier for damping, wind receptivity and density, and the
        // map is the material's complete local set on reload).
        if (arguments.physics_data != nullptr) {
            if (!arguments.physics_data->materials.empty()) {
                nlohmann::json materials = nlohmann::json::array();
                const std::vector<erhe::gltf::Physics_material_description>& descriptions = arguments.physics_data->materials;
                for (std::size_t i = 0; i < descriptions.size(); ++i) {
                    nlohmann::json entry{{"name", descriptions[i].name}};
                    if ((i < physics_material_items.size()) && physics_material_items[i]) {
                        entry["properties"] = json_properties(*physics_material_items[i]);
                    }
                    materials.push_back(std::move(entry));
                }
                scene_json["physics_materials"] = std::move(materials);
            }
            if (!arguments.physics_data->collision_filters.empty()) {
                nlohmann::json names = nlohmann::json::array();
                for (const erhe::gltf::Physics_collision_filter_description& filter : arguments.physics_data->collision_filters) {
                    names.push_back(filter.name);
                }
                scene_json["collision_filter_names"] = std::move(names);
            }
        }
        // library_folders (doc/content-library-folders.md D5): every folder
        // scope below a kind scope, depth first, with its local property
        // values and the names of the resources directly in it. The path is
        // the scope path from the kind scope, so a load places the resources
        // back where they sat.
        if (content_library) {
            nlohmann::json library_folders = nlohmann::json::array();
            const std::function<void(const erhe::Scope&, const std::string&)> collect_folder =
                [&](const erhe::Scope& scope, const std::string& scope_path) -> void
                {
                    // A kind scope is not listed: its resources are where a
                    // load puts the ones no folder names.
                    if (!scope_path.empty() && (scope_path.find('/') != std::string::npos)) {
                        nlohmann::json items = nlohmann::json::array();
                        for (const std::shared_ptr<erhe::Hierarchy>& child : scope.get_children()) {
                            if (std::dynamic_pointer_cast<erhe::Scope>(child)) {
                                continue;
                            }
                            items.push_back(child->get_name());
                        }
                        nlohmann::json entry{{"path", scope_path}};
                        const nlohmann::json properties = json_properties(scope);
                        if (properties.is_object() && !properties.empty()) {
                            entry["properties"] = properties;
                        }
                        if (scope.get_style()) {
                            entry["style"] = scope.get_style()->get_reference_path();
                        }
                        if (!items.empty()) {
                            entry["items"] = items;
                        }
                        library_folders.push_back(std::move(entry));
                    }
                    for (const std::shared_ptr<erhe::Hierarchy>& child : scope.get_children()) {
                        const std::shared_ptr<erhe::Scope> child_scope = std::dynamic_pointer_cast<erhe::Scope>(child);
                        if (child_scope) {
                            collect_folder(*child_scope, fmt::format("{}/{}", scope_path, child_scope->get_name()));
                        }
                    }
                };
            for (const uint64_t kind_type_bit : Content_library::get_kind_type_bits()) {
                const std::shared_ptr<erhe::Scope> kind_scope = content_library->find_scope(kind_type_bit);
                if (kind_scope) {
                    collect_folder(*kind_scope, kind_scope->get_name());
                }
            }
            if (!library_folders.empty()) {
                scene_json["library_folders"] = std::move(library_folders);
            }
        }
        append_members(arguments.extension_payloads.scene, fmt::format("\"ERHE_scene\":{}", scene_json.dump()));
        arguments.extensions_used.push_back("ERHE_scene");
    }

    // ERHE_brushes: brush geometry exports as extra unreferenced glTF
    // meshes (ERHE_geometry path); the brush metadata references them by
    // mesh index via the builder below. The collision shape is rebuilt at
    // first instantiation (Brush::late_initialize), as today.
    if (content_library) {
        // Depth-first walk preserving the content-library folder hierarchy
        // (same traversal as save_scene's brush pass).
        const std::function<void(const erhe::Hierarchy&, const std::string&)> collect_brush_folder =
            [&](const erhe::Hierarchy& scope, const std::string& folder_path) -> void
            {
                for (const std::shared_ptr<erhe::Hierarchy>& child : scope.get_children()) {
                    const std::shared_ptr<Brush> brush = std::dynamic_pointer_cast<Brush>(child);
                    if (!brush) {
                        // A folder scope: recurse, extending the path.
                        const std::string child_path = folder_path.empty()
                            ? child->get_name()
                            : fmt::format("{}/{}", folder_path, child->get_name());
                        collect_brush_folder(*child, child_path);
                        continue;
                    }
                    const std::shared_ptr<erhe::geometry::Geometry> geometry = brush->get_geometry();
                    if (!geometry) {
                        log_parsers->warn("add_gltf_editor_state: brush '{}' has no geometry - not exported", brush->get_name());
                        continue;
                    }
                    data->brushes.push_back(
                        Brush_record{
                            .name         = brush->get_name(),
                            .extra_mesh   = arguments.extra_meshes.size(),
                            .material     = brush->get_material().get(),
                            .density      = brush->get_density(),
                            .normal_style = brush->get_normal_style(),
                        }
                    );
                    arguments.extra_meshes.push_back(
                        erhe::gltf::Gltf_export_extra_mesh{
                            .name     = brush->get_name(),
                            .geometry = geometry,
                            .material = brush->get_material(),
                        }
                    );
                }
            };
        const std::shared_ptr<erhe::Scope> brushes_scope = content_library->find_scope(erhe::Item_type::brush);
        if (brushes_scope) {
            collect_brush_folder(*brushes_scope, std::string{});
        }
    }

    // ERHE_node_graphs: graph assets embed their node-graph JSON natively
    // (no string-in-string escaping, unlike scene.json).
    if (content_library) {
        for (const std::shared_ptr<Graph_texture>& graph_texture : content_library->get_all<Graph_texture>()) {
            data->graph_textures.push_back(
                nlohmann::json{
                    {"name",  graph_texture->get_name()},
                    {"graph", write_graph_texture_graph(*graph_texture)},
                }
            );
        }
    }
    if (content_library) {
        for (const std::shared_ptr<Graph_mesh>& graph_mesh : content_library->get_all<Graph_mesh>()) {
            data->graph_meshes.push_back(
                nlohmann::json{
                    {"name",  graph_mesh->get_name()},
                    {"graph", write_graph_mesh_graph(*graph_mesh)},
                }
            );
        }
    }
    // Materials referenced only through graphs must be exported explicitly:
    // graph-controlled meshes are excluded from export and the exporter's
    // material pass is lazy (only mesh-referenced materials are written), so
    // without this the graph output node's by-name material reference (and
    // the material_bindings side table below) resolved nothing on load and
    // reloaded graph meshes rendered with the default white material.
    const auto add_extra_material = [&arguments](const std::shared_ptr<erhe::primitive::Material>& material) {
        const auto it = std::find(arguments.extra_materials.begin(), arguments.extra_materials.end(), material);
        if (it == arguments.extra_materials.end()) {
            arguments.extra_materials.push_back(material);
        }
    };

    if (content_library) {
        const auto add_binding = [&data](const erhe::primitive::Material& material, const char* slot, const erhe::primitive::Material_texture_sampler& sampler) {
            const Graph_texture* graph_texture = dynamic_cast<const Graph_texture*>(sampler.texture_reference.get());
            if (graph_texture != nullptr) {
                Material_binding_record record{
                    .material           = &material,
                    .slot               = slot,
                    .graph_texture_name = graph_texture->get_name(),
                };
                if (sampler.sampler != erhe::primitive::Material_sampler_state{}) {
                    record.has_sampler = true;
                    record.wrap_u      = sampler.sampler.wrap_u;
                    record.wrap_v      = sampler.sampler.wrap_v;
                    record.min_filter  = sampler.sampler.min_filter;
                    record.mag_filter  = sampler.sampler.mag_filter;
                }
                data->material_bindings.push_back(record);
            }
        };
        for (const std::shared_ptr<erhe::primitive::Material>& material : content_library->get_all<erhe::primitive::Material>()) {
            const erhe::primitive::Material_texture_samplers& samplers = material->data.texture_samplers;
            const std::size_t bindings_before = data->material_bindings.size();
            add_binding(*material, "base_color",         samplers.base_color);
            add_binding(*material, "metallic_roughness", samplers.metallic_roughness);
            add_binding(*material, "normal",             samplers.normal);
            add_binding(*material, "occlusion",          samplers.occlusion);
            add_binding(*material, "emissive",           samplers.emissive);
            if (data->material_bindings.size() != bindings_before) {
                add_extra_material(material);
            }
        }
    }

    if (content_library) {
        // Walk the just-serialized graph JSON for output-node material
        // references (geometry and texture graph output nodes all write
        // parameters.material as a content-library material name).
        const auto add_graph_output_materials = [&content_library, &add_extra_material](const nlohmann::json& graph_entries, const char* asset_kind) {
            for (const nlohmann::json& entry : graph_entries) {
                const auto graph_it = entry.find("graph");
                if ((graph_it == entry.end()) || !graph_it->is_object()) {
                    continue;
                }
                const auto nodes_it = graph_it->find("nodes");
                if ((nodes_it == graph_it->end()) || !nodes_it->is_array()) {
                    continue;
                }
                for (const nlohmann::json& node_entry : *nodes_it) {
                    const auto parameters_it = node_entry.find("parameters");
                    if ((parameters_it == node_entry.end()) || !parameters_it->is_object()) {
                        continue;
                    }
                    const auto material_it = parameters_it->find("material");
                    if ((material_it == parameters_it->end()) || !material_it->is_string()) {
                        continue;
                    }
                    const std::string material_name = material_it->get<std::string>();
                    if (material_name.empty()) {
                        continue;
                    }
                    std::shared_ptr<erhe::primitive::Material> material{};
                    for (const std::shared_ptr<erhe::primitive::Material>& candidate : content_library->get_all<erhe::primitive::Material>()) {
                        if (candidate->get_name() == material_name) {
                            material = candidate;
                            break;
                        }
                    }
                    if (material) {
                        add_extra_material(material);
                    } else {
                        log_parsers->warn(
                            "glTF editor state: {} '{}' references material '{}' not found in the content library - reference will not resolve on load",
                            asset_kind, entry.value("name", std::string{}), material_name
                        );
                    }
                }
            }
        };
        add_graph_output_materials(data->graph_meshes,   "Graph Mesh");
        add_graph_output_materials(data->graph_textures, "Graph Texture");
    }

    // Asset-root payloads are resolved against glTF indices inside
    // export_gltf() (nodes / materials / extra meshes are only numbered
    // once the export passes have run).
    arguments.asset_extensions_builder =
        [data](const erhe::gltf::Gltf_export_index_lookup& lookup) -> std::vector<std::pair<std::string, std::string>>
        {
            std::vector<std::pair<std::string, std::string>> result;

            if (!data->brushes.empty()) {
                nlohmann::json brushes = nlohmann::json::array();
                for (const Brush_record& record : data->brushes) {
                    if ((record.extra_mesh >= lookup.extra_mesh_indices.size()) || !lookup.extra_mesh_indices[record.extra_mesh].has_value()) {
                        log_parsers->warn("glTF editor state: brush '{}' geometry was not exported - brush dropped", record.name);
                        continue;
                    }
                    nlohmann::json entry{
                        {"name",         record.name},
                        {"mesh",         lookup.extra_mesh_indices[record.extra_mesh].value()},
                        {"density",      json_float(record.density)},
                        {"normal_style", normal_style_name(record.normal_style)},
                    };
                    if (record.material != nullptr) {
                        const auto material_it = lookup.material_indices.find(record.material);
                        if (material_it != lookup.material_indices.end()) {
                            entry["material"] = material_it->second;
                        } else {
                            log_parsers->warn("glTF editor state: brush '{}' material was not exported - reference dropped", record.name);
                        }
                    }
                    brushes.push_back(std::move(entry));
                }
                if (!brushes.empty()) {
                    result.emplace_back("ERHE_brushes", nlohmann::json{{"brushes", brushes}}.dump());
                }
            }

            {
                nlohmann::json node_graphs = nlohmann::json::object();
                if (!data->graph_textures.empty()) {
                    node_graphs["graph_textures"] = data->graph_textures;
                }
                if (!data->graph_meshes.empty()) {
                    node_graphs["graph_meshes"] = data->graph_meshes;
                }
                nlohmann::json material_bindings = nlohmann::json::array();
                for (const Material_binding_record& record : data->material_bindings) {
                    const auto material_it = lookup.material_indices.find(record.material);
                    if (material_it == lookup.material_indices.end()) {
                        log_parsers->warn(
                            "glTF editor state: material '{}' bound to graph texture '{}' was not exported (referenced by no mesh) - binding dropped",
                            record.material->get_name(), record.graph_texture_name
                        );
                        continue;
                    }
                    nlohmann::json binding_entry{
                        {"material",      material_it->second},
                        {"slot",          record.slot},
                        {"graph_texture", record.graph_texture_name},
                    };
                    if (record.has_sampler) {
                        binding_entry["wrap"]       = {address_mode_name(record.wrap_u), address_mode_name(record.wrap_v)};
                        binding_entry["min_filter"] = filter_name(record.min_filter);
                        binding_entry["mag_filter"] = filter_name(record.mag_filter);
                    }
                    material_bindings.push_back(std::move(binding_entry));
                }
                if (!material_bindings.empty()) {
                    node_graphs["material_bindings"] = std::move(material_bindings);
                }
                nlohmann::json node_bindings = nlohmann::json::array();
                for (const Node_binding_record& record : data->node_bindings) {
                    const auto node_it = lookup.node_indices.find(record.node);
                    if (node_it == lookup.node_indices.end()) {
                        log_parsers->warn(
                            "glTF editor state: node '{}' bound to graph mesh '{}' was not exported - binding dropped",
                            record.node->get_name(), record.graph_mesh_name
                        );
                        continue;
                    }
                    node_bindings.push_back(
                        nlohmann::json{
                            {"node",       node_it->second},
                            {"graph_mesh", record.graph_mesh_name},
                        }
                    );
                }
                if (!node_bindings.empty()) {
                    node_graphs["node_bindings"] = std::move(node_bindings);
                }
                if (!node_graphs.empty()) {
                    result.emplace_back("ERHE_node_graphs", node_graphs.dump());
                }
            }

            if (!data->tag_nodes.empty()) {
                nlohmann::json collections = nlohmann::json::array();
                for (const auto& [tag, nodes] : data->tag_nodes) {
                    nlohmann::json items = nlohmann::json::array();
                    for (const erhe::scene::Node* node : nodes) {
                        const auto node_it = lookup.node_indices.find(node);
                        if (node_it == lookup.node_indices.end()) {
                            log_parsers->warn("glTF editor state: tagged node '{}' was not exported - dropped from collection '{}'", node->get_name(), tag);
                            continue;
                        }
                        items.push_back(node_it->second);
                    }
                    if (!items.empty()) {
                        std::sort(items.begin(), items.end());
                        collections.push_back(nlohmann::json{{"name", tag}, {"items", std::move(items)}});
                    }
                }
                if (!collections.empty()) {
                    result.emplace_back("ERHE_collections", nlohmann::json{{"collections", collections}}.dump());
                }
            }

            return result;
        };
}

}
