// Mcp_server graph tools (geometry graph, graph textures / meshes, texture graph).
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server_shared.hpp"

namespace editor {

using namespace mcp_server_detail;

namespace {

[[nodiscard]] auto geometry_graph_pin_json(const erhe::graph::Pin& pin) -> json
{
    return json{
        {"slot",        pin.get_slot()},
        {"key",         pin.get_key()},
        {"name",        std::string{pin.get_name()}},
        {"link_count",  pin.get_links().size()}
    };
}

// Summary of a pin payload: type tag plus the stats that matter for
// scripted verification (vertex / facet counts, point / instance counts,
// scalar values).
[[nodiscard]] auto geometry_payload_json(const Geometry_payload& payload) -> json
{
    const std::shared_ptr<erhe::geometry::Geometry> geometry = payload.get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        return json{{"type", "geometry"}, {"vertex_count", mesh.vertices.nb()}, {"facet_count", mesh.facets.nb()}};
    }
    const std::shared_ptr<Point_cloud> points = payload.get_points();
    if (points) {
        return json{{"type", "points"}, {"point_count", points->positions.size()}};
    }
    const std::shared_ptr<Geometry_instances> instances = payload.get_instances();
    if (instances) {
        return json{{"type", "instances"}, {"instance_count", instances->instance_count()}};
    }
    if (std::holds_alternative<float>(payload.value)) {
        return json{{"type", "float"}, {"value", payload.get_float()}};
    }
    if (std::holds_alternative<int>(payload.value)) {
        return json{{"type", "int"}, {"value", payload.get_int()}};
    }
    if (std::holds_alternative<bool>(payload.value)) {
        return json{{"type", "bool"}, {"value", payload.get_bool()}};
    }
    if (std::holds_alternative<glm::vec3>(payload.value)) {
        const glm::vec3 v = payload.get_vec3();
        return json{{"type", "vec3"}, {"value", {v.x, v.y, v.z}}};
    }
    if (!payload.has_value()) {
        return json{{"type", "empty"}};
    }
    return json{{"type", "other"}};
}

[[nodiscard]] auto geometry_graph_node_json(const Geometry_graph_node& node) -> json
{
    json inputs  = json::array();
    json outputs = json::array();
    for (const erhe::graph::Pin& pin : node.get_input_pins()) {
        inputs.push_back(geometry_graph_pin_json(pin));
    }
    json output_payloads = json::array();
    std::size_t output_slot = 0;
    for (const erhe::graph::Pin& pin : node.get_output_pins()) {
        outputs.push_back(geometry_graph_pin_json(pin));
        output_payloads.push_back(geometry_payload_json(node.get_output(output_slot)));
        ++output_slot;
    }
    json parameters = json::object();
    node.write_parameters(parameters);
    return json{
        {"id",              node.get_id()},
        {"name",            node.get_name()},
        {"parameters",      parameters},
        {"inputs",          inputs},
        {"outputs",         outputs},
        {"output_payloads", output_payloads}
    };
}

[[nodiscard]] auto find_geometry_graph_node(
    const std::vector<std::shared_ptr<Geometry_graph_node>>& nodes,
    const std::size_t                                        id
) -> Geometry_graph_node*
{
    for (const std::shared_ptr<Geometry_graph_node>& node : nodes) {
        if (node->get_id() == id) {
            return node.get();
        }
    }
    return nullptr;
}

} // anonymous namespace

auto Mcp_server::query_geometry_graph(const json& args) -> std::string
{
    static_cast<void>(args);
    Geometry_graph_window* window = m_context.geometry_graph_window;
    if (window == nullptr) {
        return make_error_content("Geometry graph window not available");
    }

    // Evaluation runs in the background; block until every graph is fully
    // evaluated so the reported payloads reflect every mutation issued
    // before this query (mutation tools return without waiting).
    window->wait_for_idle_evaluation();

    // Which graph the geometry_graph_* tools currently target (the
    // selected Graph_mesh asset; none selected = empty state).
    const std::shared_ptr<Graph_mesh>& graph_mesh = window->get_current_graph_mesh();
    if (!graph_mesh) {
        json result;
        result["selected"] = false;
        result["nodes"]    = json::array();
        result["links"]    = json::array();
        return make_json_content(result).dump();
    }

    json nodes = json::array();
    for (const std::shared_ptr<Geometry_graph_node>& node : window->get_nodes()) {
        nodes.push_back(geometry_graph_node_json(*node.get()));
    }

    json links = json::array();
    for (const std::unique_ptr<erhe::graph::Link>& link : graph_mesh->graph().get_links()) {
        links.push_back({
            {"source_node_id", link->get_source()->get_owner_node()->get_id()},
            {"source_slot",    link->get_source()->get_slot()},
            {"sink_node_id",   link->get_sink()->get_owner_node()->get_id()},
            {"sink_slot",      link->get_sink()->get_slot()}
        });
    }

    json result;
    result["selected"]        = true;
    result["nodes"]           = nodes;
    result["links"]           = links;
    result["graph_mesh_name"] = graph_mesh->get_name();
    result["graph_mesh_id"]   = graph_mesh->get_id();
    return make_json_content(result).dump();
}

auto Mcp_server::action_geometry_graph_add_node(const json& args) -> std::string
{
    Geometry_graph_window* window = m_context.geometry_graph_window;
    if (window == nullptr) {
        return make_error_content("Geometry graph window not available");
    }
    if (!window->get_current_graph_mesh()) {
        return make_error_content("No Graph Mesh selected - create one (create_graph_mesh) or select one first");
    }
    const std::string type_name = args.value("type", "");
    Geometry_graph_node* node = window->add_node_of_type(type_name);
    if (node == nullptr) {
        return make_error_content("Unknown geometry graph node type: " + type_name);
    }
    return make_json_content(geometry_graph_node_json(*node)).dump();
}

auto Mcp_server::action_geometry_graph_remove_node(const json& args) -> std::string
{
    Geometry_graph_window* window = m_context.geometry_graph_window;
    if (window == nullptr) {
        return make_error_content("Geometry graph window not available");
    }
    const std::size_t node_id = args.value("node_id", std::size_t{0});
    Geometry_graph_node* node = find_geometry_graph_node(window->get_nodes(), node_id);
    if (node == nullptr) {
        return make_error_content("Node not found");
    }
    window->remove_node(std::dynamic_pointer_cast<Geometry_graph_node>(node->node_from_this()));

    json result;
    result["removed"] = true;
    return make_json_content(result).dump();
}

auto Mcp_server::action_geometry_graph_set_parameter(const json& args) -> std::string
{
    Geometry_graph_window* window = m_context.geometry_graph_window;
    if (window == nullptr) {
        return make_error_content("Geometry graph window not available");
    }
    const std::size_t node_id = args.value("node_id", std::size_t{0});
    Geometry_graph_node* node = find_geometry_graph_node(window->get_nodes(), node_id);
    if (node == nullptr) {
        return make_error_content("Node not found");
    }
    if (!args.contains("parameters") || !args["parameters"].is_object()) {
        return make_error_content("Missing 'parameters' object");
    }
    window->set_node_parameters(
        std::dynamic_pointer_cast<Geometry_graph_node>(node->node_from_this()),
        args["parameters"]
    );
    return make_json_content(geometry_graph_node_json(*node)).dump();
}

auto Mcp_server::action_geometry_graph_connect(const json& args) -> std::string
{
    Geometry_graph_window* window = m_context.geometry_graph_window;
    if (window == nullptr) {
        return make_error_content("Geometry graph window not available");
    }
    const std::size_t source_node_id = args.value("source_node_id", std::size_t{0});
    const std::size_t source_slot    = args.value("source_slot",    std::size_t{0});
    const std::size_t sink_node_id   = args.value("sink_node_id",   std::size_t{0});
    const std::size_t sink_slot      = args.value("sink_slot",      std::size_t{0});

    Geometry_graph_node* source_node = find_geometry_graph_node(window->get_nodes(), source_node_id);
    Geometry_graph_node* sink_node   = find_geometry_graph_node(window->get_nodes(), sink_node_id);
    if (source_node == nullptr) {
        return make_error_content("Source node not found");
    }
    if (sink_node == nullptr) {
        return make_error_content("Sink node not found");
    }
    if (source_slot >= source_node->get_output_pins().size()) {
        return make_error_content("Source slot out of range");
    }
    if (sink_slot >= sink_node->get_input_pins().size()) {
        return make_error_content("Sink slot out of range");
    }
    erhe::graph::Pin* source_pin = &source_node->get_output_pins().at(source_slot);
    erhe::graph::Pin* sink_pin   = &sink_node->get_input_pins().at(sink_slot);
    const bool connected = window->connect(source_pin, sink_pin);
    if (!connected) {
        return make_error_content("Connect failed (pin key mismatch, or the link would create a cycle)");
    }

    json result;
    result["connected"] = true;
    return make_json_content(result).dump();
}

auto Mcp_server::action_geometry_graph_disconnect(const json& args) -> std::string
{
    Geometry_graph_window* window = m_context.geometry_graph_window;
    if (window == nullptr) {
        return make_error_content("Geometry graph window not available");
    }
    const std::size_t source_node_id = args.value("source_node_id", std::size_t{0});
    const std::size_t source_slot    = args.value("source_slot",    std::size_t{0});
    const std::size_t sink_node_id   = args.value("sink_node_id",   std::size_t{0});
    const std::size_t sink_slot      = args.value("sink_slot",      std::size_t{0});

    Geometry_graph_node* source_node = find_geometry_graph_node(window->get_nodes(), source_node_id);
    Geometry_graph_node* sink_node   = find_geometry_graph_node(window->get_nodes(), sink_node_id);
    if ((source_node == nullptr) || (sink_node == nullptr)) {
        return make_error_content("Node not found");
    }
    if ((source_slot >= source_node->get_output_pins().size()) || (sink_slot >= sink_node->get_input_pins().size())) {
        return make_error_content("Pin slot out of range");
    }
    window->disconnect(&source_node->get_output_pins().at(source_slot), &sink_node->get_input_pins().at(sink_slot));

    json result;
    result["disconnected"] = true;
    return make_json_content(result).dump();
}

namespace {

[[nodiscard]] auto texture_pin_value_type_name(const std::size_t key) -> const char*
{
    switch (key) {
        case Texture_pin_key::grayscale: return "f";
        case Texture_pin_key::rgb:       return "rgb";
        case Texture_pin_key::rgba:      return "rgba";
        default:                         return "unknown";
    }
}

[[nodiscard]] auto find_texture_graph_node(
    const std::vector<std::shared_ptr<Texture_graph_node>>& nodes,
    const std::size_t                                       id
) -> Texture_graph_node*
{
    for (const std::shared_ptr<Texture_graph_node>& node : nodes) {
        if (node->get_id() == id) {
            return node.get();
        }
    }
    return nullptr;
}

[[nodiscard]] auto texture_input_pin_json(const erhe::graph::Pin& pin) -> json
{
    json j{
        {"slot",       pin.get_slot()},
        {"key",        pin.get_key()},
        {"value_type", texture_pin_value_type_name(pin.get_key())},
        {"name",       std::string{pin.get_name()}},
        {"connected",  !pin.get_links().empty()}
    };
    const std::vector<erhe::graph::Link*>& links = pin.get_links();
    if (!links.empty()) {
        // MVP inputs take a single link; report the last connected source.
        erhe::graph::Pin* source_pin = links.back()->get_source();
        j["source_node_id"] = source_pin->get_owner_node()->get_id();
        j["source_slot"]    = source_pin->get_slot();
    }
    return j;
}

[[nodiscard]] auto texture_output_pin_json(const erhe::graph::Pin& pin) -> json
{
    return json{
        {"slot",       pin.get_slot()},
        {"key",        pin.get_key()},
        {"value_type", texture_pin_value_type_name(pin.get_key())},
        {"name",       std::string{pin.get_name()}},
        {"link_count", pin.get_links().size()}
    };
}

// Builds the compose DAG for exporting a node's output. A descriptor-driven node
// composes its own output slot; a sink node without a descriptor (the Output
// node) composes the highest-channel connected input's source subtree, matching
// how the Output node bakes its result.
[[nodiscard]] auto build_texture_export_dag(Texture_graph_node& node, const std::size_t output_slot) -> Texture_compose_dag
{
    if (node.descriptor() != nullptr) {
        return build_texture_compose_dag(node, output_slot);
    }
    const std::size_t input_count = node.get_input_pins().size();
    for (std::size_t i = input_count; i-- > 0; ) {
        const Texture_payload source = node.input_from_links(i);
        if (source.source_node != nullptr) {
            return build_texture_compose_dag(*source.source_node, source.output_index);
        }
    }
    return Texture_compose_dag{};
}

// Resolves each buffer cut point in the DAG to its already-rendered texture, so
// an export that samples buffers binds the same textures the editor produced.
[[nodiscard]] auto gather_texture_sample_bindings(const Texture_compose_dag& dag) -> std::vector<Texture_sample_binding>
{
    std::vector<Texture_sample_binding> bindings;
    bindings.reserve(dag.sampler_sources.size());
    for (const Texture_sampler_source& sampler_source : dag.sampler_sources) {
        Texture_sample_binding binding{};
        binding.binding = sampler_source.binding;
        binding.name    = std::string{"tex_"} + std::to_string(sampler_source.binding);
        binding.texture = sampler_source.buffer_node->get_preview_texture().get();
        bindings.push_back(binding);
    }
    return bindings;
}

[[nodiscard]] auto texture_graph_node_json(Texture_graph_window& window, Texture_graph_node& node) -> json
{
    json inputs = json::array();
    for (const erhe::graph::Pin& pin : node.get_input_pins()) {
        inputs.push_back(texture_input_pin_json(pin));
    }
    json outputs = json::array();
    for (const erhe::graph::Pin& pin : node.get_output_pins()) {
        outputs.push_back(texture_output_pin_json(pin));
    }
    json parameters = json::object();
    node.write_parameters(parameters);

    // 'composable' reflects whether the node's primary output currently
    // assembles to a fragment shader without an error marker. Pure string
    // composition (no GPU), so it is cheap enough to compute per query.
    bool composable = false;
    const Texture_compose_dag dag = build_texture_export_dag(node, 0);
    if (dag.ok && (dag.sink != nullptr)) {
        const erhe::texgen::Composer    composer{texture_graph_compose_options()};
        const erhe::texgen::Shader_code shader_code = composer.compose(*dag.sink, dag.sink_output_index);
        const std::string               fragment    = composer.assemble_fragment(shader_code);
        composable = (fragment.find("(error:") == std::string::npos);
    }

    const ImVec2 position = window.get_node_position(node);
    return json{
        {"id",         node.get_id()},
        {"name",       node.get_name()},
        {"type",       node.get_factory_type_name()},
        {"position",   {position.x, position.y}},
        {"parameters", parameters},
        {"inputs",     inputs},
        {"outputs",    outputs},
        {"composable", composable}
    };
}

} // anonymous namespace

auto Mcp_server::action_create_graph_texture(const json& args) -> std::string
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
    if (find_library_item<Graph_texture>(library->graph_textures, name)) {
        return make_error_content("Graph texture already exists: " + name);
    }

    const std::shared_ptr<Graph_texture> item = std::make_shared<Graph_texture>(name);
    // execute_now so the asset is live this frame and selectable immediately.
    m_context.operation_stack->execute_now(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = std::make_shared<Content_library_node>(item),
                .parent  = library->graph_textures,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );
    // Select it so the Texture Graph window and the texture_graph_* tools (which
    // operate on the current selection) target this asset.
    if (m_context.selection != nullptr) {
        m_context.selection->set_selection({item});
    }
    return make_json_content({
        {"created", true},
        {"name",    name},
        {"id",      item->get_id()}
    }).dump();
}

auto Mcp_server::action_set_material_texture_source(const json& args) -> std::string
{
    const std::string scene_name         = args.value("scene_name", "");
    const std::string material_name       = args.value("material_name", "");
    const std::string slot_name           = args.value("slot", "base_color");
    const std::string graph_texture_name  = args.value("graph_texture", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<Content_library> library = sr->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library: " + scene_name);
    }
    const std::shared_ptr<erhe::primitive::Material> material =
        find_library_item<erhe::primitive::Material>(library->materials, material_name);
    if (!material) {
        return make_error_content("Material not found: " + material_name);
    }

    erhe::primitive::Material_texture_samplers& samplers = material->data.texture_samplers;
    erhe::primitive::Material_texture_sampler*  sampler  = nullptr;
    if      (slot_name == "base_color")         sampler = &samplers.base_color;
    else if (slot_name == "metallic_roughness") sampler = &samplers.metallic_roughness;
    else if (slot_name == "normal")             sampler = &samplers.normal;
    else if (slot_name == "occlusion")          sampler = &samplers.occlusion;
    else if (slot_name == "emissive")           sampler = &samplers.emissive;
    else {
        return make_error_content("Unknown material slot: " + slot_name + " (base_color|metallic_roughness|normal|occlusion|emissive)");
    }

    if (graph_texture_name.empty()) {
        sampler->texture_source.reset();
        return make_json_content({
            {"cleared",  true},
            {"material", material_name},
            {"slot",     slot_name}
        }).dump();
    }

    const std::shared_ptr<Graph_texture> graph_texture =
        find_library_item<Graph_texture>(library->graph_textures, graph_texture_name);
    if (!graph_texture) {
        return make_error_content("Graph texture not found: " + graph_texture_name);
    }
    // Graph_texture is-a erhe::graphics::Texture_reference; the shared_ptr upcasts.
    // The source is authoritative, so clear any directly-assigned texture.
    sampler->texture_source = graph_texture;
    sampler->texture.reset();
    return make_json_content({
        {"bound",            true},
        {"material",         material_name},
        {"slot",             slot_name},
        {"graph_texture",    graph_texture_name},
        {"graph_texture_id", graph_texture->get_id()}
    }).dump();
}

auto Mcp_server::query_graph_textures(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    json graph_textures = json::array();
    const auto append_from = [&graph_textures](Scene_root& scene_root) {
        const std::shared_ptr<Content_library> library = scene_root.get_content_library();
        if (!library || !library->graph_textures) {
            return;
        }
        for (const std::shared_ptr<Graph_texture>& graph_texture : library->graph_textures->get_all<Graph_texture>()) {
            graph_textures.push_back({
                {"name",       graph_texture->get_name()},
                {"id",         graph_texture->get_id()},
                {"scene",      scene_root.get_name()},
                {"node_count", graph_texture->nodes().size()},
                {"has_output", graph_texture->get_referenced_texture() != nullptr}
            });
        }
    };
    if (!scene_name.empty()) {
        Scene_root* sr = find_scene(scene_name);
        if (sr == nullptr) {
            return make_error_content("Scene not found: " + scene_name);
        }
        append_from(*sr);
    } else if (m_context.app_scenes != nullptr) {
        for (const std::shared_ptr<Scene_root>& sr : m_context.app_scenes->get_scene_roots()) {
            append_from(*sr);
        }
    }
    json result;
    result["graph_textures"] = graph_textures;
    return make_json_content(result).dump();
}

auto Mcp_server::action_create_graph_mesh(const json& args) -> std::string
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
    if (find_library_item<Graph_mesh>(library->graph_meshes, name)) {
        return make_error_content("Graph mesh already exists: " + name);
    }

    const std::shared_ptr<Graph_mesh> item = std::make_shared<Graph_mesh>(name);
    // execute_now so the asset is live this frame and selectable immediately.
    m_context.operation_stack->execute_now(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = std::make_shared<Content_library_node>(item),
                .parent  = library->graph_meshes,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );
    // Select it so the Geometry Graph window and the geometry_graph_* tools
    // (which operate on the current selection) target this asset.
    if (m_context.selection != nullptr) {
        m_context.selection->set_selection({item});
    }
    return make_json_content({
        {"created", true},
        {"name",    name},
        {"id",      item->get_id()}
    }).dump();
}

auto Mcp_server::action_set_node_graph_mesh(const json& args) -> std::string
{
    const std::string scene_name      = args.value("scene_name", "");
    const std::string node_name       = args.value("node_name", "");
    const std::string graph_mesh_name = args.value("graph_mesh", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::shared_ptr<erhe::scene::Node> node;
    for (const std::shared_ptr<erhe::scene::Node>& entry : sr->get_scene().get_flat_nodes()) {
        if (entry->get_name() == node_name) {
            node = entry;
            break;
        }
    }
    if (!node) {
        return make_error_content("Node not found: " + node_name);
    }

    const std::shared_ptr<Geometry_graph_mesh> existing = erhe::scene::get_attachment<Geometry_graph_mesh>(node.get());

    if (graph_mesh_name.empty()) {
        if (existing) {
            // set_graph_mesh(nullptr) releases the controlled mesh/physics;
            // the shared_ptr keeps the attachment alive across detach.
            existing->set_graph_mesh({});
            node->detach(existing.get());
        }
        return make_json_content({
            {"cleared", true},
            {"node",    node_name}
        }).dump();
    }

    const std::shared_ptr<Content_library> library = sr->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library: " + scene_name);
    }
    const std::shared_ptr<Graph_mesh> graph_mesh = find_library_item<Graph_mesh>(library->graph_meshes, graph_mesh_name);
    if (!graph_mesh) {
        return make_error_content("Graph mesh not found: " + graph_mesh_name);
    }

    std::shared_ptr<Geometry_graph_mesh> attachment = existing;
    if (!attachment) {
        attachment = std::make_shared<Geometry_graph_mesh>(graph_mesh);
        node->attach(attachment);
    } else {
        attachment->set_graph_mesh(graph_mesh);
    }
    // Materialize the asset's latest bake immediately; a never-baked asset
    // applies on its first evaluation push (get_geometry_graph barrier).
    attachment->apply_baked_products();
    return make_json_content({
        {"bound",         true},
        {"node",          node_name},
        {"graph_mesh",    graph_mesh_name},
        {"graph_mesh_id", graph_mesh->get_id()}
    }).dump();
}

auto Mcp_server::query_graph_meshes(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    json graph_meshes = json::array();
    const auto append_from = [&graph_meshes](Scene_root& scene_root) {
        const std::shared_ptr<Content_library> library = scene_root.get_content_library();
        if (!library || !library->graph_meshes) {
            return;
        }
        for (const std::shared_ptr<Graph_mesh>& graph_mesh : library->graph_meshes->get_all<Graph_mesh>()) {
            graph_meshes.push_back({
                {"name",           graph_mesh->get_name()},
                {"id",             graph_mesh->get_id()},
                {"scene",          scene_root.get_name()},
                {"node_count",     graph_mesh->nodes().size()},
                {"baked_revision", graph_mesh->get_baked_revision()},
                {"has_bake",       static_cast<bool>(graph_mesh->get_baked_products().primitive)}
            });
        }
    };
    if (!scene_name.empty()) {
        Scene_root* sr = find_scene(scene_name);
        if (sr == nullptr) {
            return make_error_content("Scene not found: " + scene_name);
        }
        append_from(*sr);
    } else if (m_context.app_scenes != nullptr) {
        for (const std::shared_ptr<Scene_root>& sr : m_context.app_scenes->get_scene_roots()) {
            append_from(*sr);
        }
    }
    json result;
    result["graph_meshes"] = graph_meshes;
    return make_json_content(result).dump();
}

auto Mcp_server::query_texture_graph(const json& args) -> std::string
{
    static_cast<void>(args);
    Texture_graph_window* window = m_context.texture_graph_window;
    if (window == nullptr) {
        return make_error_content("Texture graph window not available");
    }

    const std::shared_ptr<Graph_texture>& current = window->get_current_graph_texture();
    if (!current) {
        json result;
        result["selected"] = false;
        result["nodes"]    = json::array();
        result["links"]    = json::array();
        return make_json_content(result).dump();
    }

    // Texture graph evaluation is synchronous (decision 8), so no wait is needed.
    json nodes = json::array();
    for (const std::shared_ptr<Texture_graph_node>& node : window->get_nodes()) {
        nodes.push_back(texture_graph_node_json(*window, *node.get()));
    }

    json links = json::array();
    for (const std::unique_ptr<erhe::graph::Link>& link : current->graph().get_links()) {
        links.push_back({
            {"source_node_id", link->get_source()->get_owner_node()->get_id()},
            {"source_slot",    link->get_source()->get_slot()},
            {"sink_node_id",   link->get_sink()->get_owner_node()->get_id()},
            {"sink_slot",      link->get_sink()->get_slot()}
        });
    }

    json result;
    result["selected"]           = true;
    result["graph_texture_name"] = current->get_name();
    result["graph_texture_id"]   = current->get_id();
    result["nodes"] = nodes;
    result["links"] = links;
    return make_json_content(result).dump();
}

auto Mcp_server::action_texture_graph_add_node(const json& args) -> std::string
{
    Texture_graph_window* window = m_context.texture_graph_window;
    if (window == nullptr) {
        return make_error_content("Texture graph window not available");
    }
    if (!window->get_current_graph_texture()) {
        return make_error_content("No Graph Texture selected - create one (create_graph_texture) or select one first");
    }
    const std::string type_name = args.value("type", "");
    Texture_graph_node* node = window->add_node_of_type(type_name);
    if (node == nullptr) {
        return make_error_content("Unknown texture graph node type: " + type_name);
    }
    if (args.contains("position") && args["position"].is_array() && (args["position"].size() == 2)) {
        const float x = args["position"][0].get<float>();
        const float y = args["position"][1].get<float>();
        window->set_node_position(*node, ImVec2{x, y});
    }
    return make_json_content(texture_graph_node_json(*window, *node)).dump();
}

auto Mcp_server::action_texture_graph_remove_node(const json& args) -> std::string
{
    Texture_graph_window* window = m_context.texture_graph_window;
    if (window == nullptr) {
        return make_error_content("Texture graph window not available");
    }
    const std::size_t node_id = args.value("node_id", std::size_t{0});
    Texture_graph_node* node = find_texture_graph_node(window->get_nodes(), node_id);
    if (node == nullptr) {
        return make_error_content("Node not found");
    }
    window->remove_node(std::dynamic_pointer_cast<Texture_graph_node>(node->node_from_this()));

    json result;
    result["removed"] = true;
    return make_json_content(result).dump();
}

auto Mcp_server::action_texture_graph_set_parameter(const json& args) -> std::string
{
    Texture_graph_window* window = m_context.texture_graph_window;
    if (window == nullptr) {
        return make_error_content("Texture graph window not available");
    }
    const std::size_t node_id = args.value("node_id", std::size_t{0});
    Texture_graph_node* node = find_texture_graph_node(window->get_nodes(), node_id);
    if (node == nullptr) {
        return make_error_content("Node not found");
    }
    if (!args.contains("parameters") || !args["parameters"].is_object()) {
        return make_error_content("Missing 'parameters' object");
    }
    window->set_node_parameters(
        std::dynamic_pointer_cast<Texture_graph_node>(node->node_from_this()),
        args["parameters"]
    );
    return make_json_content(texture_graph_node_json(*window, *node)).dump();
}

auto Mcp_server::action_texture_graph_connect(const json& args) -> std::string
{
    Texture_graph_window* window = m_context.texture_graph_window;
    if (window == nullptr) {
        return make_error_content("Texture graph window not available");
    }
    const std::size_t source_node_id = args.value("source_node_id", std::size_t{0});
    const std::size_t source_slot    = args.value("source_slot",    std::size_t{0});
    const std::size_t sink_node_id   = args.value("sink_node_id",   std::size_t{0});
    const std::size_t sink_slot      = args.value("sink_slot",      std::size_t{0});

    Texture_graph_node* source_node = find_texture_graph_node(window->get_nodes(), source_node_id);
    Texture_graph_node* sink_node   = find_texture_graph_node(window->get_nodes(), sink_node_id);
    if (source_node == nullptr) {
        return make_error_content("Source node not found");
    }
    if (sink_node == nullptr) {
        return make_error_content("Sink node not found");
    }
    if (source_slot >= source_node->get_output_pins().size()) {
        return make_error_content("Source slot out of range");
    }
    if (sink_slot >= sink_node->get_input_pins().size()) {
        return make_error_content("Sink slot out of range");
    }
    erhe::graph::Pin* source_pin = &source_node->get_output_pins().at(source_slot);
    erhe::graph::Pin* sink_pin   = &sink_node->get_input_pins().at(sink_slot);
    const bool connected = window->connect(source_pin, sink_pin);
    if (!connected) {
        return make_error_content("Connect failed (pin key mismatch, or the link would create a cycle)");
    }

    json result;
    result["connected"] = true;
    return make_json_content(result).dump();
}

auto Mcp_server::action_texture_graph_disconnect(const json& args) -> std::string
{
    Texture_graph_window* window = m_context.texture_graph_window;
    if (window == nullptr) {
        return make_error_content("Texture graph window not available");
    }
    const std::size_t source_node_id = args.value("source_node_id", std::size_t{0});
    const std::size_t source_slot    = args.value("source_slot",    std::size_t{0});
    const std::size_t sink_node_id   = args.value("sink_node_id",   std::size_t{0});
    const std::size_t sink_slot      = args.value("sink_slot",      std::size_t{0});

    Texture_graph_node* source_node = find_texture_graph_node(window->get_nodes(), source_node_id);
    Texture_graph_node* sink_node   = find_texture_graph_node(window->get_nodes(), sink_node_id);
    if ((source_node == nullptr) || (sink_node == nullptr)) {
        return make_error_content("Node not found");
    }
    if ((source_slot >= source_node->get_output_pins().size()) || (sink_slot >= sink_node->get_input_pins().size())) {
        return make_error_content("Pin slot out of range");
    }
    window->disconnect(&source_node->get_output_pins().at(source_slot), &sink_node->get_input_pins().at(sink_slot));

    json result;
    result["disconnected"] = true;
    return make_json_content(result).dump();
}

auto Mcp_server::action_texture_graph_export_png(const json& args) -> std::string
{
    Texture_graph_window* window = m_context.texture_graph_window;
    if (window == nullptr) {
        return make_error_content("Texture graph window not available");
    }
    if (m_context.graphics_device == nullptr) {
        return make_error_content("Graphics device not available");
    }
    const std::size_t node_id     = args.value("node_id",     std::size_t{0});
    const std::size_t output_slot = args.value("output_slot", std::size_t{0});
    const int         size        = std::clamp(args.value("size", 256), 1, 4096);
    const std::string path        = args.value("path", "");
    if (path.empty()) {
        return make_error_content("Missing 'path'");
    }
    Texture_graph_node* node = find_texture_graph_node(window->get_nodes(), node_id);
    if (node == nullptr) {
        return make_error_content("Node not found");
    }

    // Compose the node's output subtree into a fragment shader (pure string
    // logic - see decision 1). A sink node (Output) with no descriptor composes
    // its connected input's source instead.
    const Texture_compose_dag dag = build_texture_export_dag(*node, output_slot);
    if (!dag.ok || (dag.sink == nullptr)) {
        return make_error_content("Node has no composable output (unconnected sink or no descriptor)");
    }
    const erhe::texgen::Composer    composer{texture_graph_compose_options()};
    const erhe::texgen::Shader_code shader_code = composer.compose(*dag.sink, dag.sink_output_index);
    const std::string               fragment    = composer.assemble_fragment(shader_code);
    if (fragment.find("(error:") != std::string::npos) {
        return make_error_content("Composition failed (cycle / depth / error marker)");
    }

    Texture_renderer* renderer = window->get_renderer();
    if (renderer == nullptr) {
        return make_error_content("Texture renderer not available");
    }

    const std::vector<Texture_sample_binding> sampler_bindings = gather_texture_sample_bindings(dag);
    std::vector<std::uint8_t> pixels;
    if (!renderer->render_and_read_rgba8(size, fragment, shader_code.get_uniforms(), pixels, shader_code.get_samplers(), sampler_bindings)) {
        return make_error_content("Render / readback failed (shader compile error, or a sampled buffer has not rendered yet)");
    }

    std::unique_ptr<erhe::graphics::Image_writer> writer = erhe::graphics::Image_writer::create();
    const int                  row_stride = size * 4;
    std::span<const std::byte> data{reinterpret_cast<const std::byte*>(pixels.data()), pixels.size()};
    if (!writer->write_png(std::filesystem::path{path}, size, size, row_stride, Texture_renderer::color_format(), data)) {
        return make_error_content("Failed to write PNG '" + path + "' (image writer backend may be disabled)");
    }

    return make_json_content({
        {"path",   path},
        {"width",  size},
        {"height", size}
    }).dump();
}

auto Mcp_server::action_texture_graph_export_material(const json& args) -> std::string
{
    Texture_graph_window* window = m_context.texture_graph_window;
    if (window == nullptr) {
        return make_error_content("Texture graph window not available");
    }
    if (m_context.graphics_device == nullptr) {
        return make_error_content("Graphics device not available");
    }
    const std::size_t node_id = args.value("node_id", std::size_t{0});
    const int         size    = std::clamp(args.value("size", 256), 1, 4096);
    const std::string dir     = args.value("dir", "");
    if (dir.empty()) {
        return make_error_content("Missing 'dir'");
    }
    Texture_graph_node* node = find_texture_graph_node(window->get_nodes(), node_id);
    if (node == nullptr) {
        return make_error_content("Node not found");
    }
    Texture_material_output_node* material_node = dynamic_cast<Texture_material_output_node*>(node);
    if (material_node == nullptr) {
        return make_error_content("Node is not a Material Output node");
    }

    const std::vector<Material_channel_export> exports = material_node->build_channel_exports();
    if (exports.empty()) {
        return make_error_content("Material Output node has no connected channels to export");
    }

    Texture_renderer* renderer = window->get_renderer();
    if (renderer == nullptr) {
        return make_error_content("Texture renderer not available");
    }

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path{dir}, ec);

    const std::string base_name = material_node->get_base_name();
    json written = json::array();
    for (const Material_channel_export& channel : exports) {
        std::vector<std::uint8_t> pixels;
        if (!renderer->render_and_read_rgba8(size, channel.fragment, channel.uniforms, pixels)) {
            return make_error_content("Render / readback failed for channel '" + channel.suffix + "'");
        }
        const std::filesystem::path path = std::filesystem::path{dir} / (base_name + "_" + channel.suffix + ".png");
        std::unique_ptr<erhe::graphics::Image_writer> writer = erhe::graphics::Image_writer::create();
        const int                  row_stride = size * 4;
        std::span<const std::byte> data{reinterpret_cast<const std::byte*>(pixels.data()), pixels.size()};
        if (!writer->write_png(path, size, size, row_stride, Texture_renderer::color_format(), data)) {
            return make_error_content("Failed to write PNG '" + path.string() + "'");
        }
        written.push_back(path.string());
    }

    return make_json_content({
        {"files",  written},
        {"width",  size},
        {"height", size}
    }).dump();
}


} // namespace editor
