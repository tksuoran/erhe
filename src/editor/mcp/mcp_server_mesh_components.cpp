// Mcp_server mesh component tools (component selection, geometry ops, transform modes).
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server_shared.hpp"

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

auto Mcp_server::action_remesh(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    const unsigned int target     = static_cast<unsigned int>(args.value("target_vertex_count", 2000));
    const float        anisotropy = args.value("anisotropy", 0.0f);
    const bool         regen      = args.value("regenerate_attributes", true);
    if (anisotropy > 0.0f) {
        m_context.operations->anisotropic_remesh(target, anisotropy, regen);
    } else {
        m_context.operations->remesh(target, regen);
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
    m_context.operations->decimate(bins, regen);
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
    m_context.operations->smooth(iterations, strength, regen);
    return make_json_content({{"queued", true}, {"iterations", iterations}, {"strength", strength}, {"regenerate_attributes", regen}}).dump();
}

auto Mcp_server::action_chamfer3(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    const float bevel_ratio = args.value("bevel_ratio", 0.25f);
    m_context.operations->chamfer3(bevel_ratio);
    return make_json_content({{"queued", true}, {"bevel_ratio", bevel_ratio}}).dump();
}

auto Mcp_server::action_merge_faces(const json& /*args*/) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    m_context.operations->merge_faces();
    return make_json_content({{"queued", true}}).dump();
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
    return make_json_content(json{{"mode", std::string{::to_string(mode)}}}).dump();
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
