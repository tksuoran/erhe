#include "parsers/gltf_extensions_import.hpp"

#include "parsers/gltf_extensions_names.hpp"

#include "app_context.hpp"
#include "brushes/brush.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "geometry_graph/geometry_graph_mesh.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "geometry_graph/graph_mesh_serialization.hpp"
#include "operations/content_library_attach_operation.hpp"
#include "scene/scene_root.hpp"
#include "texture_graph/graph_texture.hpp"
#include "texture_graph/graph_texture_serialization.hpp"

#include "scene/generated/gltf_source_reference.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_gltf/gltf.hpp"
#include "erhe_gltf/gltf_item_flags.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/layout.hpp"
#include "erhe_scene/layout_item.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <geogram/mesh/mesh.h>

namespace editor {

namespace {

// The minified JSON of an ERHE_* extension captured on an object, or
// nullptr when the object does not carry it.
[[nodiscard]] auto find_extension(const erhe::gltf::Gltf_raw_extensions& extensions, const std::string_view name) -> const std::string*
{
    for (const auto& [extension_name, extension_json] : extensions.entries) {
        if (extension_name == name) {
            return &extension_json;
        }
    }
    return nullptr;
}

[[nodiscard]] auto parse_extension_object(const std::string& extension_json, const char* extension_name, const std::string& owner_name) -> nlohmann::json
{
    nlohmann::json parsed = nlohmann::json::parse(extension_json, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        log_parsers->warn("glTF editor state: {} on '{}' does not parse as an object - ignored", extension_name, owner_name);
        return nlohmann::json{};
    }
    return parsed;
}

[[nodiscard]] auto to_vec3(const nlohmann::json& value, const glm::vec3& fallback) -> glm::vec3
{
    if (!value.is_array() || (value.size() < 3)) {
        return fallback;
    }
    return glm::vec3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
}

[[nodiscard]] auto to_ivec3(const nlohmann::json& value, const glm::ivec3& fallback) -> glm::ivec3
{
    if (!value.is_array() || (value.size() < 3)) {
        return fallback;
    }
    return glm::ivec3{value[0].get<int>(), value[1].get<int>(), value[2].get<int>()};
}

// Applies a "flags" name array exactly (enable listed, disable unlisted
// persistent flags); leaves the item's flags untouched when the payload
// carries no flags array (older / foreign files keep their default set).
void apply_flags(erhe::Item_base& item, const nlohmann::json& payload)
{
    const auto it = payload.find("flags");
    if ((it == payload.end()) || !it->is_array()) {
        return;
    }
    uint64_t listed_bits = 0;
    for (const nlohmann::json& flag_name : *it) {
        if (flag_name.is_string()) {
            listed_bits |= erhe::gltf::persistent_item_flag_from_name(flag_name.get<std::string>());
        }
    }
    erhe::gltf::apply_persistent_item_flags(item, listed_bits);
}

} // anonymous namespace

auto parse_gltf_physics_overrides(const erhe::gltf::Gltf_data& gltf_data)
    -> std::unordered_map<const erhe::scene::Node*, Gltf_physics_overrides>
{
    std::unordered_map<const erhe::scene::Node*, Gltf_physics_overrides> overrides;
    for (std::size_t i = 0, end = gltf_data.node_extensions.size(); i < end; ++i) {
        if ((i >= gltf_data.nodes.size()) || !gltf_data.nodes[i]) {
            continue;
        }
        const std::string* extension_json = find_extension(gltf_data.node_extensions[i], "ERHE_physics");
        if (extension_json == nullptr) {
            continue;
        }
        const nlohmann::json payload = parse_extension_object(*extension_json, "ERHE_physics", gltf_data.nodes[i]->get_name());
        if (payload.is_null()) {
            continue;
        }
        Gltf_physics_overrides entry{};
        if (payload.contains("motion_mode") && payload["motion_mode"].is_string()) {
            entry.motion_mode = motion_mode_from_name(payload["motion_mode"].get<std::string>());
        }
        if (payload.contains("friction")        && payload["friction"].is_number())        { entry.friction        = payload["friction"].get<float>(); }
        if (payload.contains("restitution")     && payload["restitution"].is_number())     { entry.restitution     = payload["restitution"].get<float>(); }
        if (payload.contains("linear_damping")  && payload["linear_damping"].is_number())  { entry.linear_damping  = payload["linear_damping"].get<float>(); }
        if (payload.contains("angular_damping") && payload["angular_damping"].is_number()) { entry.angular_damping = payload["angular_damping"].get<float>(); }
        overrides.emplace(gltf_data.nodes[i].get(), entry);
    }
    return overrides;
}

auto parse_gltf_scene_state(const erhe::gltf::Gltf_data& gltf_data) -> std::optional<Gltf_scene_state>
{
    const std::string* extension_json = find_extension(gltf_data.scene_extensions, "ERHE_scene");
    if (extension_json == nullptr) {
        return std::nullopt;
    }
    const nlohmann::json payload = parse_extension_object(*extension_json, "ERHE_scene", "scene");
    if (payload.is_null()) {
        return std::nullopt;
    }
    Gltf_scene_state state{};
    const auto ambient_it = payload.find("ambient_light");
    if ((ambient_it != payload.end()) && ambient_it->is_array() && (ambient_it->size() >= 3)) {
        state.ambient_light = glm::vec4{
            (*ambient_it)[0].get<float>(),
            (*ambient_it)[1].get<float>(),
            (*ambient_it)[2].get<float>(),
            (ambient_it->size() >= 4) ? (*ambient_it)[3].get<float>() : 0.0f
        };
    }
    if (payload.contains("enable_physics") && payload["enable_physics"].is_boolean()) {
        state.enable_physics = payload["enable_physics"].get<bool>();
    }
    if (payload.contains("settings") && payload["settings"].is_object()) {
        state.settings_json = payload["settings"].dump();
    }
    return state;
}

namespace {

void import_layouts(const erhe::gltf::Gltf_data& gltf_data)
{
    for (std::size_t i = 0, end = gltf_data.node_extensions.size(); i < end; ++i) {
        if ((i >= gltf_data.nodes.size()) || !gltf_data.nodes[i]) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Node>& node = gltf_data.nodes[i];
        const std::string* extension_json = find_extension(gltf_data.node_extensions[i], "ERHE_layout");
        if (extension_json == nullptr) {
            continue;
        }
        const nlohmann::json payload = parse_extension_object(*extension_json, "ERHE_layout", node->get_name());
        if (payload.is_null()) {
            continue;
        }
        const auto layout_it = payload.find("layout");
        if ((layout_it != payload.end()) && layout_it->is_object()) {
            const nlohmann::json& lj = *layout_it;
            auto layout = std::make_shared<erhe::scene::Layout>(lj.value("name", std::string{"Layout"}));
            layout->type             = layout_type_from_name(lj.value("type", std::string{"stack"}));
            layout->volume.min       = to_vec3(lj.value("volume_min", nlohmann::json{}), layout->volume.min);
            layout->volume.max       = to_vec3(lj.value("volume_max", nlohmann::json{}), layout->volume.max);
            layout->primary          = axis_direction_from_name(lj.value("primary",   std::string{"pos_x"}));
            layout->secondary        = axis_direction_from_name(lj.value("secondary", std::string{"pos_y"}));
            layout->tertiary         = axis_direction_from_name(lj.value("tertiary",  std::string{"pos_z"}));
            layout->gap              = to_vec3(lj.value("gap", nlohmann::json{}), layout->gap);
            layout->grid_track_count = to_ivec3(lj.value("grid_track_count", nlohmann::json{}), layout->grid_track_count);
            const char* extent_keys[3] = {"grid_track_extent_x", "grid_track_extent_y", "grid_track_extent_z"};
            for (int axis = 0; axis < 3; ++axis) {
                const auto extent_it = lj.find(extent_keys[axis]);
                if ((extent_it != lj.end()) && extent_it->is_array()) {
                    for (const nlohmann::json& extent : *extent_it) {
                        if (extent.is_number()) {
                            layout->grid_track_extent[static_cast<std::size_t>(axis)].push_back(extent.get<float>());
                        }
                    }
                }
            }
            layout->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui | erhe::Item_flags::show_debug_visualizations);
            apply_flags(*layout, lj);
            node->attach(layout);
        }
        const auto item_it = payload.find("layout_item");
        if ((item_it != payload.end()) && item_it->is_object()) {
            const nlohmann::json& ij = *item_it;
            auto layout_item = std::make_shared<erhe::scene::Layout_item>(ij.value("name", std::string{"Layout item"}));
            const auto align_it = ij.find("align");
            if ((align_it != ij.end()) && align_it->is_array() && (align_it->size() >= 3)) {
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    if ((*align_it)[axis].is_string()) {
                        layout_item->alignment[axis] = layout_alignment_from_name((*align_it)[axis].get<std::string>());
                    }
                }
            }
            layout_item->margin_min     = to_vec3(ij.value("margin_min", nlohmann::json{}), layout_item->margin_min);
            layout_item->margin_max     = to_vec3(ij.value("margin_max", nlohmann::json{}), layout_item->margin_max);
            layout_item->grid_cell_auto = ij.value("grid_cell_auto", layout_item->grid_cell_auto);
            layout_item->grid_cell      = to_ivec3(ij.value("grid_cell", nlohmann::json{}), layout_item->grid_cell);
            layout_item->grid_span      = to_ivec3(ij.value("grid_span", nlohmann::json{}), layout_item->grid_span);
            layout_item->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
            apply_flags(*layout_item, ij);
            node->attach(layout_item);
        }
    }
}

void import_collections(const erhe::gltf::Gltf_data& gltf_data)
{
    const std::string* extension_json = find_extension(gltf_data.asset_extensions, "ERHE_collections");
    if (extension_json == nullptr) {
        return;
    }
    const nlohmann::json payload = parse_extension_object(*extension_json, "ERHE_collections", "asset");
    const auto collections_it = payload.find("collections");
    if ((collections_it == payload.end()) || !collections_it->is_array()) {
        return;
    }
    for (const nlohmann::json& collection : *collections_it) {
        if (!collection.is_object() || !collection.contains("name") || !collection["name"].is_string()) {
            continue;
        }
        const std::string name = collection["name"].get<std::string>();
        const auto items_it = collection.find("items");
        if ((items_it == collection.end()) || !items_it->is_array()) {
            continue;
        }
        for (const nlohmann::json& item : *items_it) {
            if (!item.is_number_unsigned()) {
                continue;
            }
            const std::size_t node_index = item.get<std::size_t>();
            if ((node_index >= gltf_data.nodes.size()) || !gltf_data.nodes[node_index]) {
                log_parsers->warn("glTF editor state: collection '{}' references node index {} out of range - skipped", name, node_index);
                continue;
            }
            gltf_data.nodes[node_index]->add_tag(name);
        }
    }
}

// Resolve (creating as needed) the content-library folder for a
// slash-separated path relative to the given root, so the saved folder
// hierarchy is reconstructed instead of flattened (same logic as
// load_scene's brush pass).
[[nodiscard]] auto resolve_library_folder(
    const std::shared_ptr<Content_library_node>& root,
    const std::string&                           folder_path
) -> std::shared_ptr<Content_library_node>
{
    std::shared_ptr<Content_library_node> current = root;
    std::size_t start = 0;
    while (start < folder_path.size()) {
        const std::size_t slash = folder_path.find('/', start);
        const std::string name  = (slash == std::string::npos)
            ? folder_path.substr(start)
            : folder_path.substr(start, slash - start);
        start = (slash == std::string::npos) ? folder_path.size() : slash + 1;
        if (name.empty()) {
            continue;
        }
        std::shared_ptr<Content_library_node> found{};
        for (const std::shared_ptr<erhe::Hierarchy>& child_hierarchy : current->get_children()) {
            const std::shared_ptr<Content_library_node> child = std::dynamic_pointer_cast<Content_library_node>(child_hierarchy);
            // A folder node has no item; match by name.
            if (child && !child->item && (child->get_name() == name)) {
                found = child;
                break;
            }
        }
        if (!found) {
            found = current->make_folder(name);
        }
        current = found;
    }
    return current;
}

void import_brushes(
    App_context&                             context,
    const erhe::gltf::Gltf_data&             gltf_data,
    const std::shared_ptr<Content_library>&  content_library,
    const std::string&                       gltf_path_str,
    std::vector<std::shared_ptr<Operation>>& operations
)
{
    const std::string* extension_json = find_extension(gltf_data.asset_extensions, "ERHE_brushes");
    if (extension_json == nullptr) {
        return;
    }
    if (!content_library || !content_library->brushes) {
        return;
    }
    const nlohmann::json payload = parse_extension_object(*extension_json, "ERHE_brushes", "asset");
    const auto brushes_it = payload.find("brushes");
    if ((brushes_it == payload.end()) || !brushes_it->is_array()) {
        return;
    }

    const erhe::primitive::Build_info brush_build_info{
        .primitive_types = {
            .fill_triangles          = true,
            .fill_triangles_expanded = true,
            .edge_lines              = true,
            .corner_points           = true,
            .centroid_points         = true,
        },
        .buffer_info = context.mesh_memory->make_primitive_buffer_info()
    };

    int brush_index = 0;
    for (const nlohmann::json& entry : *brushes_it) {
        ++brush_index;
        if (!entry.is_object() || !entry.contains("mesh") || !entry["mesh"].is_number_unsigned()) {
            log_parsers->warn("glTF editor state: ERHE_brushes entry {} has no mesh index - skipped", brush_index - 1);
            continue;
        }
        const std::string name       = entry.value("name", fmt::format("Brush {}", brush_index - 1));
        const std::size_t mesh_index = entry["mesh"].get<std::size_t>();
        if ((mesh_index >= gltf_data.meshes.size()) || !gltf_data.meshes[mesh_index]) {
            log_parsers->warn("glTF editor state: brush '{}' references mesh index {} out of range - skipped", name, mesh_index);
            continue;
        }

        // Brush geometry: the first geometry-carrying primitive of the
        // referenced (node-unreferenced) glTF mesh, restored bit-exact
        // through ERHE_geometry.
        std::shared_ptr<erhe::geometry::Geometry> geometry{};
        for (const erhe::scene::Mesh_primitive& mesh_primitive : gltf_data.meshes[mesh_index]->get_primitives()) {
            if (mesh_primitive.primitive && mesh_primitive.primitive->render_shape) {
                geometry = mesh_primitive.primitive->render_shape->get_geometry();
                if (geometry) {
                    break;
                }
            }
        }
        if (!geometry) {
            log_parsers->warn("glTF editor state: brush '{}' mesh {} carries no geometry (ERHE_geometry missing?) - skipped", name, mesh_index);
            continue;
        }
        // Geometry restored from the ERHE_geometry dump already carries
        // edges (and every other attribute) byte-exact - reprocessing would
        // overwrite it. Process only when edges are genuinely missing.
        if (geometry->get_mesh().edges.nb() == 0) {
            geometry->process({.flags =
                erhe::geometry::Geometry::process_flag_connect                       |
                erhe::geometry::Geometry::process_flag_build_edges                   |
                erhe::geometry::Geometry::process_flag_compute_facet_centroids       |
                erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
                erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates
            });
        }

        std::shared_ptr<erhe::primitive::Material> material{};
        if (entry.contains("material") && entry["material"].is_number_unsigned()) {
            const std::size_t material_index = entry["material"].get<std::size_t>();
            if (material_index < gltf_data.materials.size()) {
                material = gltf_data.materials[material_index];
            } else {
                log_parsers->warn("glTF editor state: brush '{}' references material index {} out of range - reference dropped", name, material_index);
            }
        }

        const Brush_data create_info{
            .context      = context,
            .app_settings = *context.app_settings,
            .name         = name,
            .build_info   = brush_build_info,
            .normal_style = normal_style_from_name(entry.value("normal_style", std::string{"corner_normals"})),
            .geometry     = geometry,
            .density      = entry.value("density", 1.0f),
        };
        const std::shared_ptr<Brush> brush = std::make_shared<Brush>(create_info);
        if (material) {
            brush->set_material(material);
        }
        const std::shared_ptr<Content_library_node> folder = resolve_library_folder(content_library->brushes, entry.value("folder_path", std::string{}));
        operations.push_back(
            std::make_shared<Content_library_attach_operation<Brush>>(
                content_library,
                folder,
                brush,
                Gltf_source_reference{
                    .gltf_path  = gltf_path_str,
                    .item_name  = name,
                    .item_index = brush_index - 1,
                    .item_type  = "brush",
                }
            )
        );
    }
}

void import_node_graphs(
    App_context&                             context,
    const erhe::gltf::Gltf_data&             gltf_data,
    const std::shared_ptr<Content_library>&  content_library,
    const std::string&                       gltf_path_str,
    std::vector<std::shared_ptr<Operation>>& operations
)
{
    const std::string* extension_json = find_extension(gltf_data.asset_extensions, "ERHE_node_graphs");
    if (extension_json == nullptr) {
        return;
    }
    if (!content_library) {
        return;
    }
    const nlohmann::json payload = parse_extension_object(*extension_json, "ERHE_node_graphs", "asset");
    if (payload.is_null()) {
        return;
    }

    // Graph assets: created directly, added through undoable attach
    // operations. Graphs load born-dirty; the first background evaluation
    // re-bakes and pushes to the re-attached bindings.
    std::vector<std::shared_ptr<Graph_texture>> graph_textures;
    const auto graph_textures_it = payload.find("graph_textures");
    if ((graph_textures_it != payload.end()) && graph_textures_it->is_array() && content_library->graph_textures) {
        int index = 0;
        for (const nlohmann::json& entry : *graph_textures_it) {
            if (!entry.is_object() || !entry.contains("graph") || !entry["graph"].is_object()) {
                continue;
            }
            const std::string name = entry.value("name", fmt::format("Graph Texture {}", index));
            const std::shared_ptr<Graph_texture> graph_texture = std::make_shared<Graph_texture>(name);
            if (!read_graph_texture_graph(*graph_texture, entry["graph"], context)) {
                log_parsers->warn("glTF editor state: Graph Texture '{}' graph failed to load", name);
            }
            graph_textures.push_back(graph_texture);
            operations.push_back(
                std::make_shared<Content_library_attach_operation<Graph_texture>>(
                    content_library,
                    content_library->graph_textures,
                    graph_texture,
                    Gltf_source_reference{
                        .gltf_path  = gltf_path_str,
                        .item_name  = name,
                        .item_index = index,
                        .item_type  = "graph_texture",
                    }
                )
            );
            ++index;
        }
    }

    std::vector<std::shared_ptr<Graph_mesh>> graph_meshes;
    const auto graph_meshes_it = payload.find("graph_meshes");
    if ((graph_meshes_it != payload.end()) && graph_meshes_it->is_array() && content_library->graph_meshes) {
        int index = 0;
        for (const nlohmann::json& entry : *graph_meshes_it) {
            if (!entry.is_object() || !entry.contains("graph") || !entry["graph"].is_object()) {
                continue;
            }
            const std::string name = entry.value("name", fmt::format("Graph Mesh {}", index));
            const std::shared_ptr<Graph_mesh> graph_mesh = std::make_shared<Graph_mesh>(name);
            if (!read_graph_mesh_graph(*graph_mesh, entry["graph"], context)) {
                log_parsers->warn("glTF editor state: Graph Mesh '{}' graph failed to load", name);
            }
            graph_meshes.push_back(graph_mesh);
            operations.push_back(
                std::make_shared<Content_library_attach_operation<Graph_mesh>>(
                    content_library,
                    content_library->graph_meshes,
                    graph_mesh,
                    Gltf_source_reference{
                        .gltf_path  = gltf_path_str,
                        .item_name  = name,
                        .item_index = index,
                        .item_type  = "graph_mesh",
                    }
                )
            );
            ++index;
        }
    }

    // Material slot bindings: mutate the freshly parsed materials directly
    // (they enter / leave the library with their own attach operations).
    const auto material_bindings_it = payload.find("material_bindings");
    if ((material_bindings_it != payload.end()) && material_bindings_it->is_array()) {
        for (const nlohmann::json& binding : *material_bindings_it) {
            if (!binding.is_object() || !binding.contains("material") || !binding["material"].is_number_unsigned()) {
                continue;
            }
            const std::size_t material_index     = binding["material"].get<std::size_t>();
            const std::string slot               = binding.value("slot", std::string{});
            const std::string graph_texture_name = binding.value("graph_texture", std::string{});
            std::shared_ptr<erhe::primitive::Material> material{};
            if (material_index < gltf_data.materials.size()) {
                material = gltf_data.materials[material_index];
            }
            std::shared_ptr<Graph_texture> graph_texture{};
            for (const std::shared_ptr<Graph_texture>& candidate : graph_textures) {
                if (candidate->get_name() == graph_texture_name) {
                    graph_texture = candidate;
                    break;
                }
            }
            if (!material || !graph_texture) {
                log_parsers->warn("glTF editor state: texture source binding {}.{} -> '{}' not resolved", material_index, slot, graph_texture_name);
                continue;
            }
            erhe::primitive::Material_texture_samplers& samplers = material->data.texture_samplers;
            erhe::primitive::Material_texture_sampler*  sampler  = nullptr;
            if      (slot == "base_color")         sampler = &samplers.base_color;
            else if (slot == "metallic_roughness") sampler = &samplers.metallic_roughness;
            else if (slot == "normal")             sampler = &samplers.normal;
            else if (slot == "occlusion")          sampler = &samplers.occlusion;
            else if (slot == "emissive")           sampler = &samplers.emissive;
            if (sampler != nullptr) {
                sampler->texture_reference = graph_texture;
            } else {
                log_parsers->warn("glTF editor state: texture source binding has unknown slot '{}'", slot);
            }
        }
    }

    // Node bindings: Geometry_graph_mesh attachments (attached directly,
    // entering the scene with the node insert operation). Scene load
    // attaches without applying - loaded graphs are born dirty, the first
    // evaluation pushes the baked products.
    const auto node_bindings_it = payload.find("node_bindings");
    if ((node_bindings_it != payload.end()) && node_bindings_it->is_array()) {
        for (const nlohmann::json& binding : *node_bindings_it) {
            if (!binding.is_object() || !binding.contains("node") || !binding["node"].is_number_unsigned()) {
                continue;
            }
            const std::size_t node_index      = binding["node"].get<std::size_t>();
            const std::string graph_mesh_name = binding.value("graph_mesh", std::string{});
            std::shared_ptr<Graph_mesh> graph_mesh{};
            for (const std::shared_ptr<Graph_mesh>& candidate : graph_meshes) {
                if (candidate->get_name() == graph_mesh_name) {
                    graph_mesh = candidate;
                    break;
                }
            }
            if ((node_index >= gltf_data.nodes.size()) || !gltf_data.nodes[node_index] || !graph_mesh) {
                log_parsers->warn("glTF editor state: graph mesh binding node {} -> '{}' not resolved", node_index, graph_mesh_name);
                continue;
            }
            const std::shared_ptr<Geometry_graph_mesh> attachment = std::make_shared<Geometry_graph_mesh>(graph_mesh);
            gltf_data.nodes[node_index]->attach(attachment);
        }
    }
}

} // anonymous namespace

void import_gltf_editor_state(
    App_context&                             context,
    const erhe::gltf::Gltf_data&             gltf_data,
    const std::shared_ptr<Scene_root>&       scene_root,
    const std::filesystem::path&             path,
    std::vector<std::shared_ptr<Operation>>& operations
)
{
    const std::shared_ptr<Content_library> content_library = scene_root->get_content_library();
    const std::string gltf_path_str = path.generic_string();

    import_layouts(gltf_data);
    import_collections(gltf_data);
    import_brushes(context, gltf_data, content_library, gltf_path_str, operations);
    import_node_graphs(context, gltf_data, content_library, gltf_path_str, operations);
}

}
