// Mcp_server mesh component tools (component selection, geometry ops, transform modes).
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "app_settings.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/mesh_transform_mode.hpp"
#include "geometry_graph/geometry_graph_node.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/operations_window.hpp"
#include "operations/set_edge_sharpness_operation.hpp"
#include "renderers/id_renderer.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "tools/mesh_component_selection.hpp"
#include "tools/mesh_component_selection_tool.hpp"
#include "tools/selection_tool.hpp"
#include "transform/transform_tool.hpp"
#include "transform/transform_tool_settings.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/lattice_deform.hpp"
#include "erhe_geometry/operation/project_texcoords.hpp"
#include "erhe_item/item.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace editor {

using namespace mcp_server_detail;

auto Mcp_server::action_set_mesh_component_mode(const json& args) -> std::string
{
    if (m_context.mesh_component_selection == nullptr) {
        return make_error_content("Mesh component selection not available");
    }
    const std::string mode_str = args.value("mode", "");
    if (!is_valid_mesh_component_mode(mode_str)) {
        return make_error_content("mode is required (object, vertex, edge, face)");
    }
    m_context.mesh_component_selection->set_mode(parse_mesh_component_mode(mode_str, Mesh_component_mode::object));
    return make_json_content({{"mode", mode_str}}).dump();
}

auto Mcp_server::action_select_mesh_components(const json& args) -> std::string
{
    Mesh_component_selection* selection = m_context.mesh_component_selection;
    if (selection == nullptr) {
        return make_error_content("Mesh component selection not available");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*sr, args, "node_id", "node_name");
    if (!node) {
        return make_error_content("Node not found (give node_id or node_name)");
    }
    const std::size_t primitive_index = args.value("primitive_index", std::size_t{0});
    std::shared_ptr<erhe::scene::Mesh>        mesh;
    std::shared_ptr<erhe::geometry::Geometry> geometry;
    if (!resolve_mesh_geometry(node, primitive_index, mesh, geometry)) {
        return make_error_content("Node has no mesh geometry at primitive_index " + std::to_string(primitive_index) + ": " + node->get_name());
    }

    // Only scene content is component-selectable; tool / brush / controller /
    // rendertarget / id meshes are not. This mirrors the interactive pick path
    // (Mesh_component_selection_tool::pick / apply_scan_hits_to_selection) so the
    // selection store only ever holds content meshes.
    if ((mesh->get_flag_bits() & erhe::Item_flags::content) == 0) {
        return make_error_content("Mesh is not scene content (Item_flags::content not set); not component-selectable: " + node->get_name());
    }

    if (args.contains("mode")) {
        const std::string mode_str = args.value("mode", "");
        if (!is_valid_mesh_component_mode(mode_str)) {
            return make_error_content("Invalid mode: " + mode_str + " (object, vertex, edge, face)");
        }
        selection->set_mode(parse_mesh_component_mode(mode_str, Mesh_component_mode::object));
    }

    const bool extend = args.value("extend", false);
    if (!extend) {
        selection->clear_all();
    }

    const GEO::Mesh&   geo_mesh     = geometry->get_mesh();
    const GEO::index_t vertex_count = geo_mesh.vertices.nb();
    const GEO::index_t facet_count  = geo_mesh.facets.nb();

    Mesh_component_entry& entry = selection->find_or_create_entry(mesh, primitive_index, geometry);

    if (args.contains("vertices") && args["vertices"].is_array()) {
        for (const auto& v : args["vertices"]) {
            const GEO::index_t vertex = v.get<GEO::index_t>();
            if (vertex >= vertex_count) {
                return make_error_content("Vertex index out of range: " + std::to_string(vertex) + " >= " + std::to_string(vertex_count));
            }
            entry.add_vertex(vertex);
        }
    }
    if (args.contains("edges") && args["edges"].is_array()) {
        for (const auto& e : args["edges"]) {
            if (!e.is_array() || (e.size() != 2)) {
                return make_error_content("Each edge must be a [v0, v1] vertex-index pair");
            }
            const GEO::index_t v0 = e[0].get<GEO::index_t>();
            const GEO::index_t v1 = e[1].get<GEO::index_t>();
            if ((v0 >= vertex_count) || (v1 >= vertex_count)) {
                return make_error_content("Edge vertex index out of range (vertex_count " + std::to_string(vertex_count) + ")");
            }
            entry.add_edge(v0, v1);
        }
    }
    if (args.contains("facets") && args["facets"].is_array()) {
        for (const auto& f : args["facets"]) {
            const GEO::index_t facet = f.get<GEO::index_t>();
            if (facet >= facet_count) {
                return make_error_content("Facet index out of range: " + std::to_string(facet) + " >= " + std::to_string(facet_count));
            }
            entry.add_facet(facet);
        }
    }

    return make_json_content({
        {"node",            node->get_name()},
        {"node_id",         node->get_id()},
        {"primitive_index", primitive_index},
        {"mode",            mesh_component_mode_lc(selection->get_mode())},
        {"vertices",        entry.vertices.size()},
        {"edges",           entry.edges.size()},
        {"facets",          entry.facets.size()}
    }).dump();
}

auto Mcp_server::query_mesh_component_selection(const json& args) -> std::string
{
    static_cast<void>(args);
    Mesh_component_selection* selection = m_context.mesh_component_selection;
    if (selection == nullptr) {
        return make_json_content({{"mode", "object"}, {"entries", json::array()}}).dump();
    }
    json entries = json::array();
    for (const Mesh_component_entry& entry : selection->get_entries()) {
        json vertices = json::array();
        for (const GEO::index_t v : entry.vertices) {
            vertices.push_back(v);
        }
        json facets = json::array();
        for (const GEO::index_t f : entry.facets) {
            facets.push_back(f);
        }
        json edges = json::array();
        for (const Mesh_edge_key& e : entry.edges) {
            edges.push_back(json::array({e.first, e.second}));
        }
        json entry_json = {
            {"primitive_index", entry.primitive_index},
            {"live",            selection->is_live(entry)},
            {"vertices",        vertices},
            {"edges",           edges},
            {"facets",          facets}
        };
        const std::shared_ptr<erhe::scene::Mesh> mesh = entry.mesh.lock();
        if (mesh) {
            entry_json["mesh_name"] = mesh->get_name();
            const erhe::scene::Node* node = mesh->get_node();
            if (node != nullptr) {
                entry_json["node_name"] = node->get_name();
                entry_json["node_id"]   = node->get_id();
            }
        }
        entries.push_back(entry_json);
    }
    return make_json_content({
        {"mode",    mesh_component_mode_lc(selection->get_mode())},
        {"entries", entries}
    }).dump();
}

auto Mcp_server::action_grow_mesh_selection(const json& args) -> std::string
{
    if (m_context.mesh_component_selection == nullptr) {
        return make_error_content("Mesh component selection not available");
    }
    // Blender Select More. No-op in object mode (see Mesh_component_selection::grow).
    // Returns the resulting selection so the caller can read the grown set back.
    m_context.mesh_component_selection->grow();
    return query_mesh_component_selection(args);
}

auto Mcp_server::action_shrink_mesh_selection(const json& args) -> std::string
{
    if (m_context.mesh_component_selection == nullptr) {
        return make_error_content("Mesh component selection not available");
    }
    // Blender Select Less. No-op in object mode (see Mesh_component_selection::shrink).
    m_context.mesh_component_selection->shrink();
    return query_mesh_component_selection(args);
}

auto Mcp_server::query_id_range_mapping(const json& args) -> std::string
{
    static_cast<void>(args);
    if (m_context.id_renderer == nullptr) {
        return make_error_content("Id_renderer not available");
    }
    // The id-range table is rebuilt every frame and is only meaningful right after
    // an ID render; report the snapshot captured by the most recently resolved
    // region scan. Run a box/paint select (or debug_region_select) first to populate it.
    const std::vector<erhe::scene_renderer::Primitive_buffer::Id_range>& ranges =
        m_context.id_renderer->get_last_scan_id_ranges();
    json range_array = json::array();
    for (const erhe::scene_renderer::Primitive_buffer::Id_range& range : ranges) {
        // A decoded pixel id in [offset, offset+length) selects this primitive;
        // (id - offset) is the GEO facet index directly (the id pass emits the
        // facet id per vertex). length is the index count, so the fill triangle
        // count is length / 3 (facet ids are <= triangle count).
        json entry = {
            {"id_offset",      range.offset},
            {"length",         range.length},
            {"triangle_count", range.length / 3u},
            {"primitive_index", range.index_of_gltf_primitive_in_mesh}
        };
        if (range.mesh != nullptr) {
            entry["mesh_name"] = range.mesh->get_name();
            const erhe::scene::Node* node = range.mesh->get_node();
            if (node != nullptr) {
                entry["node_id"]   = node->get_id();
                entry["node_name"] = node->get_name();
            }
            // The per-primitive base vertex in the shared pool: the ID shader
            // subtracts this from gl_VertexID so the packed triangle id is the
            // 0-based local facet index. Surfaced so the encoding can be verified.
            const std::vector<erhe::scene::Mesh_primitive>& primitives = range.mesh->get_primitives();
            if (range.index_of_gltf_primitive_in_mesh < primitives.size()) {
                const erhe::primitive::Primitive* primitive = primitives[range.index_of_gltf_primitive_in_mesh].primitive.get();
                if (primitive != nullptr) {
                    const erhe::primitive::Buffer_mesh* buffer_mesh = primitive->get_renderable_mesh();
                    if (buffer_mesh != nullptr) {
                        entry["base_vertex"] = buffer_mesh->base_vertex();
                    }
                }
            }
        }
        range_array.push_back(entry);
    }
    return make_json_content({
        {"note",   "Snapshot from the most recently resolved region scan. id = id_offset + local_facet_index; a pixel id in [id_offset, id_offset+length) maps to (mesh, primitive_index) and facet (id - id_offset)."},
        {"count",  range_array.size()},
        {"ranges", range_array}
    }).dump();
}

auto Mcp_server::action_debug_region_select(const json& args) -> std::string
{
    if ((m_context.mesh_component_selection == nullptr) || (m_context.mesh_component_selection_tool == nullptr)) {
        return make_error_content("Mesh component selection not available");
    }
    const int   x            = args.value("x", 0);
    const int   y            = args.value("y", 0);
    const int   width        = args.value("width", 0);
    const int   height       = args.value("height", 0);
    const bool  is_brush     = args.value("is_brush", false);
    const float brush_radius = args.value("brush_radius", 0.0f);
    const bool  replace      = args.value("replace", true);
    const bool  subtract     = args.value("subtract", false);
    if ((width <= 0) || (height <= 0)) {
        return make_error_content("width and height (viewport pixels) are required and must be > 0");
    }
    // Force Face mode so the scan resolves to facets.
    m_context.mesh_component_selection->set_mode(Mesh_component_mode::face);
    m_context.mesh_component_selection_tool->debug_region_select(x, y, width, height, is_brush, brush_radius, replace, subtract);
    return make_json_content({
        {"status", "scan requested; poll get_mesh_component_selection in a few frames"},
        {"x", x}, {"y", y}, {"width", width}, {"height", height},
        {"is_brush", is_brush}, {"replace", replace}, {"subtract", subtract}
    }).dump();
}

auto Mcp_server::action_clear_mesh_component_selection(const json& args) -> std::string
{
    static_cast<void>(args);
    if (m_context.mesh_component_selection == nullptr) {
        return make_error_content("Mesh component selection not available");
    }
    m_context.mesh_component_selection->clear_all();
    return make_json_content({{"cleared", true}}).dump();
}

auto Mcp_server::query_mesh_geometry_info(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*sr, args, "node_id", "node_name");
    if (!node) {
        return make_error_content("Node not found (give node_id or node_name)");
    }
    const std::size_t primitive_index = args.value("primitive_index", std::size_t{0});
    std::shared_ptr<erhe::scene::Mesh>        mesh;
    std::shared_ptr<erhe::geometry::Geometry> geometry;
    if (!resolve_mesh_geometry(node, primitive_index, mesh, geometry)) {
        return make_error_content("Node has no mesh geometry at primitive_index " + std::to_string(primitive_index) + ": " + node->get_name());
    }

    const GEO::Mesh&                 geo_mesh     = geometry->get_mesh();
    erhe::geometry::Mesh_attributes& attributes   = geometry->get_attributes();
    const GEO::index_t               vertex_count = geo_mesh.vertices.nb();
    const GEO::index_t               edge_count   = geo_mesh.edges.nb();
    const GEO::index_t               facet_count  = geo_mesh.facets.nb();
    const GEO::index_t               corner_count = geo_mesh.facet_corners.nb();

    return make_json_content({
        {"node",            node->get_name()},
        {"node_id",         node->get_id()},
        {"primitive_index", primitive_index},
        {"geometry_name",   geometry->get_name()},
        {"counts", {
            {"vertices", vertex_count},
            {"edges",    edge_count},
            {"facets",   facet_count},
            {"corners",  corner_count}
        }},
        {"attributes", {
            {"facet",  attribute_presence_summary(attributes, facet_count,  [](erhe::geometry::Mesh_attributes& a, auto&& f){ for_each_facet_attribute (a, f); })},
            {"vertex", attribute_presence_summary(attributes, vertex_count, [](erhe::geometry::Mesh_attributes& a, auto&& f){ for_each_vertex_attribute(a, f); })},
            {"corner", attribute_presence_summary(attributes, corner_count, [](erhe::geometry::Mesh_attributes& a, auto&& f){ for_each_corner_attribute(a, f); })}
        }}
    }).dump();
}

auto Mcp_server::query_mesh_attribute_values(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*sr, args, "node_id", "node_name");
    if (!node) {
        return make_error_content("Node not found (give node_id or node_name)");
    }
    const std::size_t primitive_index = args.value("primitive_index", std::size_t{0});
    std::shared_ptr<erhe::scene::Mesh>        mesh;
    std::shared_ptr<erhe::geometry::Geometry> geometry;
    if (!resolve_mesh_geometry(node, primitive_index, mesh, geometry)) {
        return make_error_content("Node has no mesh geometry at primitive_index " + std::to_string(primitive_index) + ": " + node->get_name());
    }

    const std::string domain = args.value("domain", "");
    if ((domain != "vertex") && (domain != "corner") && (domain != "facet") && (domain != "edge")) {
        return make_error_content("domain is required (vertex, corner, facet, edge)");
    }
    if (!args.contains("indices") || !args["indices"].is_array()) {
        return make_error_content("indices array is required");
    }
    if (args["indices"].size() > 4096) {
        return make_error_content("too many indices (max 4096 per call)");
    }

    // Optional attribute-name filter; empty means "all attributes in the domain".
    std::set<std::string> filter;
    if (args.contains("attributes") && args["attributes"].is_array()) {
        for (const auto& a : args["attributes"]) {
            filter.insert(a.get<std::string>());
        }
    }
    const auto wanted = [&](const char* name) -> bool {
        return filter.empty() || (filter.count(name) != 0);
    };

    const GEO::Mesh&                 geo_mesh   = geometry->get_mesh();
    erhe::geometry::Mesh_attributes& attributes = geometry->get_attributes();

    json elements = json::array();
    for (const auto& idx_j : args["indices"]) {
        const GEO::index_t idx = idx_j.get<GEO::index_t>();
        json elem;
        elem["index"] = idx;
        json attrs = json::object();
        if (domain == "vertex") {
            if (idx >= geo_mesh.vertices.nb()) {
                return make_error_content("vertex index out of range: " + std::to_string(idx) + " >= " + std::to_string(geo_mesh.vertices.nb()));
            }
            elem["position"] = geo_vec_to_json(erhe::geometry::get_pointf(geo_mesh.vertices, idx));
            for_each_vertex_attribute(attributes, [&](const char* name, auto& ap){ if (wanted(name)) { attrs[name] = attribute_value_json(ap, idx); } });
        } else if (domain == "corner") {
            if (idx >= geo_mesh.facet_corners.nb()) {
                return make_error_content("corner index out of range: " + std::to_string(idx) + " >= " + std::to_string(geo_mesh.facet_corners.nb()));
            }
            elem["vertex"] = geo_mesh.facet_corners.vertex(idx);
            elem["facet"]  = geometry->get_corner_facet(idx);
            for_each_corner_attribute(attributes, [&](const char* name, auto& ap){ if (wanted(name)) { attrs[name] = attribute_value_json(ap, idx); } });
        } else if (domain == "facet") {
            if (idx >= geo_mesh.facets.nb()) {
                return make_error_content("facet index out of range: " + std::to_string(idx) + " >= " + std::to_string(geo_mesh.facets.nb()));
            }
            json corners  = json::array();
            json vertices = json::array();
            for (const GEO::index_t corner : geo_mesh.facets.corners(idx)) {
                corners.push_back(corner);
                vertices.push_back(geo_mesh.facet_corners.vertex(corner));
            }
            elem["corners"]  = corners;
            elem["vertices"] = vertices;
            for_each_facet_attribute(attributes, [&](const char* name, auto& ap){ if (wanted(name)) { attrs[name] = attribute_value_json(ap, idx); } });
        } else { // edge
            if (idx >= geo_mesh.edges.nb()) {
                return make_error_content("edge index out of range: " + std::to_string(idx) + " >= " + std::to_string(geo_mesh.edges.nb()));
            }
            elem["vertices"] = json::array({geo_mesh.edges.vertex(idx, 0), geo_mesh.edges.vertex(idx, 1)});
            json facets = json::array();
            for (const GEO::index_t facet : geometry->get_edge_facets(idx)) {
                facets.push_back(facet);
            }
            elem["facets"] = facets;
            if (wanted(erhe::geometry::c_edge_sharpness)) {
                const std::optional<float> sharpness = attributes.edge_sharpness.try_get(idx);
                if (sharpness.has_value()) {
                    attrs[erhe::geometry::c_edge_sharpness] = std::isinf(sharpness.value()) ? json("infinity") : json(sharpness.value());
                }
            }
        }
        elem["attributes"] = attrs;
        elements.push_back(elem);
    }

    return make_json_content({
        {"node",            node->get_name()},
        {"node_id",         node->get_id()},
        {"primitive_index", primitive_index},
        {"domain",          domain},
        {"elements",        elements}
    }).dump();
}

auto Mcp_server::action_set_edge_sharpness(const json& args) -> std::string
{
    if (m_context.operation_stack == nullptr) {
        return make_error_content("Operation stack not available");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*sr, args, "node_id", "node_name");
    if (!node) {
        return make_error_content("Node not found (give node_id or node_name)");
    }
    const std::size_t primitive_index = args.value("primitive_index", std::size_t{0});
    std::shared_ptr<erhe::scene::Mesh>        mesh;
    std::shared_ptr<erhe::geometry::Geometry> geometry;
    if (!resolve_mesh_geometry(node, primitive_index, mesh, geometry)) {
        return make_error_content("Node has no mesh geometry at primitive_index " + std::to_string(primitive_index) + ": " + node->get_name());
    }

    // Target value: "sharpness" number (or the string "infinity"), or
    // "clear": true to remove the values (back to smooth).
    std::optional<float> after;
    const bool clear = args.value("clear", false);
    if (!clear) {
        if (!args.contains("sharpness")) {
            return make_error_content("sharpness is required (number or \"infinity\"), or pass \"clear\": true");
        }
        if (args["sharpness"].is_string()) {
            if (args["sharpness"].get<std::string>() != "infinity") {
                return make_error_content("sharpness string value must be \"infinity\"");
            }
            after = std::numeric_limits<float>::infinity();
        } else {
            const float value = args["sharpness"].get<float>();
            if (value < 0.0f) {
                return make_error_content("sharpness must be >= 0");
            }
            after = value;
        }
    }

    // Target edges: explicit [v0, v1] pairs, or the current edge component
    // selection on this geometry when "edges" is omitted.
    Set_edge_sharpness_operation::Parameters parameters{};
    parameters.geometry = geometry;
    parameters.after    = after;
    const erhe::geometry::Mesh_attributes& attributes   = geometry->get_attributes();
    const GEO::index_t                     vertex_count = geometry->get_mesh().vertices.nb();
    const auto add_edge = [&](const GEO::index_t v0, const GEO::index_t v1) -> bool {
        const GEO::index_t edge = geometry->get_edge(v0, v1);
        if (edge == GEO::NO_EDGE) {
            return false;
        }
        parameters.edges .emplace_back(std::min(v0, v1), std::max(v0, v1));
        parameters.before.push_back(attributes.edge_sharpness.try_get(edge));
        return true;
    };
    if (args.contains("edges") && args["edges"].is_array()) {
        for (const auto& e : args["edges"]) {
            if (!e.is_array() || (e.size() != 2)) {
                return make_error_content("Each edge must be a [v0, v1] vertex-index pair");
            }
            const GEO::index_t v0 = e[0].get<GEO::index_t>();
            const GEO::index_t v1 = e[1].get<GEO::index_t>();
            if ((v0 >= vertex_count) || (v1 >= vertex_count)) {
                return make_error_content("Edge vertex index out of range (vertex_count " + std::to_string(vertex_count) + ")");
            }
            if (!add_edge(v0, v1)) {
                return make_error_content("No such edge: [" + std::to_string(v0) + ", " + std::to_string(v1) + "]");
            }
        }
    } else {
        Mesh_component_selection* selection = m_context.mesh_component_selection;
        if (selection != nullptr) {
            Mesh_component_entry* entry = selection->find_entry(mesh, primitive_index, geometry);
            if (entry != nullptr) {
                for (const Mesh_edge_key& key : entry->edges) {
                    add_edge(key.first, key.second); // stale selection keys are skipped
                }
            }
        }
        if (parameters.edges.empty()) {
            return make_error_content("No edges given and no edge component selection on this geometry");
        }
    }

    const std::size_t edge_count = parameters.edges.size();
    m_context.operation_stack->queue(std::make_shared<Set_edge_sharpness_operation>(std::move(parameters)));

    json result = {
        {"node",            node->get_name()},
        {"node_id",         node->get_id()},
        {"primitive_index", primitive_index},
        {"edges",           edge_count}
    };
    if (clear) {
        result["cleared"] = true;
    } else {
        result["sharpness"] = std::isinf(after.value()) ? json("infinity") : json(after.value());
    }
    return make_json_content(result).dump();
}

auto Mcp_server::action_align_components(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    const bool apply_scale = args.value("apply_scale", false);
    const bool aligned = m_context.operations->align_selection(apply_scale);
    if (!aligned) {
        return make_error_content("Align failed: requires exactly two components of the active mode (vertex/edge/face) selected on two distinct nodes");
    }
    return make_json_content({{"aligned", true}, {"apply_scale", apply_scale}}).dump();
}

auto Mcp_server::action_add_joint(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    const std::string avoidance_str = args.value("avoidance", "joint_pair");
    Add_joint_avoidance avoidance = Add_joint_avoidance::joint_pair;
    if (avoidance_str == "whole_world") {
        avoidance = Add_joint_avoidance::whole_world;
    } else if (avoidance_str != "joint_pair") {
        return make_error_content("Invalid avoidance: " + avoidance_str + " (joint_pair, whole_world)");
    }
    const bool created = m_context.operations->add_joint(avoidance);
    if (!created) {
        return make_error_content("Add Joint failed: needs two aligned components on distinct rigid bodies and a non-intersecting orientation (see editor log for the specific reason)");
    }
    return make_json_content({{"created", true}, {"avoidance", avoidance_str}}).dump();
}

auto Mcp_server::action_flip_joint(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    const std::string avoidance_str = args.value("avoidance", "joint_pair");
    Add_joint_avoidance avoidance = Add_joint_avoidance::joint_pair;
    if (avoidance_str == "whole_world") {
        avoidance = Add_joint_avoidance::whole_world;
    } else if (avoidance_str != "joint_pair") {
        return make_error_content("Invalid avoidance: " + avoidance_str + " (joint_pair, whole_world)");
    }
    const bool flipped = m_context.operations->flip_joint(avoidance);
    if (!flipped) {
        return make_error_content("Flip Joint failed: select a rigid-body party of a hinge joint (see editor log for the specific reason)");
    }
    return make_json_content({{"flipped", true}, {"avoidance", avoidance_str}}).dump();
}

auto Mcp_server::query_get_physics_state(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*sr, args, "node_id", "node_name");
    if (!node) {
        return make_error_content("Node not found (give node_id or node_name)");
    }
    const std::shared_ptr<Node_physics> node_physics = erhe::scene::get_attachment<Node_physics>(node.get());
    if (!node_physics) {
        return make_error_content("Node has no rigid body: " + node->get_name());
    }
    const erhe::physics::IRigid_body* rigid_body = node_physics->get_rigid_body();
    if (rigid_body == nullptr) {
        return make_error_content("Rigid body is not live (node not attached to a scene): " + node->get_name());
    }
    const glm::vec3 lin = rigid_body->get_linear_velocity();
    const glm::vec3 ang = rigid_body->get_angular_velocity();
    const glm::vec3 pos = glm::vec3{rigid_body->get_world_transform()[3]};
    return make_json_content({
        {"node",             node->get_name()},
        {"motion_mode",      motion_mode_to_string(rigid_body->get_motion_mode())},
        {"is_active",        rigid_body->is_active()},
        {"position",         {pos.x, pos.y, pos.z}},
        {"linear_velocity",  {lin.x, lin.y, lin.z}},
        {"angular_velocity", {ang.x, ang.y, ang.z}},
        {"linear_speed",     glm::length(lin)},
        {"angular_speed",    glm::length(ang)}
    }).dump();
}

auto Mcp_server::run_geometry_op_with_target(const json& args, const std::function<void()>& op) -> std::string
{
    // No explicit target: the operation acts on the current selection, as
    // before.
    const bool has_target = args.contains("node_ids") || args.contains("node_id") || args.contains("node_name");
    if (!has_target) {
        op();
        return {};
    }

    if (m_context.selection == nullptr) {
        return "Selection system not available";
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return "Scene not found: " + scene_name + " (scene_name is required with node targets)";
    }

    std::vector<std::shared_ptr<erhe::Item_base>> targets;
    if (args.contains("node_ids")) {
        const json& ids = args["node_ids"];
        if (!ids.is_array() || ids.empty()) {
            return "node_ids must be a non-empty array of node ids";
        }
        std::set<std::size_t> target_ids;
        for (const auto& id_val : ids) {
            if (!id_val.is_number_unsigned() && !id_val.is_number_integer()) {
                return "node_ids entries must be integers";
            }
            target_ids.insert(id_val.get<std::size_t>());
        }
        targets = find_items_by_ids(*sr, target_ids);
        if (targets.size() != target_ids.size()) {
            return "Some node_ids were not found in scene: " + sr->get_name();
        }
    } else {
        const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*sr, args, "node_id", "node_name");
        if (!node) {
            return "Node not found (give node_id or node_name)";
        }
        targets.push_back(node);
    }

    // Snapshot - retarget - run - restore. The geometry operations snapshot
    // the selection synchronously (Operations::resolve_operation_items) even
    // though the mesh work itself is async, so the caller-visible selection
    // is back to what it was when this returns.
    const std::vector<std::shared_ptr<erhe::Item_base>> saved = m_context.selection->get_selected_items();
    {
        Scoped_selection_change change{*m_context.selection};
        m_context.selection->set_selection(targets);
    }
    op();
    {
        Scoped_selection_change change{*m_context.selection};
        m_context.selection->set_selection(saved);
    }
    return {};
}

auto Mcp_server::action_remesh(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    const unsigned int target     = static_cast<unsigned int>(args.value("target_vertex_count", 2000));
    const float        anisotropy = args.value("anisotropy", 0.0f);
    const bool         regen      = args.value("regenerate_attributes", true);
    const std::string  target_error = run_geometry_op_with_target(args, [&]() {
        if (anisotropy > 0.0f) {
            m_context.operations->anisotropic_remesh(target, anisotropy, regen);
        } else {
            m_context.operations->remesh(target, regen);
        }
    });
    if (!target_error.empty()) {
        return make_error_content(target_error);
    }
    return make_json_content({
        {"queued",                true},
        {"target_vertex_count",   target},
        {"anisotropy",            anisotropy},
        {"regenerate_attributes", regen}
    }).dump();
}

auto Mcp_server::action_decimate(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    const unsigned int bins  = static_cast<unsigned int>(args.value("bins", 50));
    const bool         regen = args.value("regenerate_attributes", true);
    const std::string  target_error = run_geometry_op_with_target(args, [&]() {
        m_context.operations->decimate(bins, regen);
    });
    if (!target_error.empty()) {
        return make_error_content(target_error);
    }
    return make_json_content({{"queued", true}, {"bins", bins}, {"regenerate_attributes", regen}}).dump();
}

auto Mcp_server::action_smooth(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    const unsigned int iterations = static_cast<unsigned int>(args.value("iterations", 5));
    const float        strength   = args.value("strength", 0.5f);
    const bool         regen      = args.value("regenerate_attributes", true);
    const std::string  target_error = run_geometry_op_with_target(args, [&]() {
        m_context.operations->smooth(iterations, strength, regen);
    });
    if (!target_error.empty()) {
        return make_error_content(target_error);
    }
    return make_json_content({{"queued", true}, {"iterations", iterations}, {"strength", strength}, {"regenerate_attributes", regen}}).dump();
}

auto Mcp_server::action_chamfer3(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    const float bevel_ratio = args.value("bevel_ratio", 0.25f);
    const std::string target_error = run_geometry_op_with_target(args, [&]() {
        m_context.operations->chamfer3(bevel_ratio);
    });
    if (!target_error.empty()) {
        return make_error_content(target_error);
    }
    return make_json_content({{"queued", true}, {"bevel_ratio", bevel_ratio}}).dump();
}

auto Mcp_server::action_csg(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    if (m_context.selection == nullptr) {
        return make_error_content("Selection system not available");
    }
    const std::string operation = args.value("operation", "");
    if ((operation != "union") && (operation != "intersection") && (operation != "difference")) {
        return make_error_content("Invalid operation: '" + operation + "' (union, intersection, difference)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<erhe::scene::Node> target_node = find_node_in_scene(*sr, args, "node_id", "node_name");
    if (!target_node) {
        return make_error_content("Target node not found (give node_id or node_name)");
    }
    std::vector<std::shared_ptr<erhe::scene::Node>> tool_nodes;
    if (args.contains("tool_node_ids")) {
        const json& ids = args["tool_node_ids"];
        if (!ids.is_array() || ids.empty()) {
            return make_error_content("tool_node_ids must be a non-empty array of node ids");
        }
        for (const auto& id_value : ids) {
            if (!id_value.is_number_unsigned() && !id_value.is_number_integer()) {
                return make_error_content("tool_node_ids entries must be integers");
            }
            json lookup = {{"tool_node_id", id_value.get<std::size_t>()}};
            const std::shared_ptr<erhe::scene::Node> tool_node = find_node_in_scene(*sr, lookup, "tool_node_id", "tool_node_name");
            if (!tool_node) {
                return make_error_content("Tool node not found: " + id_value.dump());
            }
            tool_nodes.push_back(tool_node);
        }
    } else {
        const std::shared_ptr<erhe::scene::Node> tool_node = find_node_in_scene(*sr, args, "tool_node_id", "tool_node_name");
        if (!tool_node) {
            return make_error_content("Tool node not found (give tool_node_id / tool_node_name / tool_node_ids)");
        }
        tool_nodes.push_back(tool_node);
    }
    for (const std::shared_ptr<erhe::scene::Node>& tool_node : tool_nodes) {
        if (target_node == tool_node) {
            return make_error_content("Target and tool must be different nodes");
        }
    }

    // Snapshot - retarget - run - restore, like run_geometry_op_with_target,
    // but with explicit ordering: the boolean takes the FIRST item as the
    // target and the rest as tools. Binary_mesh_operation snapshots the items
    // synchronously (Operations::resolve_operation_items), so restoring the
    // selection here does not race the async operation. The boolean acts on
    // the active scene's selection bucket, so activate the target scene too.
    const std::vector<std::shared_ptr<erhe::Item_base>> saved = m_context.selection->get_selected_items();
    m_context.selection->set_active_scene_root(sr->shared_from_this());
    {
        std::vector<std::shared_ptr<erhe::Item_base>> selection;
        selection.push_back(target_node);
        selection.insert(selection.end(), tool_nodes.begin(), tool_nodes.end());
        Scoped_selection_change change{*m_context.selection};
        m_context.selection->set_selection(selection);
    }
    if      (operation == "union")        { m_context.operations->union_();       }
    else if (operation == "intersection") { m_context.operations->intersection(); }
    else                                  { m_context.operations->difference();   }
    {
        Scoped_selection_change change{*m_context.selection};
        m_context.selection->set_selection(saved);
    }
    json tools = json::array();
    for (const std::shared_ptr<erhe::scene::Node>& tool_node : tool_nodes) {
        tools.push_back({{"id", tool_node->get_id()}, {"name", tool_node->get_name()}});
    }
    return make_json_content({
        {"queued",    true},
        {"operation", operation},
        {"target",    {{"id", target_node->get_id()}, {"name", target_node->get_name()}}},
        {"tools",     tools}
    }).dump();
}

auto Mcp_server::action_lattice_deform(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    erhe::geometry::operation::Lattice_deform_parameters parameters;
    bool auto_fit_cage = true;

    auto read_vec3 = [&args](const char* key, glm::vec3& out_value) -> bool {
        const json value = args.value(key, json());
        if (value.is_array() && (value.size() == 3)) {
            out_value = glm::vec3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
            return true;
        }
        return false;
    };
    glm::vec3 cage_min{};
    glm::vec3 cage_max{};
    const bool has_min = read_vec3("cage_min", cage_min);
    const bool has_max = read_vec3("cage_max", cage_max);
    if (has_min != has_max) {
        return make_error_content("Give both cage_min and cage_max, or neither (auto fit)");
    }
    if (has_min) {
        parameters.cage_min = cage_min;
        parameters.cage_max = cage_max;
        auto_fit_cage = false;
    }

    const json divisions_json = args.value("divisions", json());
    if (divisions_json.is_array() && (divisions_json.size() == 3)) {
        parameters.divisions = glm::ivec3{
            std::max(1, divisions_json[0].get<int>()),
            std::max(1, divisions_json[1].get<int>()),
            std::max(1, divisions_json[2].get<int>())
        };
    }

    const std::string interpolation = args.value("interpolation", "bezier");
    if (interpolation == "trilinear") {
        parameters.interpolation = erhe::geometry::operation::Lattice_interpolation::trilinear;
    } else if (interpolation == "bezier") {
        parameters.interpolation = erhe::geometry::operation::Lattice_interpolation::bezier;
    } else {
        return make_error_content("Invalid interpolation: " + interpolation + " (trilinear, bezier)");
    }
    parameters.regenerate_attributes = args.value("regenerate_attributes", true);

    parameters.control_point_offsets.assign(
        erhe::geometry::operation::lattice_control_point_count(parameters.divisions),
        glm::vec3{0.0f}
    );
    const json offsets_json = args.value("offsets", json::array());
    if (!offsets_json.is_array() || offsets_json.empty()) {
        return make_error_content("offsets is required: [[i, j, k, dx, dy, dz], ...] control point displacements");
    }
    for (const auto& entry : offsets_json) {
        if (!entry.is_array() || (entry.size() != 6)) {
            return make_error_content("offsets entries must be [i, j, k, dx, dy, dz]");
        }
        const int i = entry[0].get<int>();
        const int j = entry[1].get<int>();
        const int k = entry[2].get<int>();
        if (
            (i < 0) || (i > parameters.divisions.x) ||
            (j < 0) || (j > parameters.divisions.y) ||
            (k < 0) || (k > parameters.divisions.z)
        ) {
            return make_error_content(
                fmt::format(
                    "offset index ({}, {}, {}) out of range - lattice has {}x{}x{} control points",
                    i, j, k,
                    parameters.divisions.x + 1, parameters.divisions.y + 1, parameters.divisions.z + 1
                )
            );
        }
        const std::size_t index = erhe::geometry::operation::lattice_offset_index(parameters.divisions, i, j, k);
        parameters.control_point_offsets[index] = glm::vec3{entry[3].get<float>(), entry[4].get<float>(), entry[5].get<float>()};
    }

    const glm::ivec3  divisions_echo = parameters.divisions;
    const std::string target_error   = run_geometry_op_with_target(args, [&]() {
        m_context.operations->lattice_deform(std::move(parameters), auto_fit_cage);
    });
    if (!target_error.empty()) {
        return make_error_content(target_error);
    }
    return make_json_content({
        {"queued",        true},
        {"divisions",     {divisions_echo.x, divisions_echo.y, divisions_echo.z}},
        {"interpolation", interpolation},
        {"auto_fit_cage", auto_fit_cage},
        {"offset_count",  offsets_json.size()}
    }).dump();
}

auto Mcp_server::action_project_texcoords(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    erhe::geometry::operation::Project_texcoords_parameters parameters{};
    // Explicit-state rule (doc/mcp_api_guidelines.md): fixed defaults here,
    // never state read from any window.
    const std::string projection = args.value("projection", "planar");
    if      (projection == "planar")      parameters.projection = erhe::geometry::operation::Texcoord_projection::planar;
    else if (projection == "cylindrical") parameters.projection = erhe::geometry::operation::Texcoord_projection::cylindrical;
    else if (projection == "spherical")   parameters.projection = erhe::geometry::operation::Texcoord_projection::spherical;
    else {
        return make_error_content("Unknown projection: " + projection + " (planar|cylindrical|spherical)");
    }
    parameters.axis = std::clamp(args.value("axis", 2), 0, 2);
    const json scale_json = args.value("scale", json::array({1.0f, 1.0f}));
    const json offset_json = args.value("offset", json::array({0.0f, 0.0f}));
    if ((!scale_json.is_array()) || (scale_json.size() != 2) || (!offset_json.is_array()) || (offset_json.size() != 2)) {
        return make_error_content("scale and offset must be [u, v] arrays");
    }
    parameters.scale  = glm::vec2{scale_json [0].get<float>(), scale_json [1].get<float>()};
    parameters.offset = glm::vec2{offset_json[0].get<float>(), offset_json[1].get<float>()};

    const std::string target_error = run_geometry_op_with_target(args, [&]() {
        m_context.operations->project_texcoords(parameters);
    });
    if (!target_error.empty()) {
        return make_error_content(target_error);
    }
    return make_json_content({
        {"queued",     true},
        {"projection", projection},
        {"axis",       parameters.axis}
    }).dump();
}

auto Mcp_server::action_merge_faces(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    const std::string target_error = run_geometry_op_with_target(args, [&]() {
        m_context.operations->merge_faces();
    });
    if (!target_error.empty()) {
        return make_error_content(target_error);
    }
    return make_json_content({{"queued", true}}).dump();
}

auto Mcp_server::action_catmull_clark(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    // Explicit-state rule (doc/mcp_api_guidelines.md): the argument default is
    // fixed here, never read from the Operations window's Generate UVs checkbox.
    const bool generate_texcoords = args.value("generate_texcoords", true);
    const std::string target_error = run_geometry_op_with_target(args, [&]() {
        m_context.operations->catmull_clark(generate_texcoords);
    });
    if (!target_error.empty()) {
        return make_error_content(target_error);
    }
    return make_json_content({{"queued", true}, {"generate_texcoords", generate_texcoords}}).dump();
}

auto Mcp_server::action_generate_texture_coordinates(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    const std::size_t texcoord_slot   = args.value("texcoord_slot", std::size_t{0});
    const float       hard_angles_deg = args.value("hard_angles_deg", 45.0f);
    const int         parameterizer   = args.value("parameterizer", 3);
    const int         packer          = args.value("packer", 2);
    m_context.operations->make_atlas(texcoord_slot, hard_angles_deg, parameterizer, packer);
    return make_json_content({
        {"queued",          true},
        {"texcoord_slot",   texcoord_slot},
        {"hard_angles_deg", hard_angles_deg},
        {"parameterizer",   parameterizer},
        {"packer",          packer}
    }).dump();
}

auto Mcp_server::action_set_transform_reference_mode(const json& args) -> std::string
{
    if (m_context.transform_tool == nullptr) {
        return make_error_content("Transform tool not available");
    }
    const std::string mode_str = args.value("mode", "");
    Transform_reference_mode mode = Transform_reference_mode::global;
    if      (mode_str == "global")    { mode = Transform_reference_mode::global;    }
    else if (mode_str == "world")     { mode = Transform_reference_mode::global;    } // alias for global
    else if (mode_str == "local")     { mode = Transform_reference_mode::local;     }
    else if (mode_str == "reference") { mode = Transform_reference_mode::reference; }
    else if (mode_str == "selection") { mode = Transform_reference_mode::selection; }
    else {
        return make_error_content("Invalid mode: " + mode_str + " (global/world, local, reference, selection)");
    }

    Transform_tool_shared& shared = m_context.transform_tool->shared;
    shared.settings.reference_mode = mode;

    json result = {{"mode", mode_str}};

    if (mode == Transform_reference_mode::reference) {
        const std::string scene_name = args.value("scene_name", "");
        std::shared_ptr<erhe::scene::Node> ref_node;
        if (!scene_name.empty()) {
            Scene_root* sr = find_scene(scene_name);
            if (sr != nullptr) {
                ref_node = find_node_in_scene(*sr, args, "reference_node_id", "reference_node_name");
            }
        } else if (m_context.app_scenes != nullptr) {
            for (const auto& sr : m_context.app_scenes->get_scene_roots()) {
                ref_node = find_node_in_scene(*sr, args, "reference_node_id", "reference_node_name");
                if (ref_node) {
                    break;
                }
            }
        }
        if (ref_node) {
            shared.reference_node = ref_node;
            result["reference_node"] = ref_node->get_name();
        } else if (args.contains("reference_node_id") || args.contains("reference_node_name")) {
            return make_error_content("Reference node not found (give reference_node_id or reference_node_name)");
        }
    }

    if (args.contains("edge_normal_blend")) {
        shared.settings.edge_normal_blend = args.value("edge_normal_blend", 0.5f);
    }

    m_context.transform_tool->on_reference_settings_changed();
    return make_json_content(result).dump();
}

auto Mcp_server::action_set_transform_mode(const json& args) -> std::string
{
    if (m_context.editor_settings == nullptr) {
        return make_error_content("Editor settings not available");
    }
    const std::string   mode_str = args.value("mode", "");
    Mesh_transform_mode mode     = Mesh_transform_mode::move;
    if (!::from_string(mode_str, mode)) {
        return make_error_content("Invalid mode: " + mode_str + " (move, extrude, extrude_group_normal, extrude_vertex_normal)");
    }
    m_context.editor_settings->transform_mode = mode;
    if (m_context.app_settings != nullptr) {
        m_context.app_settings->settings_store().touch();
    }
    return make_json_content(json{{"mode", std::string{::to_string(mode)}}}).dump();
}

auto Mcp_server::action_set_gizmo_visibility(const json& args) -> std::string
{
    // Headless-scriptable equivalent of activating the Move/Rotate/Scale tool (or clicking
    // the viewport-toolbar gizmo toggles): tool activation is otherwise reachable only
    // through the hotbar / mouse UI.
    if (m_context.transform_tool == nullptr) {
        return make_error_content("Transform tool not available");
    }
    Transform_tool_settings& settings = m_context.transform_tool->shared.settings;
    if (args.contains("translate")) {
        settings.show_translate = args.value("translate", settings.show_translate);
    }
    if (args.contains("rotate")) {
        settings.show_rotate = args.value("rotate", settings.show_rotate);
    }
    if (args.contains("scale")) {
        settings.show_scale = args.value("scale", settings.show_scale);
    }
    const std::string scale_gizmo_mode_str = args.value("scale_gizmo_mode", "");
    if (scale_gizmo_mode_str == "basic") {
        settings.scale_gizmo_mode = Scale_gizmo_mode::basic;
    } else if (scale_gizmo_mode_str == "bounding_box") {
        settings.scale_gizmo_mode = Scale_gizmo_mode::bounding_box;
    } else if (!scale_gizmo_mode_str.empty()) {
        return make_error_content("Invalid scale_gizmo_mode: " + scale_gizmo_mode_str + " (basic, bounding_box)");
    }
    m_context.transform_tool->update_visibility();

    json result;
    result["translate"]        = settings.show_translate;
    result["rotate"]           = settings.show_rotate;
    result["scale"]            = settings.show_scale;
    result["scale_gizmo_mode"] = (settings.scale_gizmo_mode == Scale_gizmo_mode::bounding_box) ? "bounding_box" : "basic";
    return make_json_content(result).dump();
}

auto Mcp_server::query_transform_state(const json& args) -> std::string
{
    static_cast<void>(args);
    if (m_context.transform_tool == nullptr) {
        return make_error_content("Transform tool not available");
    }
    const Transform_tool_shared& shared = m_context.transform_tool->shared;

    json result;
    result["reference_mode"]         = transform_reference_mode_lc(shared.settings.reference_mode);
    result["edge_normal_blend"]      = shared.settings.edge_normal_blend;
    result["use_anchor_orientation"] = shared.settings.use_anchor_orientation();
    result["component_mode"]         = shared.component_mode;
    result["selected_node_count"]    = shared.entries.size();

    const std::shared_ptr<erhe::scene::Node> ref_node = shared.reference_node.lock();
    if (ref_node) {
        result["reference_node"] = {{"name", ref_node->get_name()}, {"id", ref_node->get_id()}};
    } else {
        result["reference_node"] = nullptr;
    }

    if (m_context.editor_settings != nullptr) {
        result["transform_mode"] = std::string{::to_string(m_context.editor_settings->transform_mode)};
    }

    // The resolved gizmo anchor frame in world space (origin + orientation the
    // gizmo and the local-space numeric edits operate in).
    const erhe::scene::Trs_transform& anchor = shared.world_from_anchor;
    const glm::vec3 t = anchor.get_translation();
    const glm::quat r = anchor.get_rotation();
    result["anchor_frame"] = {
        {"translation",   {t.x, t.y, t.z}},
        {"rotation_xyzw", {r.x, r.y, r.z, r.w}}
    };

    return make_json_content(result).dump();
}


} // namespace editor
