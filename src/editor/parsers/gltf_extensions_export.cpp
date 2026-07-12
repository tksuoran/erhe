#include "parsers/gltf_extensions_export.hpp"

#include "parsers/gltf_extensions_names.hpp"

#include "brushes/brush.hpp"
#include "content_library/content_library.hpp"
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
#include "erhe_physics/irigid_body.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/layout.hpp"
#include "erhe_scene/layout_item.hpp"
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
    std::string                      folder_path;
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
};

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

void add_gltf_editor_state(erhe::gltf::Gltf_export_arguments& arguments, Scene_root& scene_root)
{
    const erhe::scene::Scene& scene = scene_root.get_scene();
    const std::vector<std::shared_ptr<erhe::scene::Node>>& flat_nodes = scene.get_flat_nodes();
    const std::shared_ptr<Content_library> content_library = scene_root.get_content_library();
    const std::shared_ptr<erhe::scene::Node> scene_root_node = scene.get_root_node();

    const std::shared_ptr<Asset_payload_data> data = std::make_shared<Asset_payload_data>();

    bool used_physics = false;
    bool used_layout  = false;

    for (const std::shared_ptr<erhe::scene::Node>& node : flat_nodes) {
        if (node == scene_root_node) {
            continue; // the scene root itself is not a glTF node
        }
        if ((node->get_flag_bits() & erhe::Item_flags::import_root) != 0) {
            continue; // implicit containers are unwrapped by the exporter
        }
        if (is_inside_prefab_instance(node.get())) {
            continue;
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
        // cannot carry (defaults mirror scene.json's).
        if (node_physics) {
            const erhe::physics::IRigid_body* rigid_body = node_physics->get_rigid_body();
            nlohmann::json physics_json{
                {"motion_mode",     motion_mode_name(node_physics->get_motion_mode())},
                {"friction",        json_float(rigid_body ? rigid_body->get_friction()        : 0.5f)},
                {"restitution",     json_float(rigid_body ? rigid_body->get_restitution()     : 0.2f)},
                {"linear_damping",  json_float(rigid_body ? rigid_body->get_linear_damping()  : 0.05f)},
                {"angular_damping", json_float(rigid_body ? rigid_body->get_angular_damping() : 0.05f)},
            };
            append_members(arguments.extension_payloads.nodes[node.get()], fmt::format("\"ERHE_physics\":{}", physics_json.dump()));
            used_physics = true;
        }

        // ERHE_layout: Layout and Layout_item attachment fields (two
        // optional sub-objects on one extension), each with Item flags.
        const std::shared_ptr<erhe::scene::Layout>      layout      = erhe::scene::get_attachment<erhe::scene::Layout>(node.get());
        const std::shared_ptr<erhe::scene::Layout_item> layout_item = erhe::scene::get_attachment<erhe::scene::Layout_item>(node.get());
        if (layout || layout_item) {
            nlohmann::json layout_json = nlohmann::json::object();
            if (layout) {
                layout_json["layout"] = nlohmann::json{
                    {"name",             layout->get_name()},
                    {"type",             layout_type_name(layout->type)},
                    {"volume_min",       json_vec3(layout->volume.min)},
                    {"volume_max",       json_vec3(layout->volume.max)},
                    {"primary",          axis_direction_name(layout->primary)},
                    {"secondary",        axis_direction_name(layout->secondary)},
                    {"tertiary",         axis_direction_name(layout->tertiary)},
                    {"gap",              json_vec3(layout->gap)},
                    {"grid_track_count", json_ivec3(layout->grid_track_count)},
                    {"grid_track_extent_x", json_float_array(layout->grid_track_extent[0])},
                    {"grid_track_extent_y", json_float_array(layout->grid_track_extent[1])},
                    {"grid_track_extent_z", json_float_array(layout->grid_track_extent[2])},
                    {"flags",            json_flags(*layout)},
                };
            }
            if (layout_item) {
                layout_json["layout_item"] = nlohmann::json{
                    {"name",           layout_item->get_name()},
                    {"align",          nlohmann::json::array({
                                           layout_alignment_name(layout_item->alignment[0]),
                                           layout_alignment_name(layout_item->alignment[1]),
                                           layout_alignment_name(layout_item->alignment[2])})},
                    {"margin_min",     json_vec3(layout_item->margin_min)},
                    {"margin_max",     json_vec3(layout_item->margin_max)},
                    {"grid_cell_auto", layout_item->grid_cell_auto},
                    {"grid_cell",      json_ivec3(layout_item->grid_cell)},
                    {"grid_span",      json_ivec3(layout_item->grid_span)},
                    {"flags",          json_flags(*layout_item)},
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
    }

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
        append_members(arguments.extension_payloads.scene, fmt::format("\"ERHE_scene\":{}", scene_json.dump()));
        arguments.extensions_used.push_back("ERHE_scene");
    }

    // ERHE_brushes: brush geometry exports as extra unreferenced glTF
    // meshes (ERHE_geometry path); the brush metadata references them by
    // mesh index via the builder below. The collision shape is rebuilt at
    // first instantiation (Brush::late_initialize), as today.
    if (content_library && content_library->brushes) {
        // Depth-first walk preserving the content-library folder hierarchy
        // (same traversal as save_scene's brush pass).
        const std::function<void(const Content_library_node&, const std::string&)> collect_brush_folder =
            [&](const Content_library_node& folder_node, const std::string& folder_path) -> void
            {
                for (const std::shared_ptr<erhe::Hierarchy>& child_hierarchy : folder_node.get_children()) {
                    const std::shared_ptr<Content_library_node> child = std::dynamic_pointer_cast<Content_library_node>(child_hierarchy);
                    if (!child) {
                        continue;
                    }
                    const std::shared_ptr<Brush> brush = std::dynamic_pointer_cast<Brush>(child->item);
                    if (!brush) {
                        // A folder node (item == nullptr): recurse, extending the path.
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
                            .folder_path  = folder_path,
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
        collect_brush_folder(*content_library->brushes, std::string{});
    }

    // ERHE_node_graphs: graph assets embed their node-graph JSON natively
    // (no string-in-string escaping, unlike scene.json).
    if (content_library && content_library->graph_textures) {
        for (const std::shared_ptr<Graph_texture>& graph_texture : content_library->graph_textures->get_all<Graph_texture>()) {
            data->graph_textures.push_back(
                nlohmann::json{
                    {"name",  graph_texture->get_name()},
                    {"graph", write_graph_texture_graph(*graph_texture)},
                }
            );
        }
    }
    if (content_library && content_library->graph_meshes) {
        for (const std::shared_ptr<Graph_mesh>& graph_mesh : content_library->graph_meshes->get_all<Graph_mesh>()) {
            data->graph_meshes.push_back(
                nlohmann::json{
                    {"name",  graph_mesh->get_name()},
                    {"graph", write_graph_mesh_graph(*graph_mesh)},
                }
            );
        }
    }
    if (content_library && content_library->materials && content_library->graph_textures) {
        const auto add_binding = [&data](const erhe::primitive::Material& material, const char* slot, const erhe::primitive::Material_texture_sampler& sampler) {
            const Graph_texture* graph_texture = dynamic_cast<const Graph_texture*>(sampler.texture_reference.get());
            if (graph_texture != nullptr) {
                data->material_bindings.push_back(
                    Material_binding_record{
                        .material           = &material,
                        .slot               = slot,
                        .graph_texture_name = graph_texture->get_name(),
                    }
                );
            }
        };
        for (const std::shared_ptr<erhe::primitive::Material>& material : content_library->materials->get_all<erhe::primitive::Material>()) {
            const erhe::primitive::Material_texture_samplers& samplers = material->data.texture_samplers;
            add_binding(*material, "base_color",         samplers.base_color);
            add_binding(*material, "metallic_roughness", samplers.metallic_roughness);
            add_binding(*material, "normal",             samplers.normal);
            add_binding(*material, "occlusion",          samplers.occlusion);
            add_binding(*material, "emissive",           samplers.emissive);
        }
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
                    if (!record.folder_path.empty()) {
                        entry["folder_path"] = record.folder_path;
                    }
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
                    material_bindings.push_back(
                        nlohmann::json{
                            {"material",      material_it->second},
                            {"slot",          record.slot},
                            {"graph_texture", record.graph_texture_name},
                        }
                    );
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
