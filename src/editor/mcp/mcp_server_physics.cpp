// Mcp_server physics tools (bodies, joints, materials, collision filters, joint settings).
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server_shared.hpp"

namespace editor {

using namespace mcp_server_detail;

auto Mcp_server::action_wake_physics_bodies(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    // Bodies enter the world deactivated (quiet scene loading); wake the
    // dynamic ones the same way Node_joint does after constraint creation.
    std::size_t woken = 0;
    for (const std::shared_ptr<erhe::scene::Node>& node : sr->get_scene().get_flat_nodes()) {
        const std::shared_ptr<Node_physics> node_physics = erhe::scene::get_attachment<Node_physics>(node.get());
        if (!node_physics) {
            continue;
        }
        erhe::physics::IRigid_body* rigid_body = node_physics->get_rigid_body();
        if (rigid_body == nullptr) {
            continue;
        }
        if (rigid_body->get_motion_mode() != erhe::physics::Motion_mode::e_dynamic) {
            continue;
        }
        rigid_body->begin_move();
        rigid_body->end_move();
        ++woken;
    }
    return make_json_content({
        {"woken", woken}
    }).dump();
}

auto Mcp_server::query_physics_items(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<Content_library> library = sr->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library: " + scene_name);
    }

    json materials = json::array();
    for (const std::shared_ptr<erhe::physics::Physics_material>& material : library->physics_materials->get_all<erhe::physics::Physics_material>()) {
        materials.push_back({
            {"name",                material->get_name()},
            {"id",                  material->get_id()},
            {"static_friction",     material->static_friction},
            {"dynamic_friction",    material->dynamic_friction},
            {"restitution",         material->restitution},
            {"friction_combine",    combine_mode_to_string(material->friction_combine)},
            {"restitution_combine", combine_mode_to_string(material->restitution_combine)}
        });
    }
    json filters = json::array();
    for (const std::shared_ptr<erhe::physics::Collision_filter>& filter : library->collision_filters->get_all<erhe::physics::Collision_filter>()) {
        filters.push_back({
            {"name",                      filter->get_name()},
            {"id",                        filter->get_id()},
            {"collision_systems",         filter->collision_systems},
            {"collide_with_systems",      filter->collide_with_systems},
            {"not_collide_with_systems",  filter->not_collide_with_systems}
        });
    }
    json joint_settings = json::array();
    for (const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings : library->physics_joints->get_all<erhe::physics::Physics_joint_settings>()) {
        joint_settings.push_back(joint_settings_to_json(*settings));
    }
    return make_json_content({
        {"physics_materials",      materials},
        {"collision_filters",      filters},
        {"physics_joint_settings", joint_settings}
    }).dump();
}

auto Mcp_server::action_create_physics_body(const json& args) -> std::string
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
    if (erhe::scene::get_attachment<Node_physics>(node.get())) {
        return make_error_content("Node already has a rigid body: " + node->get_name());
    }
    const std::shared_ptr<Content_library> library = sr->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library: " + scene_name);
    }

    erhe::physics::IRigid_body_create_info create_info{};
    create_info.motion_mode = parse_motion_mode(args.value("motion_mode", "dynamic"), erhe::physics::Motion_mode::e_dynamic);

    std::string shape_error;
    create_info.collision_shape = build_collision_shape_from_args(args, node.get(), shape_error);
    if (!create_info.collision_shape) {
        return make_error_content(shape_error);
    }
    if ((args.value("shape", "auto") == "mesh") && (create_info.motion_mode == erhe::physics::Motion_mode::e_dynamic)) {
        return make_error_content("Triangle mesh shapes are static / kinematic only (Jolt restriction); use convex_hull for dynamic bodies");
    }

    if (args.contains("mass")) {
        create_info.mass = args["mass"].get<float>();
    }
    create_info.friction         = args.value("friction",        create_info.friction);
    create_info.restitution      = args.value("restitution",     create_info.restitution);
    create_info.linear_damping   = args.value("linear_damping",  create_info.linear_damping);
    create_info.angular_damping  = args.value("angular_damping", create_info.angular_damping);
    create_info.gravity_factor   = args.value("gravity_factor",  create_info.gravity_factor);
    create_info.is_sensor        = args.value("is_trigger",      false);
    create_info.linear_velocity  = get_vec3(args, "linear_velocity",  create_info.linear_velocity);
    create_info.angular_velocity = get_vec3(args, "angular_velocity", create_info.angular_velocity);

    const std::string material_name = args.value("material_name", "");
    if (!material_name.empty()) {
        create_info.physics_material = find_library_item<erhe::physics::Physics_material>(library->physics_materials, material_name);
        if (!create_info.physics_material) {
            return make_error_content("Physics material not found: " + material_name);
        }
    }
    const std::string filter_name = args.value("filter_name", "");
    if (!filter_name.empty()) {
        create_info.collision_filter = find_library_item<erhe::physics::Collision_filter>(library->collision_filters, filter_name);
        if (!create_info.collision_filter) {
            return make_error_content("Collision filter not found: " + filter_name);
        }
    }

    const glm::vec3 center_of_mass = get_vec3(args, "center_of_mass", glm::vec3{0.0f});
    if (center_of_mass != glm::vec3{0.0f}) {
        create_info.collision_shape = erhe::physics::ICollision_shape::create_offset_center_of_mass_shape_shared(create_info.collision_shape, center_of_mass);
    }
    create_info.debug_label = node->get_name();

    const std::shared_ptr<Node_physics> node_physics = m_context.scene_commands->create_new_rigid_body(node.get(), create_info);
    if (!node_physics) {
        return make_error_content("Failed to create rigid body on node: " + node->get_name());
    }
    return make_json_content({
        {"created",     true},
        {"queued",      true}, // the attach operation executes on the next editor frame
        {"node",        node->get_name()},
        {"node_id",     node->get_id()},
        {"shape",       create_info.collision_shape->describe()},
        {"motion_mode", motion_mode_to_string(create_info.motion_mode)}
    }).dump();
}

auto Mcp_server::action_edit_physics_body(const json& args) -> std::string
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
    const std::shared_ptr<Content_library> library = sr->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library: " + scene_name);
    }

    // Validate everything before applying anything, so an error leaves the
    // body unchanged.
    const erhe::physics::Motion_mode motion_mode = args.contains("motion_mode")
        ? parse_motion_mode(args["motion_mode"].get<std::string>(), node_physics->get_motion_mode())
        : node_physics->get_motion_mode();
    std::shared_ptr<erhe::physics::ICollision_shape> new_shape{};
    if (args.contains("shape")) {
        std::string shape_error;
        new_shape = build_collision_shape_from_args(args, node.get(), shape_error);
        if (!new_shape) {
            return make_error_content(shape_error);
        }
        if ((args["shape"].get<std::string>() == "mesh") && (motion_mode == erhe::physics::Motion_mode::e_dynamic)) {
            return make_error_content("Triangle mesh shapes are static / kinematic only (Jolt restriction); use convex_hull for dynamic bodies");
        }
    }
    std::shared_ptr<erhe::physics::Physics_material> material{};
    if (args.contains("material_name")) {
        const std::string material_name = args["material_name"].get<std::string>();
        if (!material_name.empty()) {
            material = find_library_item<erhe::physics::Physics_material>(library->physics_materials, material_name);
            if (!material) {
                return make_error_content("Physics material not found: " + material_name);
            }
        }
    }
    std::shared_ptr<erhe::physics::Collision_filter> filter{};
    if (args.contains("filter_name")) {
        const std::string filter_name = args["filter_name"].get<std::string>();
        if (!filter_name.empty()) {
            filter = find_library_item<erhe::physics::Collision_filter>(library->collision_filters, filter_name);
            if (!filter) {
                return make_error_content("Collision filter not found: " + filter_name);
            }
        }
    }

    json applied = json::array();
    if (args.contains("motion_mode")) {
        node_physics->set_motion_mode(motion_mode);
        applied.push_back("motion_mode");
    }
    // Body-recreating edits first so live scalar edits below land on the
    // final rigid body.
    if (new_shape) {
        node_physics->set_collision_shape(new_shape);
        applied.push_back("shape");
    }
    if (args.contains("is_trigger")) {
        node_physics->set_trigger(args["is_trigger"].get<bool>());
        applied.push_back("is_trigger");
    }
    if (args.contains("center_of_mass")) {
        node_physics->set_center_of_mass_offset(get_vec3(args, "center_of_mass", glm::vec3{0.0f}));
        applied.push_back("center_of_mass");
    }
    if (args.contains("gravity_factor")) {
        node_physics->set_gravity_factor(args["gravity_factor"].get<float>());
        applied.push_back("gravity_factor");
    }
    if (args.contains("linear_velocity")) {
        node_physics->set_initial_linear_velocity(get_vec3(args, "linear_velocity", glm::vec3{0.0f}));
        applied.push_back("linear_velocity");
    }
    if (args.contains("angular_velocity")) {
        node_physics->set_initial_angular_velocity(get_vec3(args, "angular_velocity", glm::vec3{0.0f}));
        applied.push_back("angular_velocity");
    }
    if (args.contains("material_name")) {
        node_physics->set_physics_material(material);
        applied.push_back("material_name");
    }
    if (args.contains("filter_name")) {
        node_physics->set_collision_filter(filter);
        applied.push_back("filter_name");
    }

    erhe::physics::IRigid_body* rigid_body = node_physics->get_rigid_body();
    const bool wants_live_edit =
        args.contains("mass")        || args.contains("friction") || args.contains("restitution") ||
        args.contains("linear_damping") || args.contains("angular_damping");
    if (wants_live_edit && (rigid_body == nullptr)) {
        return make_error_content("Rigid body is not live (node not attached to a scene); mass / friction / restitution / damping not applied");
    }
    if (rigid_body != nullptr) {
        if (args.contains("mass")) {
            rigid_body->set_mass_properties(args["mass"].get<float>(), rigid_body->get_local_inertia());
            applied.push_back("mass");
        }
        if (args.contains("friction")) {
            rigid_body->set_friction(args["friction"].get<float>());
            applied.push_back("friction");
        }
        if (args.contains("restitution")) {
            rigid_body->set_restitution(args["restitution"].get<float>());
            applied.push_back("restitution");
        }
        if (args.contains("linear_damping") || args.contains("angular_damping")) {
            rigid_body->set_damping(
                args.value("linear_damping",  rigid_body->get_linear_damping()),
                args.value("angular_damping", rigid_body->get_angular_damping())
            );
            if (args.contains("linear_damping"))  { applied.push_back("linear_damping"); }
            if (args.contains("angular_damping")) { applied.push_back("angular_damping"); }
        }
    }

    const std::shared_ptr<erhe::physics::ICollision_shape>& shape = node_physics->get_collision_shape();
    return make_json_content({
        {"node",        node->get_name()},
        {"applied",     applied},
        {"motion_mode", motion_mode_to_string(node_physics->get_motion_mode())},
        {"shape",       shape ? shape->describe() : ""},
        {"is_trigger",  node_physics->is_trigger()}
    }).dump();
}

auto Mcp_server::action_create_physics_joint(const json& args) -> std::string
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

    std::shared_ptr<erhe::scene::Node> connected{};
    if (args.contains("connected_node_id") || args.contains("connected_node_name")) {
        connected = find_node_in_scene(*sr, args, "connected_node_id", "connected_node_name");
        if (!connected) {
            return make_error_content("Connected node not found");
        }
        if (connected == node) {
            return make_error_content("Connected node must differ from the joint node");
        }
    }

    std::shared_ptr<erhe::physics::Physics_joint_settings> settings{};
    const std::string settings_name = args.value("settings_name", "");
    if (!settings_name.empty()) {
        const std::shared_ptr<Content_library> library = sr->get_content_library();
        settings = find_library_item<erhe::physics::Physics_joint_settings>(library ? library->physics_joints : std::shared_ptr<Content_library_node>{}, settings_name);
        if (!settings) {
            return make_error_content("Joint settings not found: " + settings_name);
        }
    }
    const bool enable_collision = args.value("enable_collision", false);

    const std::shared_ptr<Node_joint> node_joint = m_context.scene_commands->create_new_joint(node.get(), connected, settings, enable_collision);
    if (!node_joint) {
        return make_error_content("Failed to create joint on node: " + node->get_name());
    }
    return make_json_content({
        {"created",          true},
        {"queued",           true}, // the attach operation executes on the next editor frame
        {"node",             node->get_name()},
        {"node_id",          node->get_id()},
        {"connected_node",   connected ? connected->get_name() : "(world)"},
        {"settings",         settings ? settings->get_name() : ""},
        {"enable_collision", enable_collision}
    }).dump();
}

auto Mcp_server::action_edit_physics_joint(const json& args) -> std::string
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
    std::vector<std::shared_ptr<Node_joint>> joints;
    for (const std::shared_ptr<erhe::scene::Node_attachment>& attachment : node->get_attachments()) {
        const std::shared_ptr<Node_joint> node_joint = std::dynamic_pointer_cast<Node_joint>(attachment);
        if (node_joint) {
            joints.push_back(node_joint);
        }
    }
    if (joints.empty()) {
        return make_error_content("Node has no joint: " + node->get_name());
    }
    const std::size_t joint_index = args.value("joint_index", std::size_t{0});
    if (joint_index >= joints.size()) {
        return make_error_content("joint_index out of range: node has " + std::to_string(joints.size()) + " joint(s)");
    }
    const std::shared_ptr<Node_joint> node_joint = joints[joint_index];

    json applied = json::array();
    if (args.value("connect_to_world", false)) {
        node_joint->set_connected_node({});
        applied.push_back("connect_to_world");
    } else if (args.contains("connected_node_id") || args.contains("connected_node_name")) {
        const std::shared_ptr<erhe::scene::Node> connected = find_node_in_scene(*sr, args, "connected_node_id", "connected_node_name");
        if (!connected) {
            return make_error_content("Connected node not found");
        }
        if (connected == node) {
            return make_error_content("Connected node must differ from the joint node");
        }
        node_joint->set_connected_node(connected);
        applied.push_back("connected_node");
    }
    if (args.contains("settings_name")) {
        const std::string settings_name = args["settings_name"].get<std::string>();
        if (settings_name.empty()) {
            node_joint->set_settings({});
        } else {
            const std::shared_ptr<Content_library> library = sr->get_content_library();
            const std::shared_ptr<erhe::physics::Physics_joint_settings> settings =
                find_library_item<erhe::physics::Physics_joint_settings>(library ? library->physics_joints : std::shared_ptr<Content_library_node>{}, settings_name);
            if (!settings) {
                return make_error_content("Joint settings not found: " + settings_name);
            }
            node_joint->set_settings(settings);
        }
        applied.push_back("settings");
    }
    if (args.contains("enable_collision")) {
        node_joint->set_enable_collision(args["enable_collision"].get<bool>());
        applied.push_back("enable_collision");
    }
    if (args.value("rebuild", false)) {
        node_joint->rebuild();
        applied.push_back("rebuild");
    }

    const std::shared_ptr<erhe::scene::Node> connected = node_joint->get_connected_node();
    const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings = node_joint->get_settings();
    return make_json_content({
        {"node",             node->get_name()},
        {"applied",          applied},
        {"connected_node",   connected ? connected->get_name() : "(world)"},
        {"settings",         settings ? settings->get_name() : ""},
        {"enable_collision", node_joint->get_enable_collision()},
        {"constraint",       (node_joint->get_constraint() != nullptr) ? "created" : "pending"}
    }).dump();
}

auto Mcp_server::action_create_physics_material(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<Content_library> library = sr->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library: " + scene_name);
    }
    const std::string name = args.value("name", "");
    if (name.empty()) {
        return make_error_content("name is required");
    }
    if (find_library_item<erhe::physics::Physics_material>(library->physics_materials, name)) {
        return make_error_content("Physics material already exists: " + name);
    }

    auto item = std::make_shared<erhe::physics::Physics_material>(name);
    item->static_friction     = args.value("static_friction",  item->static_friction);
    item->dynamic_friction    = args.value("dynamic_friction", item->dynamic_friction);
    item->restitution         = args.value("restitution",      item->restitution);
    item->friction_combine    = parse_combine_mode(args.value("friction_combine",    ""), item->friction_combine);
    item->restitution_combine = parse_combine_mode(args.value("restitution_combine", ""), item->restitution_combine);

    m_context.operation_stack->queue(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = std::make_shared<Content_library_node>(item),
                .parent  = library->physics_materials,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );
    return make_json_content({
        {"created", true},
        {"queued",  true}, // the insert operation executes on the next editor frame
        {"name",    name},
        {"id",      item->get_id()}
    }).dump();
}

auto Mcp_server::action_edit_physics_material(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<Content_library> library = sr->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library: " + scene_name);
    }
    const std::string name = args.value("name", "");
    const std::shared_ptr<erhe::physics::Physics_material> item = find_library_item<erhe::physics::Physics_material>(library->physics_materials, name);
    if (!item) {
        return make_error_content("Physics material not found: " + name);
    }

    json applied = json::array();
    if (args.contains("new_name")) {
        item->set_name(args["new_name"].get<std::string>());
        applied.push_back("new_name");
    }
    if (args.contains("static_friction"))  { item->static_friction  = args["static_friction"].get<float>();  applied.push_back("static_friction"); }
    if (args.contains("dynamic_friction")) { item->dynamic_friction = args["dynamic_friction"].get<float>(); applied.push_back("dynamic_friction"); }
    if (args.contains("restitution"))      { item->restitution      = args["restitution"].get<float>();      applied.push_back("restitution"); }
    if (args.contains("friction_combine")) {
        item->friction_combine = parse_combine_mode(args["friction_combine"].get<std::string>(), item->friction_combine);
        applied.push_back("friction_combine");
    }
    if (args.contains("restitution_combine")) {
        item->restitution_combine = parse_combine_mode(args["restitution_combine"].get<std::string>(), item->restitution_combine);
        applied.push_back("restitution_combine");
    }
    reapply_physics_material(m_context, item);

    return make_json_content({
        {"name",                item->get_name()},
        {"applied",             applied},
        {"static_friction",     item->static_friction},
        {"dynamic_friction",    item->dynamic_friction},
        {"restitution",         item->restitution},
        {"friction_combine",    combine_mode_to_string(item->friction_combine)},
        {"restitution_combine", combine_mode_to_string(item->restitution_combine)}
    }).dump();
}

auto Mcp_server::action_create_collision_filter(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<Content_library> library = sr->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library: " + scene_name);
    }
    const std::string name = args.value("name", "");
    if (name.empty()) {
        return make_error_content("name is required");
    }
    if (find_library_item<erhe::physics::Collision_filter>(library->collision_filters, name)) {
        return make_error_content("Collision filter already exists: " + name);
    }

    auto item = std::make_shared<erhe::physics::Collision_filter>(name);
    if (args.contains("collision_systems"))        { item->collision_systems        = args["collision_systems"].get<std::vector<std::string>>(); }
    if (args.contains("collide_with_systems"))     { item->collide_with_systems     = args["collide_with_systems"].get<std::vector<std::string>>(); }
    if (args.contains("not_collide_with_systems")) { item->not_collide_with_systems = args["not_collide_with_systems"].get<std::vector<std::string>>(); }

    m_context.operation_stack->queue(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = std::make_shared<Content_library_node>(item),
                .parent  = library->collision_filters,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );
    return make_json_content({
        {"created", true},
        {"queued",  true}, // the insert operation executes on the next editor frame
        {"name",    name},
        {"id",      item->get_id()}
    }).dump();
}

auto Mcp_server::action_edit_collision_filter(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<Content_library> library = sr->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library: " + scene_name);
    }
    const std::string name = args.value("name", "");
    const std::shared_ptr<erhe::physics::Collision_filter> item = find_library_item<erhe::physics::Collision_filter>(library->collision_filters, name);
    if (!item) {
        return make_error_content("Collision filter not found: " + name);
    }

    json applied = json::array();
    if (args.contains("new_name")) {
        item->set_name(args["new_name"].get<std::string>());
        applied.push_back("new_name");
    }
    if (args.contains("collision_systems")) {
        item->collision_systems = args["collision_systems"].get<std::vector<std::string>>();
        applied.push_back("collision_systems");
    }
    if (args.contains("collide_with_systems")) {
        item->collide_with_systems = args["collide_with_systems"].get<std::vector<std::string>>();
        applied.push_back("collide_with_systems");
    }
    if (args.contains("not_collide_with_systems")) {
        item->not_collide_with_systems = args["not_collide_with_systems"].get<std::vector<std::string>>();
        applied.push_back("not_collide_with_systems");
    }
    reapply_collision_filter(m_context, item);

    return make_json_content({
        {"name",                     item->get_name()},
        {"applied",                  applied},
        {"collision_systems",        item->collision_systems},
        {"collide_with_systems",     item->collide_with_systems},
        {"not_collide_with_systems", item->not_collide_with_systems}
    }).dump();
}

auto Mcp_server::action_create_physics_joint_settings(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<Content_library> library = sr->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library: " + scene_name);
    }
    const std::string name = args.value("name", "");
    if (name.empty()) {
        return make_error_content("name is required");
    }
    if (find_library_item<erhe::physics::Physics_joint_settings>(library->physics_joints, name)) {
        return make_error_content("Joint settings already exist: " + name);
    }

    auto item = std::make_shared<erhe::physics::Physics_joint_settings>(name);
    if (args.contains("limits")) {
        parse_joint_limits(args["limits"], item->limits);
    }
    if (args.contains("drives")) {
        parse_joint_drives(args["drives"], item->drives);
    }

    m_context.operation_stack->queue(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = std::make_shared<Content_library_node>(item),
                .parent  = library->physics_joints,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );
    json result = joint_settings_to_json(*item);
    result["created"] = true;
    result["queued"]  = true; // the insert operation executes on the next editor frame
    return make_json_content(result).dump();
}

auto Mcp_server::action_edit_physics_joint_settings(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<Content_library> library = sr->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library: " + scene_name);
    }
    const std::string name = args.value("name", "");
    const std::shared_ptr<erhe::physics::Physics_joint_settings> item = find_library_item<erhe::physics::Physics_joint_settings>(library->physics_joints, name);
    if (!item) {
        return make_error_content("Joint settings not found: " + name);
    }

    json applied = json::array();
    if (args.contains("new_name")) {
        item->set_name(args["new_name"].get<std::string>());
        applied.push_back("new_name");
    }
    if (args.contains("limits")) {
        parse_joint_limits(args["limits"], item->limits);
        applied.push_back("limits");
    }
    if (args.contains("drives")) {
        parse_joint_drives(args["drives"], item->drives);
        applied.push_back("drives");
    }
    // Joints using these settings only pick up changes when their constraint
    // is recreated; rebuild them all.
    rebuild_joints_using_settings(m_context, item);

    json result = joint_settings_to_json(*item);
    result["applied"] = applied;
    return make_json_content(result).dump();
}


} // namespace editor
