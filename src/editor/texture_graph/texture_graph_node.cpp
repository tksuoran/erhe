#include "texture_graph/texture_graph_node.hpp"
#include "texture_graph/texture_graph_compose.hpp"
#include "texture_graph/texture_graph_operations.hpp"
#include "texture_graph/texture_graph_window.hpp"
#include "texture_graph/texture_renderer.hpp"
#include "app_context.hpp"
#include "operations/operation_stack.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_texgen/compose_node.hpp"
#include "erhe_texgen/shader_code.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_imgui/imgui_node_editor.h"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_texgen/node_descriptor.hpp"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

namespace editor {

// Pin fill color per Texture_pin_key value type. Pins only accept links from
// pins with the same key, so matching colors show which connections are legal
// before a drag gets rejected.
auto Texture_graph_node::pin_key_color(const std::size_t key) const -> ImU32
{
    switch (key) {
        case Texture_pin_key::grayscale: return IM_COL32(200, 200, 200, 255); // light grey
        case Texture_pin_key::rgb:       return IM_COL32( 90, 160, 235, 255); // blue
        case Texture_pin_key::rgba:      return IM_COL32(120, 205, 150, 255); // green
        default:                         return IM_COL32( 68,  68,  68, 255);
    }
}

void Texture_graph_node::commit_parameter_operation(App_context& app_context, std::string&& before_parameters, std::string&& after_parameters)
{
    app_context.operation_stack->execute_now(
        std::make_shared<Texture_graph_parameter_operation>(
            *app_context.texture_graph_window,
            std::dynamic_pointer_cast<Texture_graph_node>(node_from_this()),
            std::move(before_parameters),
            std::move(after_parameters)
        )
    );
}

void Texture_graph_node::after_node_content(App_context& app_context)
{
    draw_preview(app_context);
}

Texture_graph_node::Texture_graph_node(const char* label)
    : Graph_editor_node{label}
{
}

auto Texture_graph_node::input_from_links(const std::size_t i) const -> Texture_payload
{
    const erhe::graph::Pin& input_pin = get_input_pins().at(i);
    Texture_payload result{};
    for (erhe::graph::Link* link : input_pin.get_links()) {
        erhe::graph::Pin*   source_pin  = link->get_source();
        const std::size_t   slot        = source_pin->get_slot();
        erhe::graph::Node*  source_node = source_pin->get_owner_node();
        Texture_graph_node* source_texture_graph_node = dynamic_cast<Texture_graph_node*>(source_node);
        if (source_texture_graph_node != nullptr) {
            // MVP inputs take a single link; the last connected source wins.
            result = source_texture_graph_node->get_output(slot);
        }
    }
    return result;
}

void Texture_graph_node::pull_inputs()
{
    for (std::size_t i = 0, end = m_input_payloads.size(); i < end; ++i) {
        m_input_payloads[i] = input_from_links(i);
    }
}

auto Texture_graph_node::get_input(const std::size_t i) const -> const Texture_payload&
{
    return m_input_payloads.at(i);
}

auto Texture_graph_node::get_output(const std::size_t i) const -> const Texture_payload&
{
    return m_output_payloads.at(i);
}

void Texture_graph_node::set_input(const std::size_t i, const Texture_payload& payload)
{
    m_input_payloads.at(i) = payload;
}

void Texture_graph_node::set_output(const std::size_t i, const Texture_payload& payload)
{
    m_output_payloads.at(i) = payload;
}

void Texture_graph_node::make_input_pin(const std::size_t key, const std::string_view name)
{
    m_input_payloads.emplace_back();
    base_make_input_pin(key, name);
}

void Texture_graph_node::make_output_pin(const std::size_t key, const std::string_view name)
{
    m_output_payloads.emplace_back();
    base_make_output_pin(key, name);
}

void Texture_graph_node::mark_dirty()
{
    m_dirty                = true;
    m_preview_needs_render = true;
}

auto Texture_graph_node::preview_needs_render() const -> bool
{
    return m_preview_needs_render;
}

void Texture_graph_node::clear_preview_needs_render()
{
    m_preview_needs_render = false;
}

auto Texture_graph_node::preview_output_index() const -> int
{
    return get_output_pins().empty() ? -1 : 0;
}

auto Texture_graph_node::get_preview_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&
{
    return m_preview_texture;
}

auto Texture_graph_node::get_preview_texture_ref() -> std::shared_ptr<erhe::graphics::Texture>&
{
    return m_preview_texture;
}

auto Texture_graph_node::preview_display_size() const -> float
{
    return 96.0f;
}

auto Texture_graph_node::render_target_size() const -> int
{
    return 128;
}

auto Texture_graph_node::render_dag(
    App_context&                              context,
    Texture_renderer&                         renderer,
    const Texture_compose_dag&                dag,
    std::shared_ptr<erhe::graphics::Texture>& target,
    const int                                 size
) -> bool
{
    if (!dag.ok || (dag.sink == nullptr) || (context.current_command_buffer == nullptr)) {
        return false;
    }
    const erhe::texgen::Composer    composer{texture_graph_compose_options()};
    const erhe::texgen::Shader_code shader_code = composer.compose(*dag.sink, dag.sink_output_index);
    const std::string               fragment    = composer.assemble_fragment(shader_code);
    if (fragment.find("(error:") != std::string::npos) {
        return false; // composition failed (cycle / depth) - keep the previous texture
    }
    // Resolve each buffer cut point to its rendered texture. This only allocates
    // when the composition samples buffers (not steady state, and only on dirty
    // nodes), so it stays off the hot path.
    std::vector<Texture_sample_binding> sampler_bindings;
    sampler_bindings.reserve(dag.sampler_sources.size());
    for (const Texture_sampler_source& sampler_source : dag.sampler_sources) {
        Texture_sample_binding binding{};
        binding.binding = sampler_source.binding;
        binding.name    = std::string{"tex_"} + std::to_string(sampler_source.binding);
        binding.texture = sampler_source.buffer_node->get_preview_texture().get();
        sampler_bindings.push_back(binding);
    }
    return renderer.render_into(
        *context.current_command_buffer,
        target,
        size,
        fragment,
        shader_code.get_uniforms(),
        shader_code.get_samplers(),
        sampler_bindings
    );
}

void Texture_graph_node::render_products(App_context& context, Texture_renderer& renderer)
{
    const int output_index = preview_output_index();
    if ((descriptor() == nullptr) || (output_index < 0)) {
        return;
    }
    const Texture_compose_dag dag = build_texture_compose_dag(*this, static_cast<std::size_t>(output_index));
    static_cast<void>(render_dag(context, renderer, dag, m_preview_texture, render_target_size()));
}

auto Texture_graph_node::is_buffer() const -> bool
{
    return false;
}

void Texture_graph_node::evaluate(Texture_graph&)
{
    // Overridden in derived classes.
}

auto Texture_graph_node::descriptor() const -> const erhe::texgen::Node_descriptor*
{
    return nullptr;
}

void Texture_graph_node::configure(erhe::texgen::Compose_node&) const
{
}

void Texture_graph_node::build_pins_from_descriptor(const erhe::texgen::Node_descriptor& descriptor)
{
    for (const erhe::texgen::Input_descriptor& input : descriptor.inputs) {
        make_input_pin(value_type_to_pin_key(input.type), input.name);
    }
    for (const erhe::texgen::Output_descriptor& output : descriptor.outputs) {
        make_output_pin(value_type_to_pin_key(output.type), erhe::texgen::value_type_name(output.type));
    }
}

void Texture_graph_node::on_removed_from_graph()
{
}

void Texture_graph_node::set_owning_graph_texture(const std::weak_ptr<Graph_texture>& graph_texture)
{
    m_owning_graph_texture = graph_texture;
}

auto Texture_graph_node::get_owning_graph_texture() const -> std::shared_ptr<Graph_texture>
{
    return m_owning_graph_texture.lock();
}

void Texture_graph_node::draw_preview(App_context& app_context)
{
    if (!m_preview_texture || (app_context.imgui_renderer == nullptr)) {
        return;
    }
    // Issue #251: the preview thumbnail is display geometry (an ImGui image
    // widget) laid out in the zoomed node content, so its on-screen size scales
    // with the view. The render-target resolution (render_target_size) is a GPU
    // texture size and deliberately does NOT scale.
    const float size = preview_display_size() * m_content_scale;
    app_context.imgui_renderer->image(
        erhe::imgui::Draw_texture_parameters{
            .texture_reference = m_preview_texture,
            .width             = static_cast<int>(size),
            .height            = static_cast<int>(size),
            .uv0               = app_context.imgui_renderer->get_rtt_uv0(),
            .uv1               = app_context.imgui_renderer->get_rtt_uv1(),
            .filter            = erhe::graphics::Filter::linear,
            .mipmap_mode       = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .debug_label       = "Texture_graph_node preview"
        }
    );
}

} // namespace editor
