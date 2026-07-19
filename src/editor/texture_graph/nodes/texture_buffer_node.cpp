#include "texture_graph/nodes/texture_buffer_node.hpp"

#include "graph_editor/graph_editor_widgets.hpp"

#include "texture_graph/texture_graph_compose.hpp"
#include "texture_graph/texture_payload.hpp"
#include "texture_graph/texture_renderer.hpp"

#include "app_context.hpp"

#include "erhe_texgen/value_type.hpp"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>

namespace editor {

namespace {

constexpr const char* c_size_names [] = { "128", "256", "512", "1024", "2048" };
constexpr int         c_size_values[] = {  128,   256,   512,   1024,   2048  };
constexpr int         c_size_count   = 5;

} // namespace

Texture_buffer_node::Texture_buffer_node(App_context& context)
    : Texture_graph_node{"Buffer"}
{
    // context is unused during construction (the part-ctor rule forbids reading
    // App_context here); render_products() receives it as an argument instead.
    static_cast<void>(context);
    // Typed pass-through pins: one input + one output per value type, so a
    // grayscale / rgb / rgba source can each feed the buffer and be sampled back
    // at the matching type. (Pin keys are per-type; a single-type pin would
    // reject the others.)
    make_input_pin (Texture_pin_key::grayscale, "f");
    make_input_pin (Texture_pin_key::rgb,       "rgb");
    make_input_pin (Texture_pin_key::rgba,      "rgba");
    make_output_pin(Texture_pin_key::grayscale, "f");
    make_output_pin(Texture_pin_key::rgb,       "rgb");
    make_output_pin(Texture_pin_key::rgba,      "rgba");
}

auto Texture_buffer_node::is_buffer() const -> bool
{
    return true;
}

auto Texture_buffer_node::preview_output_index() const -> int
{
    // render_products() is fully overridden (it renders the buffer's own input
    // subtree into its texture), so the base preview path is not used.
    return -1;
}

auto Texture_buffer_node::render_target_size() const -> int
{
    const int index = std::clamp(m_size_index, 0, c_size_count - 1);
    return c_size_values[index];
}

auto Texture_buffer_node::connected_input_index() const -> int
{
    // Prefer the highest channel count when several inputs are connected.
    for (int i = 2; i >= 0; --i) {
        if (input_from_links(static_cast<std::size_t>(i)).source_node != nullptr) {
            return i;
        }
    }
    return -1;
}

void Texture_buffer_node::evaluate(Texture_graph&)
{
    pull_inputs();
    // Each output slot advertises this live node as its producer with its slot's
    // value type; the compose DAG turns that into a sampler-source reading this
    // buffer's rendered texture (converted to the slot type).
    const erhe::texgen::Value_type output_types[3] = {
        erhe::texgen::Value_type::grayscale,
        erhe::texgen::Value_type::rgb,
        erhe::texgen::Value_type::rgba
    };
    for (std::size_t i = 0; i < 3; ++i) {
        Texture_payload payload{};
        payload.source_node  = this;
        payload.output_index = i;
        payload.value_type   = output_types[i];
        set_output(i, payload);
    }
}

void Texture_buffer_node::render_products(App_context& context, Texture_renderer& renderer)
{
    if (context.current_command_buffer == nullptr) {
        return;
    }
    if (m_pause && get_preview_texture()) {
        return; // paused - keep the last rendered texture
    }
    const int input_index = connected_input_index();
    if (input_index < 0) {
        return; // disconnected - keep the last texture
    }
    const Texture_payload source = input_from_links(static_cast<std::size_t>(input_index));
    if (source.source_node == nullptr) {
        return;
    }
    // Render the input subtree (never this buffer itself) into our own texture.
    const Texture_compose_dag dag = build_texture_compose_dag(*source.source_node, source.output_index);
    static_cast<void>(render_dag(context, renderer, dag, get_preview_texture_ref(), render_target_size()));
}

void Texture_buffer_node::imgui()
{
    ImGui::TextUnformatted("Resolution");
    if (imgui_enum_combo("size", m_size_index, c_size_names, c_size_count, content_scale())) {
        mark_dirty();
    }
    if (ImGui::Checkbox("Pause", &m_pause)) {
        mark_dirty();
    }
}

void Texture_buffer_node::write_parameters(nlohmann::json& out) const
{
    out["size"]  = render_target_size();
    out["pause"] = m_pause;
}

void Texture_buffer_node::read_parameters(const nlohmann::json& in)
{
    if (in.contains("size")) {
        const int size = in["size"].get<int>();
        for (int i = 0; i < c_size_count; ++i) {
            if (c_size_values[i] == size) {
                m_size_index = i;
                break;
            }
        }
    }
    m_pause = in.value("pause", m_pause);
    mark_dirty();
}

} // namespace editor
