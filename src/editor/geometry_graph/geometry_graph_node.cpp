#include "geometry_graph/geometry_graph_node.hpp"
#include "geometry_graph/geometry_graph_operations.hpp"
#include "geometry_graph/geometry_graph_window.hpp"
#include "app_context.hpp"
#include "operations/operation_stack.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_imgui/imgui_node_editor.h"

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

void Geometry_graph_node::on_removed_from_graph()
{
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
