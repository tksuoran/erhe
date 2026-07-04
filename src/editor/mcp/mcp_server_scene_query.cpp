// Mcp_server scene query tools (scenes, nodes, cameras, lights, materials, textures, brushes, selection, undo/redo, async status).
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server_shared.hpp"

namespace editor {

using namespace mcp_server_detail;

// --- Query implementations ---

auto Mcp_server::query_list_scenes(const json& args) -> std::string
{
    static_cast<void>(args);

    if (!m_context.app_scenes) {
        return make_text_content("No scenes available").dump();
    }

    json scenes = json::array();
    for (const auto& sr : m_context.app_scenes->get_scene_roots()) {
        const auto& scene = sr->get_scene();
        const auto  library = sr->get_content_library();

        int material_count = 0;
        if (library && library->materials) {
            material_count = static_cast<int>(library->materials->get_all<erhe::primitive::Material>().size());
        }

        int light_count = 0;
        for (const auto& ll : scene.get_light_layers()) {
            light_count += static_cast<int>(ll->lights.size());
        }

        scenes.push_back({
            {"name",                sr->get_name()},
            {"id",                  scene.get_id()}, // Scene item id (selectable, issue #240)
            {"node_count",          static_cast<int>(scene.get_flat_nodes().size())},
            {"camera_count",        static_cast<int>(scene.get_cameras().size())},
            {"light_count",         light_count},
            {"material_count",      material_count},
            {"trigger_event_count", sr->get_trigger_event_count()}
        });
    }

    return make_json_content({{"scenes", scenes}}).dump();
}

auto Mcp_server::query_scene_nodes(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const auto& scene = sr->get_scene();
    json nodes = json::array();
    for (const auto& node : scene.get_flat_nodes()) {
        const auto& trs = node->parent_from_node_transform();
        const glm::vec3 t = trs.get_translation();
        const glm::quat r = trs.get_rotation();
        const glm::vec3 s = trs.get_scale();

        json attachment_types = json::array();
        for (const auto& att : node->get_attachments()) {
            attachment_types.push_back(std::string{att->get_type_name()});
        }

        auto parent_node = node->get_parent_node();

        json tags_arr = json::array();
        for (const auto& tag : node->get_tags()) {
            tags_arr.push_back(tag);
        }

        nodes.push_back({
            {"name",             node->get_name()},
            {"id",               node->get_id()},
            {"parent",           parent_node ? parent_node->get_name() : ""},
            {"position",         {t.x, t.y, t.z}},
            {"rotation_xyzw",    {r.x, r.y, r.z, r.w}},
            {"scale",            {s.x, s.y, s.z}},
            {"attachment_types", attachment_types},
            {"locked",           node->is_lock_edit()},
            {"tags",             tags_arr}
        });
    }

    return make_json_content({{"nodes", nodes}}).dump();
}

auto Mcp_server::query_node_details(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    const std::string node_name  = args.value("node_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const auto& scene = sr->get_scene();
    std::shared_ptr<erhe::scene::Node> found_node;
    for (const auto& node : scene.get_flat_nodes()) {
        if (node->get_name() == node_name) {
            found_node = node;
            break;
        }
    }
    if (!found_node) {
        json r = make_text_content("Node not found: " + node_name);
        r["isError"] = true;
        return r.dump();
    }

    const auto& trs = found_node->parent_from_node_transform();
    const glm::vec3 t = trs.get_translation();
    const glm::quat r = trs.get_rotation();
    const glm::vec3 s = trs.get_scale();
    const glm::vec3 k = trs.get_skew();
    const auto& world_trs = found_node->world_from_node_transform();
    const glm::vec3 wt = world_trs.get_translation();
    const glm::quat wr = world_trs.get_rotation();
    const glm::vec3 ws = world_trs.get_scale();
    const glm::vec3 wk = world_trs.get_skew();
    const glm::vec4 wp = found_node->position_in_world();

    json attachments = json::array();
    for (const auto& att : found_node->get_attachments()) {
        json att_json = {
            {"type", std::string{att->get_type_name()}},
            {"name", att->get_name()}
        };

        auto mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(att);
        if (mesh) {
            att_json["primitive_count"] = static_cast<int>(mesh->get_primitives().size());
            json mat_names = json::array();
            int total_vertices = 0;
            int total_facets = 0;
            for (const auto& prim : mesh->get_primitives()) {
                mat_names.push_back(prim.material ? prim.material->get_name() : "(none)");
                if (prim.primitive && prim.primitive->render_shape) {
                    const auto& geom = prim.primitive->render_shape->get_geometry_const();
                    if (geom) {
                        total_vertices += static_cast<int>(geom->get_mesh().vertices.nb());
                        total_facets   += static_cast<int>(geom->get_mesh().facets.nb());
                    }
                }
            }
            att_json["materials"]     = mat_names;
            att_json["vertex_count"]  = total_vertices;
            att_json["facet_count"]   = total_facets;
        }

        auto geometry_graph_mesh = std::dynamic_pointer_cast<Geometry_graph_mesh>(att);
        if (geometry_graph_mesh) {
            const std::shared_ptr<Graph_mesh>& graph_mesh = geometry_graph_mesh->get_graph_mesh();
            att_json["graph_mesh"]    = graph_mesh ? graph_mesh->get_name() : "";
            att_json["graph_mesh_id"] = graph_mesh ? json(graph_mesh->get_id()) : json(nullptr);
        }

        auto camera = std::dynamic_pointer_cast<erhe::scene::Camera>(att);
        if (camera) {
            att_json["exposure"]     = camera->get_exposure();
            att_json["shadow_range"] = camera->get_shadow_range();
        }

        auto light = std::dynamic_pointer_cast<erhe::scene::Light>(att);
        if (light) {
            const char* type_str = (light->type == erhe::scene::Light_type::directional) ? "directional"
                                 : (light->type == erhe::scene::Light_type::point)       ? "point"
                                 : (light->type == erhe::scene::Light_type::spot)         ? "spot"
                                 : "unknown";
            att_json["light_type"] = type_str;
            att_json["color"]      = {light->color.x, light->color.y, light->color.z};
            att_json["intensity"]  = light->intensity;
            att_json["range"]      = light->range;
        }

        auto bp = std::dynamic_pointer_cast<Brush_placement>(att);
        if (bp) {
            auto brush = bp->get_brush();
            if (brush) {
                att_json["brush_name"] = brush->get_name();
                att_json["brush_id"]   = brush->get_id();
            }
        }

        auto node_physics = std::dynamic_pointer_cast<Node_physics>(att);
        if (node_physics) {
            att_json["motion_mode"]    = motion_mode_to_string(node_physics->get_motion_mode());
            att_json["is_trigger"]     = node_physics->is_trigger();
            att_json["gravity_factor"] = node_physics->get_gravity_factor();
            const std::shared_ptr<erhe::physics::ICollision_shape>& shape = node_physics->get_collision_shape();
            att_json["collision_shape"] = shape ? shape->describe() : "";
            const std::shared_ptr<erhe::physics::Physics_material>& physics_material = node_physics->get_physics_material();
            att_json["physics_material"] = physics_material ? physics_material->get_name() : "";
            const std::shared_ptr<erhe::physics::Collision_filter>& collision_filter = node_physics->get_collision_filter();
            att_json["collision_filter"] = collision_filter ? collision_filter->get_name() : "";
            const erhe::physics::IRigid_body* rigid_body = node_physics->get_rigid_body();
            if (rigid_body != nullptr) {
                att_json["mass"]        = rigid_body->get_mass();
                att_json["friction"]    = rigid_body->get_friction();
                att_json["restitution"] = rigid_body->get_restitution();
                att_json["is_active"]   = rigid_body->is_active();
            }
        }

        auto node_joint = std::dynamic_pointer_cast<Node_joint>(att);
        if (node_joint) {
            const std::shared_ptr<erhe::scene::Node> connected = node_joint->get_connected_node();
            att_json["connected_node"]   = connected ? connected->get_name() : "(world)";
            const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings = node_joint->get_settings();
            att_json["joint_settings"]   = settings ? settings->get_name() : "";
            att_json["enable_collision"] = node_joint->get_enable_collision();
            att_json["constraint"]       = (node_joint->get_constraint() != nullptr) ? "created" : "pending";
        }

        attachments.push_back(att_json);
    }

    json children = json::array();
    for (const auto& child : found_node->get_children()) {
        children.push_back(child->get_name());
    }

    auto parent_node = found_node->get_parent_node();

    json result = {
        {"name",           found_node->get_name()},
        {"id",             found_node->get_id()},
        {"parent",         parent_node ? parent_node->get_name() : ""},
        {"world_position", {wp.x, wp.y, wp.z}},
        {"local_transform", {
            {"translation",   {t.x, t.y, t.z}},
            {"rotation_xyzw", {r.x, r.y, r.z, r.w}},
            {"scale",         {s.x, s.y, s.z}},
            {"skew",          {k.x, k.y, k.z}}
        }},
        {"world_transform", {
            {"translation",   {wt.x, wt.y, wt.z}},
            {"rotation_xyzw", {wr.x, wr.y, wr.z, wr.w}},
            {"scale",         {ws.x, ws.y, ws.z}},
            {"skew",          {wk.x, wk.y, wk.z}}
        }},
        {"attachments",    attachments},
        {"children",       children},
        {"visible",        found_node->is_visible()},
        {"selected",       found_node->is_selected()},
        {"locked",         found_node->is_lock_edit()},
        {"tags",           [&]() { json t = json::array(); for (const auto& tag : found_node->get_tags()) t.push_back(tag); return t; }()}
    };

    return make_json_content(result).dump();
}

auto Mcp_server::query_scene_cameras(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    json cameras = json::array();
    for (const auto& camera : sr->get_scene().get_cameras()) {
        const auto* node = camera->get_node();
        const erhe::scene::Projection* projection = camera->projection();
        cameras.push_back({
            {"name",         camera->get_name()},
            {"id",           camera->get_id()},
            {"node",         node ? node->get_name() : ""},
            {"exposure",     camera->get_exposure()},
            {"shadow_range", camera->get_shadow_range()},
            {"fov_y",        (projection != nullptr) ? projection->fov_y : 0.0f}
        });
    }

    return make_json_content({{"cameras", cameras}}).dump();
}

auto Mcp_server::query_scene_lights(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    json lights = json::array();
    for (const auto& ll : sr->get_scene().get_light_layers()) {
        for (const auto& light : ll->lights) {
            const auto* node = light->get_node();
            const char* type_str = (light->type == erhe::scene::Light_type::directional) ? "directional"
                                 : (light->type == erhe::scene::Light_type::point)       ? "point"
                                 : (light->type == erhe::scene::Light_type::spot)         ? "spot"
                                 : "unknown";
            lights.push_back({
                {"name",      light->get_name()},
                {"id",        light->get_id()},
                {"node",      node ? node->get_name() : ""},
                {"type",      type_str},
                {"color",     {light->color.x, light->color.y, light->color.z}},
                {"intensity", light->intensity},
                {"range",     light->range}
            });
        }
    }

    return make_json_content({{"lights", lights}}).dump();
}

auto Mcp_server::query_shadow_fit_debug(const json& args) -> std::string
{
    static_cast<void>(args);
    if (m_context.app_rendering == nullptr) {
        json r = make_text_content("App_rendering not available");
        r["isError"] = true;
        return r.dump();
    }

    const std::vector<std::shared_ptr<Shadow_render_node>>& shadow_nodes = m_context.app_rendering->get_all_shadow_nodes();
    json nodes = json::array();
    for (std::size_t i = 0; i < shadow_nodes.size(); ++i) {
        const std::shared_ptr<Shadow_render_node>& node = shadow_nodes[i];
        if (!node) {
            continue;
        }
        json node_json = dump_shadow_fit_debug(node->get_light_projections());
        node_json["shadow_node_index"] = i;
        nodes.push_back(node_json);
    }

    return make_json_content({{"shadow_nodes", nodes}}).dump();
}

auto Mcp_server::query_scene_materials(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    auto library = sr->get_content_library();
    if (!library || !library->materials) {
        return make_json_content({{"materials", json::array()}}).dump();
    }

    json materials = json::array();
    const auto& mat_list = library->materials->get_all<erhe::primitive::Material>();
    for (const auto& mat : mat_list) {
        materials.push_back({
            {"name",       mat->get_name()},
            {"id",         mat->get_id()},
            {"base_color", {mat->data.base_color.x, mat->data.base_color.y, mat->data.base_color.z}},
            {"metallic",   mat->data.metallic},
            {"roughness",  mat->data.roughness.x},
            {"emissive",   {mat->data.emissive.x, mat->data.emissive.y, mat->data.emissive.z}}
        });
    }

    return make_json_content({{"materials", materials}}).dump();
}

auto Mcp_server::query_server_info(const json& args) -> std::string
{
    static_cast<void>(args);
    return make_json_content({
        {"name",    "erhe-editor"},
        {"version", "0.2.0"},
        {"pid",     get_process_id()},
        {"build",   c_mcp_build_timestamp},
        {"port",    m_port}
    }).dump();
}

auto Mcp_server::query_material_details(const json& args) -> std::string
{
    const std::string scene_name    = args.value("scene_name", "");
    const std::string material_name = args.value("material_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    auto library = sr->get_content_library();
    if (!library || !library->materials) {
        json r = make_text_content("No materials in scene: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const auto& mat_list = library->materials->get_all<erhe::primitive::Material>();
    for (const auto& mat : mat_list) {
        if (mat->get_name() == material_name) {
            const auto& d = mat->data;
            auto sampler_to_json = [](const erhe::primitive::Material_texture_sampler& s) -> json {
                json entry = {
                    {"tex_coord", s.tex_coord},
                    {"rotation",  s.rotation},
                    {"offset",    {s.offset.x, s.offset.y}},
                    {"scale",     {s.scale.x,  s.scale.y}}
                };
                if (s.texture) {
                    entry["texture_id"]   = s.texture->get_id();
                    entry["texture_name"] = s.texture->get_name();
                } else {
                    entry["texture_id"]   = nullptr;
                    entry["texture_name"] = nullptr;
                }
                // The graph-texture source, when the slot is bound to a
                // Graph_texture asset (the material -> graph back-reference).
                const Graph_texture* graph_texture = dynamic_cast<const Graph_texture*>(s.texture_source.get());
                if (graph_texture != nullptr) {
                    entry["graph_texture_id"]   = graph_texture->get_id();
                    entry["graph_texture_name"] = graph_texture->get_name();
                } else {
                    entry["graph_texture_id"]   = nullptr;
                    entry["graph_texture_name"] = nullptr;
                }
                return entry;
            };
            json result = {
                {"name",                       mat->get_name()},
                {"id",                         mat->get_id()},
                {"base_color",                 {d.base_color.x, d.base_color.y, d.base_color.z}},
                {"opacity",                    d.opacity},
                {"roughness",                  {d.roughness.x, d.roughness.y}},
                {"metallic",                   d.metallic},
                {"reflectance",                d.reflectance},
                {"emissive",                   {d.emissive.x, d.emissive.y, d.emissive.z}},
                {"normal_texture_scale",       d.normal_texture_scale},
                {"occlusion_texture_strength", d.occlusion_texture_strength},
                {"bxdf_model",
                    (d.bxdf_model == erhe::primitive::Bxdf_model::unlit)                    ? "unlit" :
                    (d.bxdf_model == erhe::primitive::Bxdf_model::anisotropic_brdf)         ? "anisotropic_brdf" :
                    (d.bxdf_model == erhe::primitive::Bxdf_model::anisotropic_slope)        ? "anisotropic_slope" :
                    (d.bxdf_model == erhe::primitive::Bxdf_model::anisotropic_engine_ready) ? "anisotropic_engine_ready" :
                                                                                              "isotropic_brdf"},
                {"use_circular_brushed_metal", d.use_circular_brushed_metal},
                {"use_aniso_control",          d.use_aniso_control},
                {"texture_samplers", {
                    {"base_color",         sampler_to_json(d.texture_samplers.base_color)},
                    {"metallic_roughness", sampler_to_json(d.texture_samplers.metallic_roughness)},
                    {"normal",             sampler_to_json(d.texture_samplers.normal)},
                    {"occlusion",          sampler_to_json(d.texture_samplers.occlusion)},
                    {"emissive",           sampler_to_json(d.texture_samplers.emissive)}
                }}
            };
            return make_json_content(result).dump();
        }
    }

    json r = make_text_content("Material not found: " + material_name);
    r["isError"] = true;
    return r.dump();
}

auto Mcp_server::query_scene_textures(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    auto library = sr->get_content_library();
    if (!library || !library->textures) {
        return make_json_content({{"textures", json::array()}}).dump();
    }

    json textures = json::array();
    const auto& tex_list = library->textures->get_all<erhe::graphics::Texture>();
    for (const auto& tex : tex_list) {
        textures.push_back({
            {"name",   tex->get_name()},
            {"id",     tex->get_id()},
            {"width",  tex->get_width()},
            {"height", tex->get_height()},
            {"format", erhe::dataformat::c_str(tex->get_pixelformat())}
        });
    }

    return make_json_content({{"textures", textures}}).dump();
}

auto Mcp_server::query_scene_brushes(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    auto library = sr->get_content_library();
    if (!library || !library->brushes) {
        return make_json_content({{"brushes", json::array()}}).dump();
    }

    json brushes = json::array();
    const auto& brush_list = library->brushes->get_all<Brush>();
    for (const auto& brush : brush_list) {
        auto geometry = brush->get_geometry();
        const GEO::index_t vertex_count = geometry ? geometry->get_mesh().vertices.nb() : 0;
        const GEO::index_t facet_count  = geometry ? geometry->get_mesh().facets.nb()   : 0;
        brushes.push_back({
            {"name",         brush->get_name()},
            {"id",           brush->get_id()},
            {"vertex_count", vertex_count},
            {"facet_count",  facet_count}
        });
    }

    return make_json_content({{"brushes", brushes}}).dump();
}

auto Mcp_server::query_selection(const json& args) -> std::string
{
    static_cast<void>(args);

    if (!m_context.selection) {
        return make_json_content({{"items", json::array()}}).dump();
    }

    json items = json::array();
    for (const auto& item : m_context.selection->get_selected_items()) {
        items.push_back({
            {"name",      item->get_name()},
            {"type",      std::string{item->get_type_name()}},
            {"id",        item->get_id()}
        });
    }

    json result = {{"items", items}};
    if (m_context.transform_tool != nullptr) {
        result["transform_reference_mode"] = transform_reference_mode_lc(m_context.transform_tool->shared.settings.reference_mode);
    }
    if (m_context.mesh_component_selection != nullptr) {
        result["mesh_component_mode"] = mesh_component_mode_lc(m_context.mesh_component_selection->get_mode());
    }
    if (m_context.editor_settings != nullptr) {
        result["transform_mode"] = std::string{::to_string(m_context.editor_settings->transform_mode)};
    }
    return make_json_content(result).dump();
}

auto Mcp_server::query_undo_redo_stack(const json& args) -> std::string
{
    static_cast<void>(args);

    if (!m_context.operation_stack) {
        return make_json_content({{"undo", json::array()}, {"redo", json::array()}, {"can_undo", false}, {"can_redo", false}}).dump();
    }

    auto make_stack = [](const std::vector<std::shared_ptr<Operation>>& ops) -> json {
        json arr = json::array();
        for (const auto& op : ops) {
            json entry = {{"description", op->describe()}};
            if (op->has_error()) {
                entry["error"] = op->get_error();
            }
            arr.push_back(entry);
        }
        return arr;
    };

    return make_json_content({
        {"undo",     make_stack(m_context.operation_stack->get_undo_stack())},
        {"redo",     make_stack(m_context.operation_stack->get_redo_stack())},
        {"can_undo", m_context.operation_stack->can_undo()},
        {"can_redo", m_context.operation_stack->can_redo()}
    }).dump();
}

auto Mcp_server::query_async_status(const json& args) -> std::string
{
    static_cast<void>(args);

    return make_json_content({
        {"pending", m_context.pending_async_ops.load()},
        {"running", m_context.running_async_ops.load()}
    }).dump();
}

auto Mcp_server::find_items_by_ids(Scene_root& sr, const std::set<std::size_t>& target_ids) -> std::vector<std::shared_ptr<erhe::Item_base>>
{
    std::vector<std::shared_ptr<erhe::Item_base>> result;

    const auto& scene = sr.get_scene();
    // The Scene item itself is selectable (issue #240); match it too so it can
    // be selected via MCP (e.g. to show Scene properties in the Properties window).
    const std::shared_ptr<erhe::scene::Scene> scene_item = sr.get_scene_item();
    if (scene_item && target_ids.contains(scene_item->get_id())) {
        result.push_back(scene_item);
    }
    for (const auto& node : scene.get_flat_nodes()) {
        if (target_ids.contains(node->get_id())) {
            result.push_back(node);
        }
    }
    for (const auto& camera : scene.get_cameras()) {
        if (target_ids.contains(camera->get_id())) {
            result.push_back(camera);
        }
    }
    for (const auto& ll : scene.get_light_layers()) {
        for (const auto& light : ll->lights) {
            if (target_ids.contains(light->get_id())) {
                result.push_back(light);
            }
        }
    }
    auto library = sr.get_content_library();
    if (library) {
        if (library->materials) {
            for (const auto& mat : library->materials->get_all<erhe::primitive::Material>()) {
                if (target_ids.contains(mat->get_id())) {
                    result.push_back(mat);
                }
            }
        }
        if (library->brushes) {
            for (const auto& brush : library->brushes->get_all<Brush>()) {
                if (target_ids.contains(brush->get_id())) {
                    result.push_back(brush);
                }
            }
        }
        if (library->graph_textures) {
            for (const auto& graph_texture : library->graph_textures->get_all<Graph_texture>()) {
                if (target_ids.contains(graph_texture->get_id())) {
                    result.push_back(graph_texture);
                }
            }
        }
        if (library->graph_meshes) {
            for (const auto& graph_mesh : library->graph_meshes->get_all<Graph_mesh>()) {
                if (target_ids.contains(graph_mesh->get_id())) {
                    result.push_back(graph_mesh);
                }
            }
        }
    }
    return result;
}


} // namespace editor
