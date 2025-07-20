#include "preview/scene_preview.hpp"

#include "app_context.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "renderers/viewport_config.hpp"
#include "scene/scene_root.hpp"
#include "content_library/content_library.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/renderbuffer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/scene.hpp"

#include <fmt/format.h>

namespace editor {

using Vertex_input_state   = erhe::graphics::Vertex_input_state;
using Input_assembly_state = erhe::graphics::Input_assembly_state;
using Rasterization_state  = erhe::graphics::Rasterization_state;
using Depth_stencil_state  = erhe::graphics::Depth_stencil_state;
using Color_blend_state    = erhe::graphics::Color_blend_state;

Scene_preview::Scene_preview(
    erhe::graphics::Device&         graphics_device,
    erhe::scene::Scene_message_bus& scene_message_bus,
    App_context&                    context,
    Mesh_memory&                    mesh_memory,
    Programs&                       programs
)
    : Scene_view       {context, Viewport_config{}}
    , m_graphics_device{graphics_device}
    , m_pipeline_pass  {erhe::graphics::Render_pipeline_state{{
        .name           = "Polygon Fill Opaque",
        .shader_stages  = &programs.standard.shader_stages,
        .vertex_input   = &mesh_memory.vertex_input,
        .input_assembly = Input_assembly_state::triangle,
        .rasterization  = Rasterization_state::cull_mode_back_ccw,
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(),
        .color_blend    = Color_blend_state::color_blend_disabled
    }}}
    , m_composer{"Material Preview Composer"}
{
    ERHE_PROFILE_FUNCTION();

    m_content_library = std::make_shared<Content_library>();

    m_scene_root = std::make_shared<Scene_root>(
        nullptr, // No Imgui_renderer
        nullptr, // No Imgui_windows
        scene_message_bus,
        nullptr, // No App_context
        nullptr, // Don't process editor messages
        nullptr, // Don't register to App_scenes
        m_content_library,
        "Material preview scene"
    );

    m_scene_root->get_scene().disable_flag_bits(erhe::Item_flags::show_in_ui);

    // For now, shadow texture is dummy 1 by 1 pixel only cleared
    m_shadow_texture = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info {
            .device            = graphics_device,
            .type              = erhe::graphics::Texture_type::texture_2d,
            .pixelformat       = erhe::dataformat::Format::format_d32_sfloat,
            .width             = 1,
            .height            = 1,
            .depth             = 1,
            .array_layer_count = 1,
            .debug_label       = "Scene_preview::m_shadow_texture (dummy shadowmap)"
        }
    );

    const double depth_clear_value = 0.0; // reverse Z
    graphics_device.clear_texture(*m_shadow_texture.get(), { depth_clear_value, 0.0, 0.0, 0.0 });
}

Scene_preview::~Scene_preview()
{
}

void Scene_preview::resize(int width, int height)
{
    m_width  = std::max(1, width);
    m_height = std::max(1, height);
}

void Scene_preview::set_color_texture(const std::shared_ptr<erhe::graphics::Texture>& color_texture)
{
    m_use_external_color_texture = static_cast<bool>(color_texture);
    if (m_color_texture != color_texture) {
        m_render_pass.reset();
    }
    m_color_texture = color_texture;
}

void Scene_preview::set_clear_color(glm::vec4 clear_color)
{
    m_clear_color = clear_color;
}

void Scene_preview::update_rendertarget(erhe::graphics::Device& graphics_device)
{
    ERHE_PROFILE_FUNCTION();

    bool attachment_changed = false;
    if (
        !m_use_external_color_texture &&
        (
            !m_color_texture ||
            (m_color_texture->get_width () != m_width) ||
            (m_color_texture->get_height() != m_height)
        )
    ) {
        m_color_format = erhe::dataformat::Format::format_16_vec4_float;
        m_color_texture.reset();
        m_color_texture = std::make_shared<erhe::graphics::Texture>(
            graphics_device,
            erhe::graphics::Texture_create_info{
                .device      = graphics_device,
                .type        = erhe::graphics::Texture_type::texture_2d,
                .pixelformat = m_color_format,
                .width       = m_width,
                .height      = m_height,
                .debug_label = "Preview Color Texture"
            }
        );
        graphics_device.clear_texture(*m_color_texture.get(), { 1.0, 0.0, 0.5, 0.0 });
        attachment_changed = true;
    }

    if (
        !m_depth_renderbuffer ||
        (m_depth_renderbuffer->get_width() != m_width) ||
        (m_depth_renderbuffer->get_height() != m_height)
    ) {
        m_depth_format = erhe::dataformat::Format::format_d32_sfloat;
        using Render_pass  = erhe::graphics::Render_pass;
        using Renderbuffer = erhe::graphics::Renderbuffer;
        using Texture      = erhe::graphics::Texture;
        m_depth_renderbuffer = std::make_unique<erhe::graphics::Renderbuffer>(
            graphics_device,
            m_depth_format,
            m_width,
            m_height
        );
        m_depth_renderbuffer->set_debug_label("Material Preview Depth Renderbuffer");
        attachment_changed = true;
    }

    if (!attachment_changed && m_render_pass) {
        return;
    }

    m_render_pass.reset();
    erhe::graphics::Render_pass_descriptor render_pass_descriptor; 
    render_pass_descriptor.color_attachments[0].texture      = m_color_texture.get();
    render_pass_descriptor.color_attachments[0].load_action  = erhe::graphics::Load_action::Clear;
    render_pass_descriptor.color_attachments[0].store_action = erhe::graphics::Store_action::Store;
    render_pass_descriptor.color_attachments[0].clear_value  = { m_clear_color.x, m_clear_color.y, m_clear_color.z, m_clear_color.w };
    render_pass_descriptor.depth_attachment.renderbuffer     = m_depth_renderbuffer.get();
    render_pass_descriptor.depth_attachment.load_action      = erhe::graphics::Load_action::Clear;
    render_pass_descriptor.depth_attachment.clear_value[0]   = 0.0; // Reverse Z
    render_pass_descriptor.depth_attachment.store_action     = erhe::graphics::Store_action::Dont_care;
    render_pass_descriptor.render_target_width               = m_width;
    render_pass_descriptor.render_target_height              = m_height;
    render_pass_descriptor.debug_label                       = "Preview Render_pass";
    m_render_pass = std::make_unique<erhe::graphics::Render_pass>(graphics_device, render_pass_descriptor);
}

auto Scene_preview::get_scene_root() const -> std::shared_ptr<Scene_root>
{
    return m_scene_root;
}

auto Scene_preview::get_camera() const -> std::shared_ptr<erhe::scene::Camera>
{
    return m_camera;
}

auto Scene_preview::get_perspective_scale() const -> float
{
    return 1.0f; // TODO
}

auto Scene_preview::get_rendergraph_node() -> erhe::rendergraph::Rendergraph_node*
{
    return {};
}

auto Scene_preview::get_light_projections() const -> const erhe::scene_renderer::Light_projections*
{
    return &m_light_projections;
}

auto Scene_preview::get_shadow_texture() const -> erhe::graphics::Texture*
{
    return m_shadow_texture.get();
}

auto Scene_preview::get_content_library() -> std::shared_ptr<Content_library>
{
    return m_content_library;
}

}
