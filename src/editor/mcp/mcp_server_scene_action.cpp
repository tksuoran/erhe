// Mcp_server scene action tools (select, transform, brush, shapes, nodes, lights, cameras, reparent, lock, tags).
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server_shared.hpp"

#include "scene/generated/scene_settings_serialization.hpp"

#include <simdjson.h>

namespace editor {

using namespace mcp_server_detail;

auto Mcp_server::action_set_scene_settings(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    bool changed = false;
    if (args.contains("ambient_light")) {
        const json& value = args["ambient_light"];
        if (!value.is_array() || (value.size() < 3)) {
            json r = make_text_content("ambient_light must be an array of 3 or 4 numbers");
            r["isError"] = true;
            return r.dump();
        }
        sr->get_scene().ambient_light = glm::vec4{
            value[0].get<float>(),
            value[1].get<float>(),
            value[2].get<float>(),
            (value.size() >= 4) ? value[3].get<float>() : 0.0f
        };
        changed = true;
    }
    if (args.contains("settings")) {
        const json& value = args["settings"];
        if (!value.is_object()) {
            json r = make_text_content("settings must be an object (Scene_settings shape; {} resets every override)");
            r["isError"] = true;
            return r.dump();
        }
        // Replace semantics: the whole Scene_settings is rebuilt from the
        // given object, so omitted fields return to "use the editor-global
        // default" ({} clears every override).
        Scene_settings new_settings{};
        const std::string            settings_text = value.dump();
        simdjson::ondemand::parser   settings_parser;
        simdjson::padded_string      settings_padded{settings_text};
        simdjson::ondemand::document settings_document;
        simdjson::ondemand::object   settings_object;
        const bool ok =
            (settings_parser.iterate(settings_padded).get(settings_document) == simdjson::SUCCESS) &&
            (settings_document.get_object().get(settings_object) == simdjson::SUCCESS) &&
            (deserialize(settings_object, new_settings) == simdjson::SUCCESS);
        if (!ok) {
            json r = make_text_content("settings did not deserialize as Scene_settings");
            r["isError"] = true;
            return r.dump();
        }
        sr->get_scene_settings() = new_settings;
        changed = true;
    }
    return make_json_content({
        {"updated",    changed},
        {"scene_name", sr->get_name()}
    }).dump();
}

auto Mcp_server::action_select_items(const json& args) -> std::string
{
    if (!m_context.selection) {
        json r = make_text_content("Selection system not available");
        r["isError"] = true;
        return r.dump();
    }

    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const json ids_json = args.value("ids", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& id_val : ids_json) {
        if (id_val.is_number_unsigned() || id_val.is_number_integer()) {
            target_ids.insert(id_val.get<std::size_t>());
        }
    }

    // Mirrors the UI's scoped selection semantics: selecting (or clearing)
    // in one scene leaves other scenes' selections untouched, and the
    // selection change makes the target scene the active scene.
    if (target_ids.empty()) {
        m_context.selection->clear_selection(static_cast<erhe::Item_host*>(sr));
        return make_text_content("Selection cleared in scene: " + sr->get_name()).dump();
    }

    auto items_to_select = find_items_by_ids(*sr, target_ids);
    {
        Scoped_selection_change selection_change{*m_context.selection};
        m_context.selection->clear_selection(static_cast<erhe::Item_host*>(sr));
        for (const std::shared_ptr<erhe::Item_base>& item : items_to_select) {
            m_context.selection->add_to_selection(item);
        }
    }

    json selected = json::array();
    for (const auto& item : items_to_select) {
        selected.push_back({
            {"name", item->get_name()},
            {"type", std::string{item->get_type_name()}},
            {"id",   item->get_id()}
        });
    }

    return make_json_content({
        {"selected_count", static_cast<int>(items_to_select.size())},
        {"items",          selected}
    }).dump();
}

auto Mcp_server::action_transform_selection(const json& args) -> std::string
{
    Transform_tool* transform_tool = m_context.transform_tool;
    if (transform_tool == nullptr) {
        json r = make_text_content("Transform tool not available");
        r["isError"] = true;
        return r.dump();
    }

    Transform_tool_shared& shared = transform_tool->shared;
    // A node selection populates shared.entries; an active mesh-component selection
    // instead sets shared.component_mode (entries stays empty). Either is a valid target,
    // matching the gizmo's own "nothing to transform" condition used elsewhere.
    if (shared.entries.empty() && !shared.component_mode) {
        json r = make_text_content("Nothing to transform - select node(s) with select_items, or a mesh-component selection with select_mesh_components");
        r["isError"] = true;
        return r.dump();
    }

    const std::string space = args.value("space", "global");
    if ((space != "local") && (space != "global")) {
        json r = make_text_content("Invalid space '" + space + "' (expected 'local' or 'global')");
        r["isError"] = true;
        return r.dump();
    }
    const bool local = (space == "local");
    if (local && (shared.entries.size() != 1)) {
        json r = make_text_content("Local space edit requires exactly one selected node");
        r["isError"] = true;
        return r.dump();
    }

    std::string parse_error;
    auto read_floats = [&args, &parse_error](const char* key, float* out_values, std::size_t count) -> bool {
        if (!args.contains(key)) {
            return false;
        }
        const json& value = args.at(key);
        const bool shape_ok = value.is_array() && (value.size() == count);
        if (shape_ok) {
            for (std::size_t i = 0; i < count; ++i) {
                if (!value[i].is_number()) {
                    parse_error = std::string{key} + " must be an array of " + std::to_string(count) + " numbers";
                    return false;
                }
                out_values[i] = value[i].get<float>();
            }
            return true;
        }
        parse_error = std::string{key} + " must be an array of " + std::to_string(count) + " numbers";
        return false;
    };

    std::optional<glm::vec3> translation;
    std::optional<glm::quat> rotation;
    std::optional<glm::vec3> scale;
    std::optional<glm::vec3> skew;
    float v[4];
    if (read_floats("translation",   v, 3)) { translation = glm::vec3{v[0], v[1], v[2]};       }
    if (read_floats("rotation_xyzw", v, 4)) { rotation    = glm::quat{v[3], v[0], v[1], v[2]}; }
    if (read_floats("scale",         v, 3)) { scale       = glm::vec3{v[0], v[1], v[2]};       }
    if (read_floats("skew",          v, 3)) { skew        = glm::vec3{v[0], v[1], v[2]};       }
    if (!parse_error.empty()) {
        json r = make_text_content(parse_error);
        r["isError"] = true;
        return r.dump();
    }
    if (!translation.has_value() && !rotation.has_value() && !scale.has_value() && !skew.has_value()) {
        json r = make_text_content("Nothing to apply - provide translation, rotation_xyzw, scale and/or skew");
        r["isError"] = true;
        return r.dump();
    }

    json applied = json::object();
    if (translation.has_value()) {
        transform_tool->apply_translation_edit(translation.value(), local);
        applied["translation"] = {translation->x, translation->y, translation->z};
    }
    if (rotation.has_value()) {
        transform_tool->apply_rotation_edit(rotation.value(), local);
        applied["rotation_xyzw"] = {rotation->x, rotation->y, rotation->z, rotation->w};
    }
    if (scale.has_value()) {
        transform_tool->apply_scale_edit(scale.value(), local);
        applied["scale"] = {scale->x, scale->y, scale->z};
    }
    if (skew.has_value()) {
        transform_tool->apply_skew_edit(skew.value(), local);
        applied["skew"] = {skew->x, skew->y, skew->z};
    }

    const bool end_edit = args.value("end_edit", true);
    if (end_edit) {
        // Mirror the gizmo drag-release: the node record path is a no-op in component
        // mode (shared.entries is empty there), so a mesh-component edit (move / extrude /
        // extrude_group_normal / extrude_vertex_normal) must be finalized via
        // commit_component_edit() to queue its undoable operation. Without this, an MCP
        // component edit would deform the live geometry but never commit (and never finalize
        // extrude normals).
        transform_tool->record_transform_operation();
        if (shared.component_mode && transform_tool->is_component_edit_active()) {
            transform_tool->commit_component_edit();
        }
    }

    auto trs_to_json = [](const erhe::scene::Trs_transform& trs) -> json {
        const glm::vec3 t = trs.get_translation();
        const glm::quat r = trs.get_rotation();
        const glm::vec3 s = trs.get_scale();
        const glm::vec3 k = trs.get_skew();
        return json{
            {"translation",   {t.x, t.y, t.z}},
            {"rotation_xyzw", {r.x, r.y, r.z, r.w}},
            {"scale",         {s.x, s.y, s.z}},
            {"skew",          {k.x, k.y, k.z}}
        };
    };

    json nodes = json::array();
    for (const Transform_entry& entry : shared.entries) {
        if (!entry.node) {
            continue;
        }
        nodes.push_back({
            {"name",            entry.node->get_name()},
            {"id",              entry.node->get_id()},
            {"local_transform", trs_to_json(entry.node->parent_from_node_transform())},
            {"world_transform", trs_to_json(entry.node->world_from_node_transform())}
        });
    }

    return make_json_content({
        {"space",    space},
        {"applied",  applied},
        {"end_edit", end_edit},
        {"nodes",    nodes}
    }).dump();
}

auto Mcp_server::action_place_brush(const json& args) -> std::string
{
    const std::string scene_name    = args.value("scene_name", "");
    const std::size_t brush_id      = args.value("brush_id", std::size_t{0});
    const json        pos_json      = args.value("position", json::array());
    const std::string material_name = args.value("material_name", "");
    const double      scale         = args.value("scale", 1.0);
    const std::string motion_str    = args.value("motion_mode", "dynamic");

    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    // Find brush by ID
    auto library = sr->get_content_library();
    if (!library || !library->brushes) {
        json r = make_text_content("No brushes in scene");
        r["isError"] = true;
        return r.dump();
    }

    std::shared_ptr<Brush> brush;
    const auto& brush_list = library->brushes->get_all<Brush>();
    for (const auto& b : brush_list) {
        if (b->get_id() == brush_id) {
            brush = b;
            break;
        }
    }
    if (!brush) {
        json r = make_text_content("Brush not found with id: " + std::to_string(brush_id));
        r["isError"] = true;
        return r.dump();
    }

    // Parse position
    glm::vec3 position{0.0f};
    if (pos_json.is_array() && pos_json.size() >= 3) {
        position.x = pos_json[0].get<float>();
        position.y = pos_json[1].get<float>();
        position.z = pos_json[2].get<float>();
    }

    // Find material
    std::shared_ptr<erhe::primitive::Material> material;
    if (!material_name.empty() && library->materials) {
        const auto& mat_list = library->materials->get_all<erhe::primitive::Material>();
        for (const auto& mat : mat_list) {
            if (mat->get_name() == material_name) {
                material = mat;
                break;
            }
        }
    }
    if (!material && library->materials) {
        const auto& mat_list = library->materials->get_all<erhe::primitive::Material>();
        if (!mat_list.empty()) {
            material = mat_list.front();
        }
    }
    if (!material) {
        json r = make_text_content("No materials available");
        r["isError"] = true;
        return r.dump();
    }

    // Motion mode
    erhe::physics::Motion_mode motion_mode = erhe::physics::Motion_mode::e_dynamic;
    if (motion_str == "static") {
        motion_mode = erhe::physics::Motion_mode::e_static;
    }

    const glm::mat4 world_from_node = erhe::math::create_translation<float>(position);

    auto node = place_brush_in_scene(
        m_context,
        *brush,
        *sr,
        world_from_node,
        material,
        scale,
        motion_mode
    );

    if (!node) {
        json r = make_text_content("Failed to place brush");
        r["isError"] = true;
        return r.dump();
    }

    json result = {
        {"node_name", node->get_name()},
        {"node_id",   node->get_id()},
        {"brush",     brush->get_name()},
        {"material",  material->get_name()},
        {"position",  {position.x, position.y, position.z}},
        {"scale",     scale}
    };
    return make_json_content(result).dump();
}

auto Mcp_server::action_create_shape(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const std::string shape = args.value("shape", "");
    if ((shape != "box") && (shape != "uv_sphere") && (shape != "cone") && (shape != "capsule") && (shape != "torus")) {
        json r = make_text_content("Invalid shape '" + shape + "' (expected box, uv_sphere, cone, capsule or torus)");
        r["isError"] = true;
        return r.dump();
    }

    const bool make_instance = args.value("instance", true);
    const bool add_brush     = args.value("add_brush", false);
    if (!make_instance && !add_brush) {
        json r = make_text_content("Nothing to do - enable instance and/or add_brush");
        r["isError"] = true;
        return r.dump();
    }

    auto read_vec3 = [&args](const char* key, glm::vec3& out_value) {
        const json value = args.value(key, json());
        if (value.is_array() && (value.size() == 3)) {
            out_value = glm::vec3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        }
    };
    auto read_ivec3 = [&args](const char* key, glm::ivec3& out_value) {
        const json value = args.value(key, json());
        if (value.is_array() && (value.size() == 3)) {
            out_value = glm::ivec3{value[0].get<int>(), value[1].get<int>(), value[2].get<int>()};
        }
    };

    Brush_data brush_create_info{
        .context      = m_context,
        .app_settings = *m_context.app_settings,
        .name         = args.value("name", shape),
        .build_info   = erhe::primitive::Build_info{
            .primitive_types = {
                .fill_triangles          = true,
                .fill_triangles_expanded = true,
                .edge_lines              = true,
                .corner_points           = true,
                .centroid_points         = true
            },
            .buffer_info     = m_context.mesh_memory->make_primitive_buffer_info()
        },
        .normal_style = erhe::primitive::Normal_style::point_normals,
        .density      = 1.0f
    };

    std::shared_ptr<Brush> brush;
    json parameters_echo = json::object();
    if (shape == "box") {
        Box_parameters parameters;
        read_vec3 ("size",  parameters.size);
        read_ivec3("steps", parameters.steps);
        parameters.power = args.value("power", parameters.power);
        brush = Create_box::create_brush(brush_create_info, parameters);
        parameters_echo = {
            {"size",  {parameters.size.x,  parameters.size.y,  parameters.size.z}},
            {"steps", {parameters.steps.x, parameters.steps.y, parameters.steps.z}},
            {"power", parameters.power}
        };
    } else if (shape == "uv_sphere") {
        Uv_sphere_parameters parameters;
        parameters.radius      = args.value("radius",      parameters.radius);
        parameters.slice_count = args.value("slice_count", parameters.slice_count);
        parameters.stack_count = args.value("stack_count", parameters.stack_count);
        brush = Create_uv_sphere::create_brush(brush_create_info, parameters);
        parameters_echo = {
            {"radius",      parameters.radius},
            {"slice_count", parameters.slice_count},
            {"stack_count", parameters.stack_count}
        };
    } else if (shape == "cone") {
        Cone_parameters parameters;
        parameters.height        = args.value("height",        parameters.height);
        parameters.bottom_radius = args.value("bottom_radius", parameters.bottom_radius);
        parameters.top_radius    = args.value("top_radius",    parameters.top_radius);
        parameters.use_top       = args.value("use_top",       parameters.use_top);
        parameters.use_bottom    = args.value("use_bottom",    parameters.use_bottom);
        parameters.slice_count   = args.value("slice_count",   parameters.slice_count);
        parameters.stack_count   = args.value("stack_count",   parameters.stack_count);
        brush = Create_cone::create_brush(brush_create_info, parameters);
        parameters_echo = {
            {"height",        parameters.height},
            {"bottom_radius", parameters.bottom_radius},
            {"top_radius",    parameters.top_radius},
            {"use_top",       parameters.use_top},
            {"use_bottom",    parameters.use_bottom},
            {"slice_count",   parameters.slice_count},
            {"stack_count",   parameters.stack_count}
        };
    } else if (shape == "capsule") {
        Capsule_parameters parameters;
        parameters.length                 = args.value("length",        parameters.length);
        parameters.bottom_radius          = args.value("bottom_radius", parameters.bottom_radius);
        parameters.top_radius             = args.value("top_radius",    parameters.top_radius);
        parameters.slice_count            = args.value("slice_count",   parameters.slice_count);
        parameters.hemisphere_stack_count = args.value("stack_count",   parameters.hemisphere_stack_count);
        // make_capsule() requires |bottom_radius - top_radius| < length when the
        // radii differ: the tangent cone between the cap spheres exists only
        // while neither sphere contains the other.
        if (
            (parameters.bottom_radius != parameters.top_radius) &&
            (parameters.length <= std::abs(parameters.bottom_radius - parameters.top_radius))
        ) {
            json r = make_text_content("Tapered capsule requires length > |bottom_radius - top_radius|");
            r["isError"] = true;
            return r.dump();
        }
        brush = Create_capsule::create_brush(brush_create_info, parameters);
        parameters_echo = {
            {"length",        parameters.length},
            {"bottom_radius", parameters.bottom_radius},
            {"top_radius",    parameters.top_radius},
            {"slice_count",   parameters.slice_count},
            {"stack_count",   parameters.hemisphere_stack_count},
            {"tapered",       parameters.bottom_radius != parameters.top_radius}
        };
    } else { // torus
        Torus_parameters parameters;
        parameters.major_radius = args.value("major_radius", parameters.major_radius);
        parameters.minor_radius = args.value("minor_radius", parameters.minor_radius);
        parameters.major_steps  = args.value("major_steps",  parameters.major_steps);
        parameters.minor_steps  = args.value("minor_steps",  parameters.minor_steps);
        brush = Create_torus::create_brush(brush_create_info, parameters);
        parameters_echo = {
            {"major_radius", parameters.major_radius},
            {"minor_radius", parameters.minor_radius},
            {"major_steps",  parameters.major_steps},
            {"minor_steps",  parameters.minor_steps}
        };
    }

    if (!brush) {
        json r = make_text_content("Failed to create shape: " + shape);
        r["isError"] = true;
        return r.dump();
    }

    json result = {
        {"shape",      shape},
        {"name",       brush->get_name()},
        {"parameters", parameters_echo}
    };

    auto library = sr->get_content_library();
    if (add_brush) {
        if (!library || !library->brushes) {
            json r = make_text_content("No brush library in scene");
            r["isError"] = true;
            return r.dump();
        }
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{library->mutex};
        library->brushes->add(brush);
        result["brush_id"] = brush->get_id();
    }

    if (make_instance) {
        std::shared_ptr<erhe::primitive::Material> material;
        const std::string material_name = args.value("material_name", "");
        if (!material_name.empty() && library && library->materials) {
            const auto& mat_list = library->materials->get_all<erhe::primitive::Material>();
            for (const auto& mat : mat_list) {
                if (mat->get_name() == material_name) {
                    material = mat;
                    break;
                }
            }
        }
        if (!material && library && library->materials) {
            const auto& mat_list = library->materials->get_all<erhe::primitive::Material>();
            if (!mat_list.empty()) {
                material = mat_list.front();
            }
        }
        if (!material) {
            json r = make_text_content("No materials available");
            r["isError"] = true;
            return r.dump();
        }

        glm::vec3 position{0.0f};
        read_vec3("position", position);

        std::shared_ptr<erhe::scene::Node> parent;
        if (args.contains("parent_node_id")) {
            const std::size_t parent_node_id = args.value("parent_node_id", std::size_t{0});
            for (const auto& node : sr->get_scene().get_flat_nodes()) {
                if (node->get_id() == parent_node_id) {
                    parent = node;
                    break;
                }
            }
            if (!parent) {
                json r = make_text_content("Parent node not found with id: " + std::to_string(parent_node_id));
                r["isError"] = true;
                return r.dump();
            }
        }

        const double scale = args.value("scale", 1.0);
        const erhe::physics::Motion_mode motion_mode = parse_motion_mode(
            args.value("motion_mode", "dynamic"),
            erhe::physics::Motion_mode::e_dynamic
        );

        const glm::mat4 world_from_node = erhe::math::create_translation<float>(position);
        auto instance_node = place_brush_in_scene(m_context, *brush, *sr, world_from_node, material, scale, motion_mode, parent);
        if (!instance_node) {
            json r = make_text_content("Failed to create shape instance");
            r["isError"] = true;
            return r.dump();
        }
        result["node_name"]   = instance_node->get_name();
        result["node_id"]     = instance_node->get_id();
        result["material"]    = material->get_name();
        result["position"]    = {position.x, position.y, position.z};
        result["motion_mode"] = motion_mode_to_string(motion_mode);
        result["parent"]      = parent ? parent->get_name() : "(scene root)";
    }

    return make_json_content(result).dump();
}

auto Mcp_server::action_create_node(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    std::shared_ptr<erhe::scene::Node> parent{};
    if (args.contains("parent_node_id") || args.contains("parent_node_name")) {
        parent = find_node_in_scene(*sr, args, "parent_node_id", "parent_node_name");
        if (!parent) {
            json r = make_text_content("Parent node not found");
            r["isError"] = true;
            return r.dump();
        }
    } else {
        parent = sr->get_hosted_scene()->get_root_node();
    }

    const std::shared_ptr<erhe::scene::Node> node = m_context.scene_commands->create_new_empty_node(parent.get());
    if (!node) {
        json r = make_text_content("Failed to create node");
        r["isError"] = true;
        return r.dump();
    }

    const std::string name = args.value("name", "");
    if (!name.empty()) {
        node->set_name(name);
    }

    glm::vec3 position{0.0f};
    const json pos_json = args.value("position", json());
    if (pos_json.is_array() && (pos_json.size() == 3)) {
        position = glm::vec3{pos_json[0].get<float>(), pos_json[1].get<float>(), pos_json[2].get<float>()};
    }
    // The node is not yet attached (the insert executes on the next editor
    // frame); setting the world transform now is preserved by Node::set_parent.
    node->set_world_from_node(erhe::math::create_translation<float>(position));

    return make_json_content({
        {"node_name", node->get_name()},
        {"node_id",   node->get_id()},
        {"parent",    parent->get_name()},
        {"position",  {position.x, position.y, position.z}},
        {"queued",    true} // the insert operation executes on the next editor frame
    }).dump();
}

auto Mcp_server::action_create_light(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const std::string type_str = args.value("type", "directional");
    if ((type_str != "directional") && (type_str != "point") && (type_str != "spot")) {
        json r = make_text_content("Invalid light type '" + type_str + "' (expected directional, point or spot)");
        r["isError"] = true;
        return r.dump();
    }
    const erhe::scene::Light_type type = parse_light_type(type_str, erhe::scene::Light_type::directional);

    auto read_vec3 = [&args](const char* key, glm::vec3& out_value) {
        const json value = args.value(key, json());
        if (value.is_array() && (value.size() == 3)) {
            out_value = glm::vec3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        }
    };

    glm::vec3 position{0.0f};
    read_vec3("position", position);
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    read_vec3("color", color);
    const float       intensity   = args.value("intensity",   1.0f);
    const bool        cast_shadow = args.value("cast_shadow", true);
    // Directional lights have no meaningful range (parallel rays); point / spot
    // default to the same 25.0 the editor's Scene_builder uses.
    const float       range       = args.value("range", (type == erhe::scene::Light_type::directional) ? 0.0f : 25.0f);
    const std::string name        = args.value("name", "MCP light");

    std::shared_ptr<erhe::scene::Node>  node;
    std::shared_ptr<erhe::scene::Light> light;
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{sr->item_host_mutex};
        node  = std::make_shared<erhe::scene::Node>(name);
        light = std::make_shared<erhe::scene::Light>(name);
        light->type        = type;
        light->color       = color;
        light->intensity   = intensity;
        light->range       = range;
        light->cast_shadow = cast_shadow;
        light->layer_id    = sr->layers().light()->id;
        if (args.contains("inner_spot_angle")) { light->inner_spot_angle = args.value("inner_spot_angle", light->inner_spot_angle); }
        if (args.contains("outer_spot_angle")) { light->outer_spot_angle = args.value("outer_spot_angle", light->outer_spot_angle); }
        light->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui | erhe::Item_flags::show_debug_visualizations);
        node->attach(light);
        node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
        // The node is attached to the scene via the queued insert operation below;
        // set the world transform now (preserved by Node::set_parent).
        node->set_world_from_node(erhe::math::create_translation<float>(position));
    }

    // Insert the light node into the scene root via an undoable operation that
    // runs on the next editor frame (mirrors Scene_builder::add_lights).
    const std::shared_ptr<erhe::scene::Node>& root_node = sr->get_scene().get_root_node();
    m_context.operation_stack->queue(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = node,
                .parent  = root_node,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );

    return make_json_content({
        {"light_name",  light->get_name()},
        {"light_id",    light->get_id()},
        {"node_name",   node->get_name()},
        {"node_id",     node->get_id()},
        {"type",        type_str},
        {"color",       {color.x, color.y, color.z}},
        {"intensity",   intensity},
        {"range",       range},
        {"cast_shadow", cast_shadow},
        {"position",    {position.x, position.y, position.z}},
        {"queued",      true} // the insert operation executes on the next editor frame
    }).dump();
}

auto Mcp_server::action_add_node_attachment(const json& args) -> std::string
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
    const std::string type_key = args.value("type", "");
    if (type_key.empty()) {
        return make_error_content("Missing 'type' (attachment catalog key)");
    }
    const Attachment_type_info* info = find_attachment_type(type_key);
    if (info == nullptr) {
        return make_error_content("Unknown attachment type: " + type_key);
    }
    if (!info->can_add(*node)) {
        return make_error_content(
            "Cannot add attachment '" + type_key + "' to node '" + node->get_name() +
            "' (duplicate, or precondition not met)"
        );
    }
    info->make(*m_context.scene_commands, *node);
    return make_json_content({
        {"added",   true},
        {"queued",  true}, // the attach operation executes on the next editor frame
        {"node",    node->get_name()},
        {"node_id", node->get_id()},
        {"type",    type_key}
    }).dump();
}

auto Mcp_server::action_remove_node_attachment(const json& args) -> std::string
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
    const std::size_t attachment_id = args.value("attachment_id", std::size_t{0});
    const std::string type_key      = args.value("type", "");
    if ((attachment_id == 0) && type_key.empty()) {
        return make_error_content("Give attachment_id or type to identify the attachment to remove");
    }

    // Match by attachment_id, or by attachment type name (as reported by
    // get_node_details, e.g. "Camera", "Node_physics"), case-insensitively.
    auto iequals = [](std::string_view a, std::string_view b) -> bool {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    };

    std::shared_ptr<erhe::scene::Node_attachment> target;
    for (const std::shared_ptr<erhe::scene::Node_attachment>& att : node->get_attachments()) {
        const bool match = (attachment_id != 0)
            ? (att->get_id() == attachment_id)
            : iequals(type_key, att->get_type_name());
        if (match) {
            target = att;
            break;
        }
    }
    if (!target) {
        return make_error_content("No matching attachment on node '" + node->get_name() + "'");
    }

    const std::string removed_type = std::string{target->get_type_name()};
    const std::size_t removed_id   = target->get_id();
    m_context.scene_commands->remove_attachment(target);
    return make_json_content({
        {"removed",       true},
        {"queued",        true}, // the detach operation executes on the next editor frame
        {"node",          node->get_name()},
        {"node_id",       node->get_id()},
        {"attachment_id", removed_id},
        {"type",          removed_type}
    }).dump();
}

auto Mcp_server::action_edit_light(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    // Accept light_id / light_name, and also bare id / name for convenience.
    std::shared_ptr<erhe::scene::Light> light = find_light_in_scene(*sr, args, "light_id", "light_name");
    if (!light) {
        light = find_light_in_scene(*sr, args, "id", "name");
    }
    if (!light) {
        json r = make_text_content("Light not found (specify light_id or light_name)");
        r["isError"] = true;
        return r.dump();
    }

    auto read_vec3 = [&args](const char* key, glm::vec3& out_value) -> bool {
        const json value = args.value(key, json());
        if (value.is_array() && (value.size() == 3)) {
            out_value = glm::vec3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
            return true;
        }
        return false;
    };

    json changed = json::object();
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{sr->item_host_mutex};

        if (args.contains("type")) {
            const std::string type_str = args.value("type", "");
            if ((type_str != "directional") && (type_str != "point") && (type_str != "spot")) {
                json r = make_text_content("Invalid light type '" + type_str + "' (expected directional, point or spot)");
                r["isError"] = true;
                return r.dump();
            }
            // Assigning Light::type re-buckets the light for rendering (forward
            // variant + shadow technique). Other type-dependent fields (e.g.
            // range) are left exactly as provided by the caller.
            light->type = parse_light_type(type_str, light->type);
            changed["type"] = type_str;
        }
        glm::vec3 color{};
        if (read_vec3("color", color)) {
            light->color = color;
            changed["color"] = {color.x, color.y, color.z};
        }
        if (args.contains("intensity")) {
            light->intensity = args.value("intensity", light->intensity);
            changed["intensity"] = light->intensity;
        }
        if (args.contains("range")) {
            light->range = args.value("range", light->range);
            changed["range"] = light->range;
        }
        if (args.contains("cast_shadow")) {
            light->cast_shadow = args.value("cast_shadow", light->cast_shadow);
            changed["cast_shadow"] = light->cast_shadow;
        }
        if (args.contains("inner_spot_angle")) {
            light->inner_spot_angle = args.value("inner_spot_angle", light->inner_spot_angle);
            changed["inner_spot_angle"] = light->inner_spot_angle;
        }
        if (args.contains("outer_spot_angle")) {
            light->outer_spot_angle = args.value("outer_spot_angle", light->outer_spot_angle);
            changed["outer_spot_angle"] = light->outer_spot_angle;
        }
        glm::vec3 position{};
        if (read_vec3("position", position)) {
            erhe::scene::Node* node = light->get_node();
            if (node != nullptr) {
                node->set_world_from_node(erhe::math::create_translation<float>(position));
                changed["position"] = {position.x, position.y, position.z};
            } else {
                changed["position_error"] = "light has no node";
            }
        }
    }

    return make_json_content({
        {"light_id",   light->get_id()},
        {"light_name", light->get_name()},
        {"changed",    changed}
    }).dump();
}

auto Mcp_server::action_edit_camera(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const std::size_t camera_id   = args.contains("camera_id") ? args.value("camera_id", std::size_t{0}) : args.value("id", std::size_t{0});
    const std::string camera_name = args.contains("camera_name") ? args.value("camera_name", "") : args.value("name", "");
    std::shared_ptr<erhe::scene::Camera> camera{};
    for (const std::shared_ptr<erhe::scene::Camera>& candidate : sr->get_scene().get_cameras()) {
        if ((camera_id != 0) ? (candidate->get_id() == camera_id) : (!camera_name.empty() && (candidate->get_name() == camera_name))) {
            camera = candidate;
            break;
        }
    }
    if (!camera) {
        json r = make_text_content("Camera not found (specify camera_id or camera_name)");
        r["isError"] = true;
        return r.dump();
    }

    json changed = json::object();
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{sr->item_host_mutex};
        if (args.contains("exposure")) {
            camera->set_exposure(args.value("exposure", camera->get_exposure()));
            changed["exposure"] = camera->get_exposure();
        }
        if (args.contains("shadow_range")) {
            camera->set_shadow_range(args.value("shadow_range", camera->get_shadow_range()));
            changed["shadow_range"] = camera->get_shadow_range();
        }
        if (args.contains("fov_y")) {
            erhe::scene::Projection* projection = camera->projection();
            if (projection != nullptr) {
                projection->fov_y = args.value("fov_y", projection->fov_y);
                changed["fov_y"] = projection->fov_y;
            }
        }
    }

    return make_json_content({
        {"camera_id",   camera->get_id()},
        {"camera_name", camera->get_name()},
        {"changed",     changed}
    }).dump();
}

auto Mcp_server::action_toggle_physics(const json& args) -> std::string
{
    static_cast<void>(args);

    if (!m_context.app_settings) {
        json r = make_text_content("Settings not available");
        r["isError"] = true;
        return r.dump();
    }

    m_context.editor_settings->physics.dynamic_enable = !m_context.editor_settings->physics.dynamic_enable;
    const bool enabled = m_context.editor_settings->physics.dynamic_enable;

    return make_json_content({
        {"dynamic_physics_enabled", enabled}
    }).dump();
}

auto Mcp_server::action_reparent_node(const json& args) -> std::string
{
    const std::string scene_name    = args.value("scene_name", "");
    const std::size_t node_id       = args.value("node_id", std::size_t{0});
    const std::size_t parent_node_id = args.value("parent_node_id", std::size_t{0});

    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    erhe::scene::Scene& scene = sr->get_scene();

    // Find child node
    std::shared_ptr<erhe::scene::Node> child_node;
    for (const std::shared_ptr<erhe::scene::Node>& node : scene.get_flat_nodes()) {
        if (node->get_id() == node_id) {
            child_node = node;
            break;
        }
    }
    if (!child_node) {
        json r = make_text_content("Node not found: " + std::to_string(node_id));
        r["isError"] = true;
        return r.dump();
    }

    // Find parent node (0 means scene root)
    std::shared_ptr<erhe::scene::Node> new_parent;
    if (parent_node_id == 0) {
        new_parent = scene.get_root_node();
    } else {
        for (const std::shared_ptr<erhe::scene::Node>& node : scene.get_flat_nodes()) {
            if (node->get_id() == parent_node_id) {
                new_parent = node;
                break;
            }
        }
    }
    if (!new_parent) {
        json r = make_text_content("Parent node not found: " + std::to_string(parent_node_id));
        r["isError"] = true;
        return r.dump();
    }

    std::shared_ptr<Operation> op = std::make_shared<Item_parent_change_operation>(
        new_parent,
        child_node,
        std::shared_ptr<erhe::Hierarchy>{},
        std::shared_ptr<erhe::Hierarchy>{}
    );
    m_context.operation_stack->queue(op);

    return make_json_content({
        {"node",   child_node->get_name()},
        {"parent", new_parent->get_name()}
    }).dump();
}

auto Mcp_server::action_lock_items(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const json ids_json = args.value("ids", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& v : ids_json) {
        if (v.is_number()) target_ids.insert(v.get<std::size_t>());
    }
    auto items = find_items_by_ids(*sr, target_ids);
    for (auto& item : items) {
        item->set_lock_edit(true);
    }
    return make_json_content({{"locked_count", static_cast<int>(items.size())}}).dump();
}

auto Mcp_server::action_unlock_items(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const json ids_json = args.value("ids", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& v : ids_json) {
        if (v.is_number()) target_ids.insert(v.get<std::size_t>());
    }
    auto items = find_items_by_ids(*sr, target_ids);
    for (auto& item : items) {
        item->set_lock_edit(false);
    }
    return make_json_content({{"unlocked_count", static_cast<int>(items.size())}}).dump();
}

auto Mcp_server::action_add_tags(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const json ids_json  = args.value("ids", json::array());
    const json tags_json = args.value("tags", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& v : ids_json) {
        if (v.is_number()) target_ids.insert(v.get<std::size_t>());
    }
    std::vector<std::string> tags;
    for (const auto& v : tags_json) {
        if (v.is_string()) tags.push_back(v.get<std::string>());
    }
    auto items = find_items_by_ids(*sr, target_ids);
    for (auto& item : items) {
        for (const auto& tag : tags) {
            item->add_tag(tag);
        }
    }
    return make_json_content({{"tagged_count", static_cast<int>(items.size())}}).dump();
}

auto Mcp_server::action_remove_tags(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const json ids_json  = args.value("ids", json::array());
    const json tags_json = args.value("tags", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& v : ids_json) {
        if (v.is_number()) target_ids.insert(v.get<std::size_t>());
    }
    std::vector<std::string> tags;
    for (const auto& v : tags_json) {
        if (v.is_string()) tags.push_back(v.get<std::string>());
    }
    auto items = find_items_by_ids(*sr, target_ids);
    for (auto& item : items) {
        for (const auto& tag : tags) {
            item->remove_tag(tag);
        }
    }
    return make_json_content({{"untagged_count", static_cast<int>(items.size())}}).dump();
}


} // namespace editor
