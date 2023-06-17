#include "erhe/graphics/framebuffer.hpp"
#include "erhe/gl/enum_string_functions.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/graphics_log.hpp"
#include "erhe/graphics/instance.hpp"
#include "erhe/graphics/renderbuffer.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/toolkit/verify.hpp"
#include <fmt/format.h>
#include <thread>

namespace erhe::graphics
{

namespace {

void dump_fbo_attachment(
    int                        fbo_name,
    gl::Framebuffer_attachment attachment
)
{
    int type{0};
    gl::get_named_framebuffer_attachment_parameter_iv(
        fbo_name,
        attachment,
        gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_object_type,
        &type
    );
    if (type != GL_NONE) {
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
        if (type == GL_RENDERBUFFER) {
            gl::get_named_renderbuffer_parameter_iv(name, gl::Renderbuffer_parameter_name::renderbuffer_samples,         &samples);
            gl::get_named_renderbuffer_parameter_iv(name, gl::Renderbuffer_parameter_name::renderbuffer_width,           &width);
            gl::get_named_renderbuffer_parameter_iv(name, gl::Renderbuffer_parameter_name::renderbuffer_height,          &height);
            gl::get_named_renderbuffer_parameter_iv(name, gl::Renderbuffer_parameter_name::renderbuffer_internal_format, &internal_format);
        }
        if (type == GL_TEXTURE) {
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

        log_framebuffer->trace(
            "\t{} {} attachment {} samples = {} size = {} x {} format = {}",
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

    log_framebuffer->trace(
        "FBO {} uses {} samples {} sample buffers",
        fbo_name,
        samples,
        sample_buffers
    );

    dump_fbo_attachment(fbo_name, gl::Framebuffer_attachment::color_attachment0);
    dump_fbo_attachment(fbo_name, gl::Framebuffer_attachment::depth_attachment);
    dump_fbo_attachment(fbo_name, gl::Framebuffer_attachment::stencil_attachment);
}

}

std::mutex                Framebuffer::s_mutex;
std::vector<Framebuffer*> Framebuffer::s_all_framebuffers;

void Framebuffer::Create_info::attach(
    const gl::Framebuffer_attachment attachment_point,
    const gsl::not_null<Texture*>    texture,
    const unsigned int               level,
    const unsigned int               layer
)
{
    attachments.emplace_back(attachment_point, texture, level, layer);
}

void Framebuffer::Create_info::attach(
    const gl::Framebuffer_attachment   attachment_point,
    const gsl::not_null<Renderbuffer*> renderbuffer)
{
    attachments.emplace_back(attachment_point, renderbuffer);
}

Framebuffer::Framebuffer(const Create_info& create_info)
    : m_attachments{create_info.attachments}
{
    const std::lock_guard lock{s_mutex};

    s_all_framebuffers.push_back(this);

    create();
}

Framebuffer::~Framebuffer() noexcept
{
    const std::lock_guard lock{s_mutex};

    s_all_framebuffers.erase(
        std::remove(
            s_all_framebuffers.begin(),
            s_all_framebuffers.end(),
            this
        ),
        s_all_framebuffers.end()
    );
}

void Framebuffer::on_thread_enter()
{
    const std::lock_guard lock{s_mutex};

    for (auto* framebuffer : s_all_framebuffers) {
        if (framebuffer->m_owner_thread == std::thread::id{}) {
            framebuffer->create();
        }
    }
}

void Framebuffer::on_thread_exit()
{
    const std::lock_guard lock{s_mutex};

    gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, 0);
    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
    auto this_thread_id = std::this_thread::get_id();
    for (auto* framebuffer : s_all_framebuffers) {
        if (framebuffer->m_owner_thread == this_thread_id) {
            framebuffer->reset();
        }
    }
}

void Framebuffer::reset()
{
    m_owner_thread = {};
    m_gl_framebuffer.reset();
}

void Framebuffer::create()
{
    if (m_gl_framebuffer.has_value()) {
        ERHE_VERIFY(m_owner_thread == std::this_thread::get_id());
        return;
    }

    m_owner_thread = std::this_thread::get_id();
    m_gl_framebuffer.emplace(Gl_framebuffer{});
    for (auto& attachment : m_attachments) {
        if (attachment.texture != nullptr) {
            ERHE_VERIFY(attachment.texture->width() >= 1);
            ERHE_VERIFY(attachment.texture->height() >= 1);
            if (attachment.texture->is_layered()) {
                gl::named_framebuffer_texture_layer(
                    gl_name(),
                    attachment.attachment_point,
                    attachment.texture->gl_name(),
                    attachment.texture_level,
                    attachment.texture_layer
                );
            } else {
                gl::named_framebuffer_texture(
                    gl_name(),
                    attachment.attachment_point,
                    attachment.texture->gl_name(),
                    attachment.texture_level
                );
            }
        } else if (attachment.renderbuffer != nullptr) {
            ERHE_VERIFY(attachment.renderbuffer->width() >= 1);
            ERHE_VERIFY(attachment.renderbuffer->height() >= 1);
            gl::named_framebuffer_renderbuffer(
                gl_name(),
                attachment.attachment_point,
                gl::Renderbuffer_target::renderbuffer,
                attachment.renderbuffer->gl_name()
            );
        }
    }

    Ensures(check_status());
}

auto Framebuffer::check_status() const -> bool
{
#if !defined(NDEBUG)
    const auto status = gl::check_named_framebuffer_status(
        gl_name(),
        gl::Framebuffer_target::draw_framebuffer
    );
    if (status != gl::Framebuffer_status::framebuffer_complete) {
        log_framebuffer->warn(
            "Framebuffer {} not complete: {}",
            gl_name(),
            gl::c_str(status)
        );
        dump_fbo(gl_name());
        return false;
    }
    return status == gl::Framebuffer_status::framebuffer_complete;
#else
    return true;
#endif
}

auto Framebuffer::gl_name() const -> unsigned int
{
    return m_gl_framebuffer.has_value()
        ? m_gl_framebuffer.value().gl_name()
        : 0;
}

void Framebuffer::set_debug_label(const std::string& label)
{
    m_debug_label = fmt::format("(F:{}) {}", gl_name(), label);
    gl::object_label(
        gl::Object_identifier::framebuffer,
        gl_name(),
        static_cast<GLsizei>(m_debug_label.length()),
        m_debug_label.c_str()
    );
}

} // namespace erhe::graphics
