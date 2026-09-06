#include "parsers/gltf_extensions_import.hpp"

#include "parsers/gltf_extensions_names.hpp"

#include "app_context.hpp"
#include "brushes/brush.hpp"
#include "content_library/content_library.hpp"
#include "content_library/style.hpp"
#include "editor_log.hpp"
#include "geometry_graph/geometry_graph_mesh.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "geometry_graph/graph_mesh_serialization.hpp"
#include "operations/library_attach_operation.hpp"
#include "operations/operation.hpp"
#include "scene/item_lookup.hpp"
#include "scene/scene_root.hpp"
#include "texture_graph/graph_texture.hpp"
#include "texture_graph/graph_texture_serialization.hpp"

#include "scene/generated/gltf_source_reference.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_gltf/gltf.hpp"
#include "erhe_gltf/gltf_item_flags.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/layout.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <mutex>

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
// persistent flags) and the "properties" local values (D23); leaves the
// item's flags untouched when the payload carries no flags array (older /
// foreign files keep their default set). Flags without a properties object
// is an older file: visible / shadow_cast / lightmapped come from its flags.
void apply_flags(erhe::Item_base& item, const nlohmann::json& payload)
{
    const auto it = payload.find("flags");
    const bool has_flags = (it != payload.end()) && it->is_array();
    uint64_t listed_bits = 0;
    if (has_flags) {
        for (const nlohmann::json& flag_name : *it) {
            if (flag_name.is_string()) {
                listed_bits |= erhe::gltf::persistent_item_flag_from_name(flag_name.get<std::string>());
            }
        }
        erhe::gltf::apply_persistent_item_flags(item, listed_bits);
    }
    const auto properties_it = payload.find("properties");
    if ((properties_it != payload.end()) && properties_it->is_object()) {
        for (const auto& [name, value] : properties_it->items()) {
            if (value.is_string()) {
                static_cast<void>(erhe::gltf::apply_item_local_property(item, name, value.get<std::string>()));
            }
        }
    } else if (has_flags) {
        erhe::gltf::apply_legacy_derived_item_flags(item, listed_bits);
    }
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
        const auto properties_it = payload.find("properties");
        if ((properties_it != payload.end()) && properties_it->is_object()) {
            entry.has_properties = true;
            for (const auto& [name, value] : properties_it->items()) {
                if (value.is_string()) {
                    entry.properties.emplace_back(name, value.get<std::string>());
                }
            }
        }
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

auto parse_gltf_physics_item_names(const erhe::gltf::Gltf_data& gltf_data) -> Gltf_physics_item_names
{
    Gltf_physics_item_names names{};
    const std::string* extension_json = find_extension(gltf_data.scene_extensions, "ERHE_scene");
    if (extension_json == nullptr) {
        return names;
    }
    const nlohmann::json payload = parse_extension_object(*extension_json, "ERHE_scene", "scene");
    if (!payload.is_object()) {
        return names;
    }
    const auto read_names = [&payload](const char* key, std::vector<std::string>& out) {
        const auto it = payload.find(key);
        if ((it == payload.end()) || !it->is_array()) {
            return;
        }
        for (const nlohmann::json& entry : *it) {
            out.push_back(entry.is_string() ? entry.get<std::string>() : std::string{});
        }
    };
    const auto materials_it = payload.find("physics_materials");
    if ((materials_it != payload.end()) && materials_it->is_array()) {
        for (const nlohmann::json& entry : *materials_it) {
            Gltf_physics_material_record record{};
            if (entry.is_object()) {
                record.name = entry.value("name", std::string{});
                const auto properties_it = entry.find("properties");
                if ((properties_it != entry.end()) && properties_it->is_object()) {
                    record.has_properties = true;
                    for (const auto& [property_name, value] : properties_it->items()) {
                        if (value.is_string()) {
                            record.properties.emplace_back(property_name, value.get<std::string>());
                        }
                    }
                }
            }
            names.physics_materials.push_back(std::move(record));
        }
    }
    read_names("collision_filter_names", names.collision_filters);
    return names;
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
            layout->set_layout_type     (layout_type_from_name(lj.value("type", std::string{"stack"})));
            layout->set_volume_min      (to_vec3(lj.value("volume_min", nlohmann::json{}), layout->get_volume().min));
            layout->set_volume_max      (to_vec3(lj.value("volume_max", nlohmann::json{}), layout->get_volume().max));
            layout->set_primary         (axis_direction_from_name(lj.value("primary",   std::string{"pos_x"})));
            layout->set_secondary       (axis_direction_from_name(lj.value("secondary", std::string{"pos_y"})));
            layout->set_tertiary        (axis_direction_from_name(lj.value("tertiary",  std::string{"pos_z"})));
            layout->set_gap             (to_vec3(lj.value("gap", nlohmann::json{}), layout->get_gap()));
            layout->set_grid_track_count(to_ivec3(lj.value("grid_track_count", nlohmann::json{}), layout->get_grid_track_count()));
            const char* extent_keys[3] = {"grid_track_extent_x", "grid_track_extent_y", "grid_track_extent_z"};
            for (int axis = 0; axis < 3; ++axis) {
                const auto extent_it = lj.find(extent_keys[axis]);
                if ((extent_it != lj.end()) && extent_it->is_array()) {
                    for (const nlohmann::json& extent : *extent_it) {
                        if (extent.is_number()) {
                            layout->get_grid_track_extent(axis).push_back(extent.get<float>());
                        }
                    }
                }
            }
            layout->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui | erhe::Item_flags::show_debug_visualizations);
            apply_flags(*layout, lj);
            // The explicit fields above wrote local values; the properties
            // map is the layout's complete local set (the ERHE_light rule),
            // so a field it does not name is cleared again and a value
            // held by the node above inherits after the reload.
            const auto properties_it = lj.find("properties");
            if ((properties_it != lj.end()) && properties_it->is_object()) {
                erhe::gltf::clear_local_properties_not_listed(
                    *layout,
                    [properties_it](const std::string_view property_name) -> bool {
                        return properties_it->contains(std::string{property_name});
                    }
                );
            }
            node->attach(layout);
        }
        // Legacy "layout_item" sub-object (files written before the hints
        // became attached properties): the values land on the node as the
        // Layout.* attached properties; the name and flags of the former
        // attachment are dropped.
        const auto item_it = payload.find("layout_item");
        if ((item_it != payload.end()) && item_it->is_object()) {
            const nlohmann::json& ij = *item_it;
            const auto align_it = ij.find("align");
            if ((align_it != ij.end()) && align_it->is_array() && (align_it->size() >= 3)) {
                const erhe::property::Property<erhe::scene::Layout_alignment>* align_properties[3] = {
                    &erhe::scene::Layout::align_x_property, &erhe::scene::Layout::align_y_property, &erhe::scene::Layout::align_z_property
                };
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    if ((*align_it)[axis].is_string()) {
                        node->set_value(*align_properties[axis], layout_alignment_from_name((*align_it)[axis].get<std::string>()));
                    }
                }
            }
            node->set_value(erhe::scene::Layout::margin_min_property,     to_vec3 (ij.value("margin_min", nlohmann::json{}), node->get_value(erhe::scene::Layout::margin_min_property)));
            node->set_value(erhe::scene::Layout::margin_max_property,     to_vec3 (ij.value("margin_max", nlohmann::json{}), node->get_value(erhe::scene::Layout::margin_max_property)));
            node->set_value(erhe::scene::Layout::grid_cell_auto_property, ij.value("grid_cell_auto", node->get_value(erhe::scene::Layout::grid_cell_auto_property)));
            node->set_value(erhe::scene::Layout::grid_cell_property,      to_ivec3(ij.value("grid_cell", nlohmann::json{}), node->get_value(erhe::scene::Layout::grid_cell_property)));
            node->set_value(erhe::scene::Layout::grid_span_property,      to_ivec3(ij.value("grid_span", nlohmann::json{}), node->get_value(erhe::scene::Layout::grid_span_property)));
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

// Resolve (creating as needed) the folder scope for a slash-separated path
// relative to the given scope, so the saved folder hierarchy is reconstructed
// instead of flattened (doc/content-library-folders.md D2: a folder is a
// Scope).
[[nodiscard]] auto resolve_library_folder(
    const std::shared_ptr<erhe::Scope>& root,
    const std::string&                  folder_path
) -> std::shared_ptr<erhe::Scope>
{
    std::shared_ptr<erhe::Scope> current = root;
    std::size_t start = 0;
    while (current && (start < folder_path.size())) {
        const std::size_t slash = folder_path.find('/', start);
        const std::string name  = (slash == std::string::npos)
            ? folder_path.substr(start)
            : folder_path.substr(start, slash - start);
        start = (slash == std::string::npos) ? folder_path.size() : slash + 1;
        if (name.empty()) {
            continue;
        }
        std::shared_ptr<erhe::Scope> found{};
        for (const std::shared_ptr<erhe::Hierarchy>& child : current->get_children()) {
            const std::shared_ptr<erhe::Scope> child_scope = std::dynamic_pointer_cast<erhe::Scope>(child);
            if (child_scope && (child_scope->get_name() == name)) {
                found = child_scope;
                break;
            }
        }
        if (!found) {
            found = std::make_shared<erhe::Scope>(name);
            found->enable_flag_bits(erhe::Item_flags::show_in_ui);
            found->set_parent(current);
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
    if (!content_library) {
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
        // folder_path is read from older files only; the writer places
        // brushes through ERHE_scene library_folders (D5).
        const std::string folder_path = entry.value("folder_path", std::string{});
        const std::shared_ptr<erhe::Scope> folder = folder_path.empty()
            ? std::shared_ptr<erhe::Scope>{}
            : resolve_library_folder(content_library->get_scope(erhe::Item_type::brush), folder_path);
        operations.push_back(
            make_library_attach_operation(
                context,
                content_library,
                brush,
                Gltf_source_reference{
                    .gltf_path  = gltf_path_str,
                    .item_name  = name,
                    .item_index = brush_index - 1,
                    .item_type  = "brush",
                },
                std::shared_ptr<erhe::gltf::Gltf_image_source>{},
                std::optional<Asset_key>{},
                folder
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
    if ((graph_textures_it != payload.end()) && graph_textures_it->is_array()) {
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
                make_library_attach_operation(
                    context,
                    content_library,
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
    if ((graph_meshes_it != payload.end()) && graph_meshes_it->is_array()) {
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
                make_library_attach_operation(
                    context,
                    content_library,
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
                material->set_slot_texture(*sampler, graph_texture);
                // Sampler state (wrap / filters): graph-texture slots export
                // no glTF texture, so this is the only carrier - without it
                // a wrap=repeat slot reloads as the clamp fallback.
                if (binding.contains("wrap")) {
                    const auto parse_address_mode = [](const nlohmann::json& value) -> erhe::graphics::Sampler_address_mode {
                        const std::string s = value.is_string() ? value.get<std::string>() : std::string{};
                        if (s == "repeat")          return erhe::graphics::Sampler_address_mode::repeat;
                        if (s == "mirrored_repeat") return erhe::graphics::Sampler_address_mode::mirrored_repeat;
                        return erhe::graphics::Sampler_address_mode::clamp_to_edge;
                    };
                    const auto parse_filter = [](const nlohmann::json& parent, const char* key) -> erhe::graphics::Filter {
                        return (parent.value(key, std::string{"linear"}) == "nearest")
                            ? erhe::graphics::Filter::nearest
                            : erhe::graphics::Filter::linear;
                    };
                    erhe::primitive::Material_sampler_state state{
                        .min_filter  = parse_filter(binding, "min_filter"),
                        .mag_filter  = parse_filter(binding, "mag_filter"),
                        .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::not_mipmapped // a graph bake has one level
                    };
                    const nlohmann::json& wrap = binding["wrap"];
                    if (wrap.is_array() && (wrap.size() == 2)) {
                        state.wrap_u = parse_address_mode(wrap[0]);
                        state.wrap_v = parse_address_mode(wrap[1]);
                    }
                    material->set_slot_sampler(*sampler, state);
                }
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

// One saved library folder (ERHE_scene library_folders,
// doc/content-library-folders.md D5).
class Library_folder_record
{
public:
    std::string                                      path;       // category-rooted, "Materials/Metals"
    std::vector<std::pair<std::string, std::string>> properties; // local values, name -> text
    std::vector<std::string>                         items;      // names of the entries directly in the folder
    std::string                                      style;      // the style item the folder uses, by name (empty: none)
};

// The style item of that name in the library's Styles folder, or null.
[[nodiscard]] auto find_style_by_name(const Content_library& content_library, const std::string& name) -> std::shared_ptr<Style>
{
    for (const std::shared_ptr<Style>& style : content_library.get_all<Style>()) {
        if (style && (style->get_name() == name)) {
            return style;
        }
    }
    return {};
}

// Recreates the saved folder scopes, applies their local property values and
// places the named resources under them (D6). Runs after every attach
// operation of the same import, so the resources exist. Properties apply only
// to scopes this operation creates: an existing scope of the same path (an
// import into a scene that already has it) keeps its values. Undo moves the
// resources back and removes the created scopes.
class Content_library_folders_operation : public Operation
{
public:
    Content_library_folders_operation(std::shared_ptr<Content_library> content_library, std::vector<Library_folder_record> folders)
        : m_content_library{std::move(content_library)}
        , m_folders        {std::move(folders)}
    {
        set_description(fmt::format("[{}] Content_library_folders ({} folders)", get_serial(), m_folders.size()));
    }

    void execute(App_context&) override
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_content_library->mutex};
        m_created_folders.clear();
        m_moves.clear();
        for (const Library_folder_record& record : m_folders) {
            std::shared_ptr<erhe::Scope> kind_scope{};
            std::shared_ptr<erhe::Scope> folder{};
            bool created = false;
            resolve_folder(record.path, kind_scope, folder, created);
            if (!folder) {
                log_parsers->warn("glTF editor state: library folder '{}' names no resource kind - folder dropped", record.path);
                continue;
            }
            if (created) {
                for (const std::pair<std::string, std::string>& property : record.properties) {
                    erhe::gltf::apply_item_local_property(*folder, property.first, property.second);
                }
                if (!record.style.empty()) {
                    const std::shared_ptr<Style> style = find_style_by_name(*m_content_library, record.style);
                    if (style) {
                        folder->set_style(style);
                    } else {
                        log_parsers->warn("glTF editor state: library folder '{}' names style '{}', which the scene does not hold", record.path, record.style);
                    }
                }
            } else if (!record.properties.empty()) {
                log_parsers->info("glTF editor state: library folder '{}' exists - its saved property values are not applied", record.path);
            }
            for (const std::string& item_name : record.items) {
                place_item(*kind_scope, folder, record.path, item_name);
            }
        }
    }

    void undo(App_context&) override
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_content_library->mutex};
        for (auto it = m_moves.rbegin(); it != m_moves.rend(); ++it) {
            it->node->set_parent(it->before_parent, it->before_index);
        }
        m_moves.clear();
        for (auto it = m_created_folders.rbegin(); it != m_created_folders.rend(); ++it) {
            (*it)->set_parent(std::shared_ptr<erhe::Hierarchy>{});
        }
        m_created_folders.clear();
    }

private:
    class Move
    {
    public:
        std::shared_ptr<erhe::Hierarchy> node;
        std::shared_ptr<erhe::Hierarchy> before_parent;
        std::size_t                      before_index;
    };

    // Walks the kind-scope-rooted path; the first component names the kind
    // scope (never created), every missing scope below it is created and
    // recorded for undo. out_created is true when the last component was
    // created by this call.
    void resolve_folder(
        const std::string&            path,
        std::shared_ptr<erhe::Scope>& out_kind_scope,
        std::shared_ptr<erhe::Scope>& out_folder,
        bool&                         out_created
    )
    {
        out_kind_scope.reset();
        out_folder.reset();
        out_created = false;
        std::shared_ptr<erhe::Scope> current{};
        std::size_t start = 0;
        bool first = true;
        while (start < path.size()) {
            const std::size_t slash = path.find('/', start);
            const std::string name  = (slash == std::string::npos) ? path.substr(start) : path.substr(start, slash - start);
            start = (slash == std::string::npos) ? path.size() : slash + 1;
            if (name.empty()) {
                continue;
            }
            std::shared_ptr<erhe::Scope> found{};
            if (first) {
                for (const uint64_t kind_type_bit : Content_library::get_kind_type_bits()) {
                    if (Content_library::get_kind_scope_name(kind_type_bit) == name) {
                        found = m_content_library->get_scope(kind_type_bit);
                        break;
                    }
                }
                if (!found) {
                    return; // no such kind
                }
                out_kind_scope = found;
                out_created    = false;
            } else {
                for (const std::shared_ptr<erhe::Hierarchy>& child : current->get_children()) {
                    const std::shared_ptr<erhe::Scope> child_scope = std::dynamic_pointer_cast<erhe::Scope>(child);
                    if (child_scope && (child_scope->get_name() == name)) {
                        found = child_scope;
                        break;
                    }
                }
                out_created = false;
                if (!found) {
                    found = std::make_shared<erhe::Scope>(name);
                    found->enable_flag_bits(erhe::Item_flags::show_in_ui);
                    found->set_parent(current);
                    m_created_folders.push_back(found);
                    out_created = true;
                }
            }
            first   = false;
            current = found;
        }
        if (out_kind_scope && (current != out_kind_scope)) {
            out_folder = current;
        }
    }

    void place_item(
        const erhe::Scope&                  kind_scope,
        const std::shared_ptr<erhe::Scope>& folder,
        const std::string&                  folder_path,
        const std::string&                  item_name
    )
    {
        std::shared_ptr<erhe::Hierarchy> found{};
        std::size_t                      match_count = 0;
        const_cast<erhe::Scope&>(kind_scope).for_each<erhe::Hierarchy>(
            [&found, &match_count, &item_name](erhe::Hierarchy& prim) -> bool {
                if ((dynamic_cast<erhe::Scope*>(&prim) == nullptr) && (prim.get_name() == item_name)) {
                    if (!found) {
                        found = prim.shared_hierarchy_from_this();
                    }
                    ++match_count;
                }
                return true;
            }
        );
        if (!found) {
            log_parsers->warn("glTF editor state: library folder '{}' lists '{}', which the resource kind does not hold - not placed", folder_path, item_name);
            return;
        }
        if (match_count > 1) {
            log_parsers->warn("glTF editor state: library folder '{}' lists '{}', which matches {} resources - the first is placed", folder_path, item_name, match_count);
        }
        const std::shared_ptr<erhe::Hierarchy> before_parent = found->get_parent().lock();
        if (before_parent == folder) {
            return;
        }
        m_moves.push_back(Move{.node = found, .before_parent = before_parent, .before_index = found->get_index_in_parent()});
        found->set_parent(folder);
    }

    std::shared_ptr<Content_library>          m_content_library;
    std::vector<Library_folder_record>        m_folders;
    std::vector<std::shared_ptr<erhe::Scope>> m_created_folders;
    std::vector<Move>                         m_moves;
};

void import_library_folders(
    const erhe::gltf::Gltf_data&             gltf_data,
    const std::shared_ptr<Content_library>&  content_library,
    std::vector<std::shared_ptr<Operation>>& operations
)
{
    const std::string* extension_json = find_extension(gltf_data.scene_extensions, "ERHE_scene");
    if ((extension_json == nullptr) || !content_library) {
        return;
    }
    const nlohmann::json payload = parse_extension_object(*extension_json, "ERHE_scene", "scene");
    if (payload.is_null()) {
        return;
    }
    const auto folders_it = payload.find("library_folders");
    if ((folders_it == payload.end()) || !folders_it->is_array()) {
        return;
    }
    std::vector<Library_folder_record> records;
    for (const nlohmann::json& entry : *folders_it) {
        if (!entry.is_object() || !entry.contains("path") || !entry["path"].is_string()) {
            log_parsers->warn("glTF editor state: library_folders entry without a path - skipped");
            continue;
        }
        Library_folder_record record{.path = entry["path"].get<std::string>()};
        const auto properties_it = entry.find("properties");
        if ((properties_it != entry.end()) && properties_it->is_object()) {
            for (const auto& [name, value] : properties_it->items()) {
                if (value.is_string()) {
                    record.properties.emplace_back(name, value.get<std::string>());
                }
            }
        }
        const auto items_it = entry.find("items");
        if ((items_it != entry.end()) && items_it->is_array()) {
            for (const nlohmann::json& item : *items_it) {
                if (item.is_string()) {
                    record.items.push_back(item.get<std::string>());
                }
            }
        }
        record.style = entry.value("style", std::string{});
        records.push_back(std::move(record));
    }
    if (records.empty()) {
        return;
    }
    operations.push_back(std::make_shared<Content_library_folders_operation>(content_library, std::move(records)));
}

// ERHE_scene styles (doc/style-library.md D4): one attach operation per
// style item, run before anything that names a style.
void import_styles(
    App_context&                             context,
    const erhe::gltf::Gltf_data&             gltf_data,
    const std::shared_ptr<Content_library>&  content_library,
    const std::string&                       gltf_path_str,
    std::vector<std::shared_ptr<Operation>>& operations
)
{
    const std::string* extension_json = find_extension(gltf_data.scene_extensions, "ERHE_scene");
    if ((extension_json == nullptr) || !content_library) {
        return;
    }
    const nlohmann::json payload = parse_extension_object(*extension_json, "ERHE_scene", "scene");
    if (payload.is_null()) {
        return;
    }
    const auto styles_it = payload.find("styles");
    if ((styles_it == payload.end()) || !styles_it->is_array()) {
        return;
    }
    std::size_t style_index = 0;
    for (const nlohmann::json& entry : *styles_it) {
        ++style_index;
        if (!entry.is_object() || !entry.contains("name") || !entry["name"].is_string()) {
            log_parsers->warn("glTF editor state: styles entry {} without name - skipped", style_index);
            continue;
        }
        const std::string name = entry["name"].get<std::string>();
        std::shared_ptr<Style> style = std::make_shared<Style>(name);
        const auto properties_it = entry.find("properties");
        if ((properties_it != entry.end()) && properties_it->is_object()) {
            for (const auto& [property_name, value] : properties_it->items()) {
                if (value.is_string()) {
                    erhe::gltf::apply_item_local_property(*style, property_name, value.get<std::string>());
                }
            }
        }
        operations.push_back(
            make_library_attach_operation(
                context,
                content_library,
                style,
                Gltf_source_reference{
                    .gltf_path  = gltf_path_str,
                    .item_name  = name,
                    .item_index = static_cast<int>(style_index - 1),
                    .item_type  = "style",
                }
            )
        );
    }
}

// Assigns a material's ERHE_material style by name at execute time, when
// the style items of the same import exist (doc/style-library.md D4).
// Assigns the style item of the named style to an item (a material from
// ERHE_material.style, a node from ERHE_node.style) once the styles exist.
class Item_style_by_name_operation : public Operation
{
public:
    Item_style_by_name_operation(std::shared_ptr<Content_library> content_library, std::shared_ptr<erhe::Item_base> item, std::string style_name)
        : m_content_library{std::move(content_library)}
        , m_item           {std::move(item)}
        , m_style_name     {std::move(style_name)}
    {
        set_description(fmt::format("[{}] {} '{}' style '{}'", get_serial(), m_item->get_type_name(), m_item->get_name(), m_style_name));
    }

    void execute(App_context&) override
    {
        m_before = m_item->get_style();
        const std::shared_ptr<Style> style = find_style_by_name(*m_content_library, m_style_name);
        if (!style) {
            log_parsers->warn("glTF editor state: {} '{}' names style '{}', which the scene does not hold", m_item->get_type_name(), m_item->get_name(), m_style_name);
            return;
        }
        if (!m_item->set_style(style)) {
            log_parsers->warn("glTF editor state: {} '{}' cannot use style '{}'", m_item->get_type_name(), m_item->get_name(), m_style_name);
        }
    }

    void undo(App_context&) override
    {
        m_item->set_style(m_before);
    }

private:
    std::shared_ptr<Content_library>                         m_content_library;
    std::shared_ptr<erhe::Item_base>                         m_item;
    std::string                                              m_style_name;
    std::shared_ptr<const erhe::property::Dependency_object> m_before;
};

// An object-reference local value of a "properties" map that named an
// item the parse could not resolve (Gltf_data::unresolved_object_properties):
// resolved in the scene at execute time, when the items the import's
// earlier operations create (physics materials, filters, styles) exist
// and the node has its host; the local value set is undoable.
class Item_object_property_by_name_operation : public Operation
{
public:
    Item_object_property_by_name_operation(std::shared_ptr<Scene_root> scene_root, erhe::gltf::Unresolved_object_property entry)
        : m_scene_root{std::move(scene_root)}
        , m_entry     {std::move(entry)}
    {
        set_description(fmt::format("[{}] {} '{}' {} = '{}'", get_serial(), m_entry.item->get_type_name(), m_entry.item->get_name(), m_entry.property_name, m_entry.text));
    }

    void execute(App_context&) override
    {
        const erhe::property::Dependency_property* property = erhe::property::Property_registry::get().find_for_object(*m_entry.item, m_entry.property_name);
        if (property == nullptr) {
            return;
        }
        m_property = property;
        m_before   = m_entry.item->read_local_state(*property);
        const std::shared_ptr<erhe::Item_base> referenced = find_item_in_scene_by_reference(*m_scene_root, m_entry.text);
        if (!referenced) {
            log_parsers->warn("glTF: {} '{}' property '{}' names '{}', which the scene does not hold", m_entry.item->get_type_name(), m_entry.item->get_name(), m_entry.property_name, m_entry.text);
            return;
        }
        if (!m_entry.item->set_value(*property, erhe::property::Object_reference{referenced})) {
            log_parsers->warn("glTF: {} '{}' property '{}' cannot reference '{}'", m_entry.item->get_type_name(), m_entry.item->get_name(), m_entry.property_name, m_entry.text);
        }
    }

    void undo(App_context&) override
    {
        if (m_property != nullptr) {
            static_cast<void>(m_entry.item->apply_local_state(*m_property, m_before));
        }
    }

private:
    std::shared_ptr<Scene_root>                  m_scene_root;
    erhe::gltf::Unresolved_object_property       m_entry;
    const erhe::property::Dependency_property*   m_property{nullptr};
    std::optional<erhe::property::Local_state>   m_before;
};

void import_unresolved_object_properties(
    const erhe::gltf::Gltf_data&             gltf_data,
    const std::shared_ptr<Scene_root>&       scene_root,
    std::vector<std::shared_ptr<Operation>>& operations
)
{
    for (const erhe::gltf::Unresolved_object_property& entry : gltf_data.unresolved_object_properties) {
        if (entry.item) {
            operations.push_back(std::make_shared<Item_object_property_by_name_operation>(scene_root, entry));
        }
    }
}

void import_material_styles(
    const erhe::gltf::Gltf_data&             gltf_data,
    const std::shared_ptr<Content_library>&  content_library,
    std::vector<std::shared_ptr<Operation>>& operations
)
{
    if (!content_library) {
        return;
    }
    for (std::size_t i = 0; (i < gltf_data.material_style_names.size()) && (i < gltf_data.materials.size()); ++i) {
        const std::string& style_name = gltf_data.material_style_names[i];
        if (style_name.empty() || !gltf_data.materials[i]) {
            continue;
        }
        operations.push_back(std::make_shared<Item_style_by_name_operation>(content_library, gltf_data.materials[i], style_name));
    }
}

// ERHE_node.style (doc/style-library.md D4): the node's style item by name.
void import_node_styles(
    const erhe::gltf::Gltf_data&             gltf_data,
    const std::shared_ptr<Content_library>&  content_library,
    std::vector<std::shared_ptr<Operation>>& operations
)
{
    if (!content_library) {
        return;
    }
    for (std::size_t i = 0, end = gltf_data.node_extensions.size(); i < end; ++i) {
        if ((i >= gltf_data.nodes.size()) || !gltf_data.nodes[i]) {
            continue;
        }
        const std::string* extension_json = find_extension(gltf_data.node_extensions[i], "ERHE_node");
        if (extension_json == nullptr) {
            continue;
        }
        const nlohmann::json payload = parse_extension_object(*extension_json, "ERHE_node", gltf_data.nodes[i]->get_name());
        if (!payload.is_object()) {
            continue;
        }
        const std::string style_name = payload.value("style", std::string{});
        if (style_name.empty()) {
            continue;
        }
        operations.push_back(std::make_shared<Item_style_by_name_operation>(content_library, gltf_data.nodes[i], style_name));
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
    // Styles first: the material and folder assignments below name them.
    import_styles(context, gltf_data, content_library, gltf_path_str, operations);
    import_brushes(context, gltf_data, content_library, gltf_path_str, operations);
    import_node_graphs(context, gltf_data, content_library, gltf_path_str, operations);
    import_material_styles(gltf_data, content_library, operations);
    import_node_styles(gltf_data, content_library, operations);
    // Object references by name (a node-held Node_physics.physics_material):
    // after every operation that creates the items they may name.
    import_unresolved_object_properties(gltf_data, scene_root, operations);
    // Last: the folders operation places entries the operations above attach.
    import_library_folders(gltf_data, content_library, operations);
}

}
