#include "windows/viewport_window.hpp"
#include "tools.hpp"
#include "tools/pointer_context.hpp"
#include "log.hpp"

#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/renderbuffer.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/tracy_client.hpp"

#include "imgui.h"

namespace editor
{

using namespace erhe::graphics;
using namespace std;

namespace {

auto to_glm(ImVec2 v) -> glm::vec2
{
    return glm::ivec2(static_cast<int>(v[0]),
                      static_cast<int>(v[1]));
}

void dump_fbo_attachment(int fbo_name, gl::Framebuffer_attachment attachment)
{
    int type{0};
    gl::get_named_framebuffer_attachment_parameter_iv(
        fbo_name,
        attachment,
        gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_object_type,
        &type
    );
    if (type != GL_NONE)
    {
        int name{0};
        gl::get_named_framebuffer_attachment_parameter_iv(
            fbo_name,
            attachment,
            gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_object_name,
            &name
        );
        int samples        {0};
        int width          {0};
        int height         {0};
        int internal_format{0};
        if (type == GL_RENDERBUFFER)
        {
            gl::get_named_renderbuffer_parameter_iv(name, gl::Renderbuffer_parameter_name::renderbuffer_samples,         &samples);
            gl::get_named_renderbuffer_parameter_iv(name, gl::Renderbuffer_parameter_name::renderbuffer_width,           &width);
            gl::get_named_renderbuffer_parameter_iv(name, gl::Renderbuffer_parameter_name::renderbuffer_height,          &height);
            gl::get_named_renderbuffer_parameter_iv(name, gl::Renderbuffer_parameter_name::renderbuffer_internal_format, &internal_format);
        }
        if (type == GL_TEXTURE)
        {
            int level{0};
            gl::get_named_framebuffer_attachment_parameter_iv(
                fbo_name, attachment,
                gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_texture_level,
                &level
            );
            gl::get_texture_level_parameter_iv(name, level, gl::Get_texture_parameter::texture_width,           &width);
            gl::get_texture_level_parameter_iv(name, level, gl::Get_texture_parameter::texture_height,          &height);
            gl::get_texture_level_parameter_iv(name, level, gl::Get_texture_parameter::texture_internal_format, &internal_format);
            gl::get_texture_level_parameter_iv(
                name,
                level,
//              gl::Get_texture_parameter::texture_samples,  Missing from gl.xml :/
                static_cast<gl::Get_texture_parameter>(GL_TEXTURE_SAMPLES),
                &samples
            );
        }

        log_framebuffer.trace(
            "\t{} {} attachment {} samples = {} size = {} x {} format = {}\n",
            c_str(attachment),
            gl::enum_string(type),
            name,
            samples,
            width,
            height,
            gl::enum_string(internal_format)
        );
    }
}

void dump_fbo(int fbo_name)
{
    int samples       {0};
    int sample_buffers{0};
    gl::get_named_framebuffer_parameter_iv(fbo_name, gl::Get_framebuffer_parameter::samples, &samples);
    gl::get_named_framebuffer_parameter_iv(fbo_name, gl::Get_framebuffer_parameter::sample_buffers, &sample_buffers);

    log_framebuffer.trace("FBO {} uses {} samples {} sample buffers\n", fbo_name, samples, sample_buffers);

    dump_fbo_attachment(fbo_name, gl::Framebuffer_attachment::color_attachment0);
    dump_fbo_attachment(fbo_name, gl::Framebuffer_attachment::depth_attachment);
    dump_fbo_attachment(fbo_name, gl::Framebuffer_attachment::stencil_attachment);
}

}

Viewport_window::Viewport_window()
    : erhe::components::Component{c_name}
{
}

Viewport_window::~Viewport_window() = default;

void Viewport_window::initialize_component()
{
    get<Editor_tools>()->register_imgui_window(this);
}

void Viewport_window::imgui(Pointer_context& pointer_context)
{
    ZoneScoped;

    ImGui::Begin("Scene");

    const auto size = ImGui::GetContentRegionAvail();

    if (m_can_present &&
        m_color_texture_resolved_for_present &&
        (m_color_texture_resolved_for_present->width() > 0) &&
        (m_color_texture_resolved_for_present->height() > 0))
    {
        ImGui::Image(reinterpret_cast<ImTextureID>(
            m_color_texture_resolved_for_present.get()),
            size,
            ImVec2(0, 1),
            ImVec2(1, 0)
        );
        m_content_region_min  = to_glm(ImGui::GetItemRectMin());
        m_content_region_max  = to_glm(ImGui::GetItemRectMax());
        m_content_region_size = m_content_region_max - m_content_region_min;
        m_is_focused = ImGui::IsItemHovered(ImGuiHoveredFlags_None);
    }
    else
    {
        m_content_region_size = to_glm(size);
        m_is_focused = false;
    }
    pointer_context.scene_view_focus = m_is_focused;
    ImGui::End();
    update_framebuffer();
}

auto Viewport_window::content_region_size() -> glm::ivec2
{
    return m_content_region_size;
}

auto Viewport_window::is_focused() -> bool
{
    return m_is_focused;
}

auto Viewport_window::is_framebuffer_ready() -> bool
{
    return m_framebuffer_multisample.get() != nullptr;
}

void Viewport_window::bind_multisample_framebuffer()
{
    if (!m_framebuffer_multisample)
    {
        return;
    }

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer_multisample->gl_name());
    const auto status = gl::check_named_framebuffer_status(m_framebuffer_multisample->gl_name(), gl::Framebuffer_target::draw_framebuffer);
    if (status != gl::Framebuffer_status::framebuffer_complete)
    {
        log_framebuffer.error("draw framebuffer status = {}\n", c_str(status));
    }
    VERIFY(status == gl::Framebuffer_status::framebuffer_complete);
}

static constexpr std::string_view c_multisample_resolve{"Viewport_window::multisample_resolve()"};
void Viewport_window::multisample_resolve()
{
    ZoneScoped;
    TracyGpuZone(c_multisample_resolve.data());

    if (!m_framebuffer_multisample || !m_framebuffer_resolved)
    {
        return;
    }

    gl::push_debug_group(
        gl::Debug_source::debug_source_application,
        0,
        static_cast<GLsizei>(c_multisample_resolve.length()),
        c_multisample_resolve.data()
    );

    gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, m_framebuffer_multisample->gl_name());
    if constexpr (true)
    {
        const auto status = gl::check_named_framebuffer_status(m_framebuffer_multisample->gl_name(), gl::Framebuffer_target::draw_framebuffer);
        if (status != gl::Framebuffer_status::framebuffer_complete)
        {
            log_framebuffer.error("read framebuffer status = {}\n", c_str(status));
        }
        VERIFY(status == gl::Framebuffer_status::framebuffer_complete);
    }

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer_resolved->gl_name());
    if constexpr (true)
    {
        const auto status = gl::check_named_framebuffer_status(m_framebuffer_multisample->gl_name(), gl::Framebuffer_target::draw_framebuffer);
        if (status != gl::Framebuffer_status::framebuffer_complete)
        {
            log_framebuffer.error("draw framebuffer status = {}\n", c_str(status));
        }
        VERIFY(status == gl::Framebuffer_status::framebuffer_complete);
    }

    if constexpr (false)
    {
        log_framebuffer.trace("Read framebuffer:\n");
        dump_fbo(m_framebuffer_multisample->gl_name());

        log_framebuffer.trace("Draw framebuffer:\n");
        dump_fbo(m_framebuffer_resolved->gl_name());
    }

    gl::disable(gl::Enable_cap::scissor_test);
    gl::disable(gl::Enable_cap::framebuffer_srgb);
    gl::blit_framebuffer(
        0, 0, m_color_texture_multisample->width(), m_color_texture_multisample->height(),
        0, 0, m_color_texture_resolved   ->width(), m_color_texture_resolved   ->height(),
        gl::Clear_buffer_mask::color_buffer_bit, gl::Blit_framebuffer_filter::nearest
    );

    gl::pop_debug_group();

    m_color_texture_resolved_for_present = m_color_texture_resolved;
}

void Viewport_window::update_framebuffer()
{
    ZoneScoped;

    if (m_content_region_size.x < 1 || m_content_region_size.y < 1)
    {
        return;
    }

    if (m_framebuffer_multisample &&
        m_framebuffer_resolved    &&
        m_color_texture_resolved  &&
        (m_color_texture_resolved->width()  == m_content_region_size.x) &&
        (m_color_texture_resolved->height() == m_content_region_size.y))
    {
        return;
    }

    gl::bind_framebuffer(gl::Framebuffer_target::framebuffer, 0);

    {
        Texture::Create_info create_info;
        create_info.target          = s_sample_count > 0 ? gl::Texture_target::texture_2d_multisample
                                                         : gl::Texture_target::texture_2d;
        create_info.internal_format = gl::Internal_format::srgb8_alpha8;
        create_info.sample_count    = s_sample_count;
        create_info.width           = m_content_region_size.x;
        create_info.height          = m_content_region_size.y;
        m_color_texture_multisample = make_unique<Texture>(create_info);
        m_color_texture_multisample->set_debug_label("Viewport Window Multisample Color");
        const float clear_value[4] = {1.0f, 0.0f, 1.0f, 1.0f };
        gl::clear_tex_image(m_color_texture_multisample->gl_name(), 0, gl::Pixel_format::rgba, gl::Pixel_type::float_, &clear_value[0]);
    }

    {
        Texture::Create_info create_info;
        create_info.target          = gl::Texture_target::texture_2d;
        create_info.internal_format = gl::Internal_format::rgba8;
        create_info.sample_count    = 0;
        create_info.width           = m_content_region_size.x;
        create_info.height          = m_content_region_size.y;
        m_color_texture_resolved = make_shared<Texture>(create_info);
        m_color_texture_multisample->set_debug_label("Viewport Window Multisample Resolved");
        if (!m_can_present)
        {
            constexpr float clear_value[4] = {1.0f, 0.0f, 0.0f, 1.0f };
            gl::clear_tex_image(m_color_texture_resolved->gl_name(), 0, gl::Pixel_format::rgba, gl::Pixel_type::float_, &clear_value[0]);
            m_can_present = true;
        }
    }

    m_depth_stencil_renderbuffer = make_unique<Renderbuffer>(
        gl::Internal_format::depth24_stencil8,
        s_sample_count,
        m_content_region_size.x,
        m_content_region_size.y
    );
    m_depth_stencil_renderbuffer->set_debug_label("Viewport Window Depth-Stencil");
    {
        Framebuffer::Create_info create_info;
        create_info.attach(gl::Framebuffer_attachment::color_attachment0,  m_color_texture_multisample.get());
        create_info.attach(gl::Framebuffer_attachment::depth_attachment,   m_depth_stencil_renderbuffer.get());
        create_info.attach(gl::Framebuffer_attachment::stencil_attachment, m_depth_stencil_renderbuffer.get());
        m_framebuffer_multisample = make_unique<Framebuffer>(create_info);
        m_framebuffer_multisample->set_debug_label("Viewport Window Multisample");

        gl::Color_buffer draw_buffers[] = { gl::Color_buffer::color_attachment0};
        gl::named_framebuffer_draw_buffers(m_framebuffer_multisample->gl_name(), 1, &draw_buffers[0]);
        gl::named_framebuffer_read_buffer (m_framebuffer_multisample->gl_name(), gl::Color_buffer::color_attachment0);

        log_framebuffer.trace("Multisample framebuffer:\n");
        dump_fbo(m_framebuffer_multisample->gl_name());
    }

    {
        Framebuffer::Create_info create_info;
        create_info.attach(gl::Framebuffer_attachment::color_attachment0, m_color_texture_resolved.get());
        m_framebuffer_resolved = make_unique<Framebuffer>(create_info);
        m_framebuffer_resolved->set_debug_label("Viewport Window Multisample Resolved");

        constexpr gl::Color_buffer draw_buffers[] = { gl::Color_buffer::color_attachment0};
        gl::named_framebuffer_draw_buffers(m_framebuffer_resolved->gl_name(), 1, &draw_buffers[0]);

        log_framebuffer.trace("Multisample resolved framebuffer:\n");
        dump_fbo(m_framebuffer_resolved->gl_name());
    }
}

glm::vec2 Viewport_window::to_scene_content(glm::vec2 position_in_root)
{
    const float content_x      = static_cast<float>(position_in_root.x) - m_content_region_min.x;
    const float content_y      = static_cast<float>(position_in_root.y) - m_content_region_min.y;
    const float content_flip_y = m_content_region_size.y - content_y;
    return { content_x, content_flip_y };
}

} // namespace editor
