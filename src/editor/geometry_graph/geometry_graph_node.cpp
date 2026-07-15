#include "geometry_graph/geometry_graph_node.hpp"
#include "geometry_graph/geometry_graph_operations.hpp"
#include "geometry_graph/geometry_graph_window.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "app_context.hpp"
#include "operations/operation_stack.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_node_editor.h"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include <geogram/mesh/mesh.h>

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

namespace editor {

// Pin fill color per Geometry_pin_key payload type. Pins only accept links
// from pins with the same key, so matching colors show which connections
// are legal before a drag gets rejected.
auto Geometry_graph_node::pin_key_color(const std::size_t key) const -> ImU32
{
    switch (key) {
        case Geometry_pin_key::geometry:    return IM_COL32( 54, 192, 154, 255); // teal
        case Geometry_pin_key::float_value: return IM_COL32(160, 160, 160, 255); // grey
        case Geometry_pin_key::int_value:   return IM_COL32( 89, 140,  92, 255); // muted green
        case Geometry_pin_key::bool_value:  return IM_COL32(204, 166, 214, 255); // pink
        case Geometry_pin_key::vec3_value:  return IM_COL32( 99,  99, 199, 255); // indigo
        case Geometry_pin_key::vec4_value:  return IM_COL32(199, 199,  41, 255); // yellow
        case Geometry_pin_key::mat4_value:  return IM_COL32( 70, 120, 190, 255); // steel blue
        case Geometry_pin_key::material:    return IM_COL32(235, 122,  82, 255); // orange
        case Geometry_pin_key::points:      return IM_COL32(110, 205, 230, 255); // light cyan
        case Geometry_pin_key::instances:   return IM_COL32(130, 215, 120, 255); // spring green
        default:                            return IM_COL32( 68,  68,  68, 255);
    }
}

void Geometry_graph_node::commit_parameter_operation(App_context& app_context, std::string&& before_parameters, std::string&& after_parameters)
{
    app_context.operation_stack->execute_now(
        std::make_shared<Geometry_graph_parameter_operation>(
            *app_context.geometry_graph_window,
            std::dynamic_pointer_cast<Geometry_graph_node>(node_from_this()),
            std::move(before_parameters),
            std::move(after_parameters)
        )
    );
}

void process_for_graph(erhe::geometry::Geometry& geometry)
{
    geometry.process(
        {
            .flags =
                erhe::geometry::Geometry::process_flag_connect |
                erhe::geometry::Geometry::process_flag_build_edges
        }
    );
}

void write_vec3(nlohmann::json& out, const char* key, const glm::vec3& value)
{
    out[key] = { value.x, value.y, value.z };
}

void write_ivec3(nlohmann::json& out, const char* key, const glm::ivec3& value)
{
    out[key] = { value.x, value.y, value.z };
}

auto read_vec3(const nlohmann::json& in, const char* key, const glm::vec3& fallback) -> glm::vec3
{
    if (in.contains(key) && in[key].is_array() && (in[key].size() == 3)) {
        return glm::vec3{in[key][0].get<float>(), in[key][1].get<float>(), in[key][2].get<float>()};
    }
    return fallback;
}

auto read_ivec3(const nlohmann::json& in, const char* key, const glm::ivec3& fallback) -> glm::ivec3
{
    if (in.contains(key) && in[key].is_array() && (in[key].size() == 3)) {
        return glm::ivec3{in[key][0].get<int>(), in[key][1].get<int>(), in[key][2].get<int>()};
    }
    return fallback;
}

Geometry_graph_node::Geometry_graph_node(const char* label)
    : Graph_editor_node{label}
{
}

auto Geometry_graph_node::accumulate_input_from_links(const std::size_t i) const -> Geometry_payload
{
    const erhe::graph::Pin& input_pin = get_input_pins().at(i);
    Geometry_payload accumulated{};
    for (erhe::graph::Link* link : input_pin.get_links()) {
        erhe::graph::Pin*    source_pin  = link->get_source();
        const std::size_t    slot        = source_pin->get_slot();
        erhe::graph::Node*   source_node = source_pin->get_owner_node();
        Geometry_graph_node* source_geometry_graph_node = dynamic_cast<Geometry_graph_node*>(source_node);
        if (source_geometry_graph_node != nullptr) {
            accumulated += source_geometry_graph_node->get_output(slot);
        }
    }
    return accumulated;
}

void Geometry_graph_node::pull_inputs()
{
    for (std::size_t i = 0, end = m_input_payloads.size(); i < end; ++i) {
        m_input_payloads[i] = accumulate_input_from_links(i);
    }
}

auto Geometry_graph_node::get_input(const std::size_t i) const -> const Geometry_payload&
{
    return m_input_payloads.at(i);
}

auto Geometry_graph_node::get_output(const std::size_t i) const -> const Geometry_payload&
{
    return m_output_payloads.at(i);
}

void Geometry_graph_node::set_input(const std::size_t i, const Geometry_payload& payload)
{
    m_input_payloads.at(i) = payload;
}

void Geometry_graph_node::set_output(const std::size_t i, const Geometry_payload& payload)
{
    m_output_payloads.at(i) = payload;
}

void Geometry_graph_node::make_input_pin(const std::size_t key, const std::string_view name)
{
    m_input_payloads.emplace_back();
    base_make_input_pin(key, name);
}

void Geometry_graph_node::make_output_pin(const std::size_t key, const std::string_view name)
{
    m_output_payloads.emplace_back();
    base_make_output_pin(key, name);
}

void Geometry_graph_node::evaluate(Geometry_graph&)
{
    // Overridden in derived classes
}

auto Geometry_graph_node::is_scene_output() const -> bool
{
    return false;
}

void Geometry_graph_node::after_node_content(App_context& context)
{
    // Houdini-style display ("D") / ghost ("G") designation badges. Hidden
    // for scene outputs (designating the output is a no-op), for nodes
    // without a geometry output (value / math nodes have nothing to show)
    // and for nodes outside an asset graph (scratch / shadow / group
    // subgraph nodes have no owning Graph_mesh).
    if (is_scene_output()) {
        return;
    }
    const std::shared_ptr<Graph_mesh> graph_mesh = get_owning_graph_mesh();
    if (!graph_mesh) {
        return;
    }
    bool has_geometry_output = false;
    for (const erhe::graph::Pin& pin : get_output_pins()) {
        if (pin.get_key() == Geometry_pin_key::geometry) {
            has_geometry_output = true;
            break;
        }
    }
    if (!has_geometry_output) {
        return;
    }

    Geometry_graph&   graph      = graph_mesh->graph();
    const std::size_t node_id    = get_id();
    const std::size_t display_id = graph.get_display_node_id();
    const std::size_t ghost_id   = graph.get_ghost_node_id();
    const bool        is_display = (display_id == node_id);
    const bool        is_ghost   = (ghost_id   == node_id);

    const ImVec2 badge_size{18.0f * m_content_scale, 18.0f * m_content_scale};

    bool toggle_display = false;
    bool toggle_ghost   = false;

    if (is_display) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.20f, 0.45f, 0.85f, 1.0f});
    }
    if (ImGui::Button("D", badge_size)) {
        toggle_display = true;
    }
    if (is_display) {
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();

    if (is_ghost) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.60f, 0.35f, 0.75f, 1.0f});
    }
    if (ImGui::Button("G", badge_size)) {
        toggle_ghost = true;
    }
    if (is_ghost) {
        ImGui::PopStyleColor();
    }

    if (toggle_display || toggle_ghost) {
        // Toggle off on the current holder, move otherwise; one operation
        // covers the implicit un-badge of the previous holder.
        std::size_t new_display_id = display_id;
        std::size_t new_ghost_id   = ghost_id;
        if (toggle_display) {
            new_display_id = is_display ? 0 : node_id;
        }
        if (toggle_ghost) {
            new_ghost_id = is_ghost ? 0 : node_id;
        }
        context.operation_stack->execute_now(
            std::make_shared<Geometry_graph_display_designation_operation>(
                graph_mesh, get_name(), display_id, ghost_id, new_display_id, new_ghost_id
            )
        );
    }

    if (graph_mesh->get_node_previews_enabled()) {
        draw_preview(context);
    }
}

void Geometry_graph_node::on_removed_from_graph()
{
    // The node object may stay alive in the undo stack; GPU resources
    // (preview texture + primitive buffers) must not stay alive with it
    // (texture-graph precedent). Re-insertion re-marks the node dirty, so
    // the preview rebuilds.
    m_preview_primitive.reset();
    m_preview_texture.reset();
    m_preview_needs_render = false;
}

void Geometry_graph_node::build_preview_primitive(erhe::scene_renderer::Mesh_memory& mesh_memory)
{
    m_preview_primitive.reset();

    // The first geometry output payload; value / math nodes have none.
    std::shared_ptr<erhe::geometry::Geometry> source{};
    const auto& pins = get_output_pins();
    for (std::size_t slot = 0, end = pins.size(); slot < end; ++slot) {
        if (pins[slot].get_key() == Geometry_pin_key::geometry) {
            source = get_output(slot).get_geometry();
            break;
        }
    }
    if (!source || (source->get_mesh().facets.nb() == 0)) {
        return;
    }

    // Copy before render processing; the payload geometry is shared with
    // downstream nodes and must not be mutated.
    std::shared_ptr<erhe::geometry::Geometry> preview_geometry = std::make_shared<erhe::geometry::Geometry>(source->get_name());
    GEO::mat4f identity;
    identity.load_identity();
    preview_geometry->copy_with_transform(*source.get(), identity);
    preview_geometry->process(
        {
            .flags =
                erhe::geometry::Geometry::process_flag_connect                            |
                erhe::geometry::Geometry::process_flag_build_edges                        |
                erhe::geometry::Geometry::process_flag_compute_facet_centroids            |
                erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals      |
                erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates |
                erhe::geometry::Geometry::process_flag_generate_tangents
        }
    );

    const erhe::primitive::Build_info build_info{
        .primitive_types = {
            .fill_triangles          = true,
            // Expanded fill soup for the preview's solid-wireframe edge-line
            // overlay (see Brush_preview / Preview_edge_lines_config).
            .fill_triangles_expanded = true
        },
        .buffer_info     = mesh_memory.make_primitive_buffer_info()
    };
    std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(preview_geometry);
    if (primitive->make_renderable_mesh(build_info, erhe::primitive::Normal_style::point_normals)) {
        m_preview_primitive = primitive;
    } else {
        log_graph_editor->warn("Geometry_graph_node: failed to build preview mesh for '{}'", get_name());
    }
}

void Geometry_graph_node::take_preview(Geometry_graph_node& from)
{
    if (!from.m_preview_primitive) {
        return;
    }
    m_preview_primitive = std::move(from.m_preview_primitive);
    m_preview_needs_render = true;
}

auto Geometry_graph_node::get_preview_primitive() const -> const std::shared_ptr<erhe::primitive::Primitive>&
{
    return m_preview_primitive;
}

auto Geometry_graph_node::preview_needs_render() const -> bool
{
    return m_preview_needs_render;
}

void Geometry_graph_node::clear_preview_needs_render()
{
    m_preview_needs_render = false;
}

void Geometry_graph_node::mark_preview_needs_render()
{
    m_preview_needs_render = true;
}

auto Geometry_graph_node::get_preview_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&
{
    return m_preview_texture;
}

void Geometry_graph_node::set_preview_texture(const std::shared_ptr<erhe::graphics::Texture>& texture)
{
    m_preview_texture = texture;
}

auto Geometry_graph_node::get_preview_rotation() const -> float
{
    return m_preview_rotation;
}

auto Geometry_graph_node::get_preview_desired_texture_size() const -> int
{
    int size = 64;
    while ((static_cast<float>(size) < m_preview_display_size) && (size < 512)) {
        size *= 2;
    }
    return size;
}

void Geometry_graph_node::draw_preview(App_context& context)
{
    // Display geometry laid out in the zoomed node content, so the
    // on-screen size scales with the view (issue #251), fitted to the node's
    // available content space (center-column width, remaining height when
    // the node has a requested Size). The render-target resolution follows
    // it too (quantized; see get_preview_desired_texture_size), so a
    // zoomed-in or enlarged node is not upscaled from a low-resolution
    // render.
    const float size = get_preview_fit_size();
    m_preview_display_size = size;

    // Hovering the node spins the preview; the angle persists so the
    // model stops in place when the hover ends. Re-rendering goes through
    // the normal budgeted path (update_node_previews).
    if (m_is_hovered && m_preview_primitive) {
        m_preview_rotation += 1.5f * ImGui::GetIO().DeltaTime;
        if (m_preview_rotation > 6.2831853f) {
            m_preview_rotation -= 6.2831853f;
        }
        m_preview_needs_render = true;
    }

    if (!m_preview_texture || (context.imgui_renderer == nullptr)) {
        return;
    }
    context.imgui_renderer->image(
        erhe::imgui::Draw_texture_parameters{
            .texture_reference = m_preview_texture,
            .width             = static_cast<int>(size),
            .height            = static_cast<int>(size),
            .uv0               = context.imgui_renderer->get_rtt_uv0(),
            .uv1               = context.imgui_renderer->get_rtt_uv1(),
            .filter            = erhe::graphics::Filter::linear,
            .mipmap_mode       = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .debug_label       = "Geometry_graph_node preview"
        }
    );
}

auto Geometry_graph_node::get_log_id() const -> std::size_t
{
    return (m_log_source_id != 0) ? m_log_source_id : get_id();
}

void Geometry_graph_node::set_log_source_id(const std::size_t id)
{
    m_log_source_id = id;
}

auto Geometry_graph_node::get_owning_graph_mesh() const -> std::shared_ptr<Graph_mesh>
{
    return m_owning_graph_mesh.lock();
}

void Geometry_graph_node::set_owning_graph_mesh(const std::shared_ptr<Graph_mesh>& graph_mesh)
{
    m_owning_graph_mesh = graph_mesh;
}

}
