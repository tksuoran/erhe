#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/renderbuffer.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/graphics/configuration.hpp"

namespace erhe::graphics
{

std::mutex                Framebuffer::s_mutex;
std::vector<Framebuffer*> Framebuffer::s_all_framebuffers;

void Framebuffer::Create_info::attach(gl::Framebuffer_attachment attachment_point,
                                      gsl::not_null<Texture*>    texture,
                                      unsigned int               level,
                                      unsigned int               layer)
{
    attachments.emplace_back(attachment_point, texture, level, layer);
}

void Framebuffer::Create_info::attach(gl::Framebuffer_attachment   attachment_point,
                                      gsl::not_null<Renderbuffer*> renderbuffer)
{
    attachments.emplace_back(attachment_point, renderbuffer);
}

Framebuffer::Framebuffer(const Create_info& create_info)
    : m_attachments{create_info.attachments}
{
    std::lock_guard lock{s_mutex};
    s_all_framebuffers.push_back(this);

    create();
}

Framebuffer::~Framebuffer()
{
    std::lock_guard lock{s_mutex};
    s_all_framebuffers.erase(std::remove(s_all_framebuffers.begin(),
                                         s_all_framebuffers.end(),
                                         this),
                             s_all_framebuffers.end());
}

void Framebuffer::reset()
{
    m_gl_framebuffer.reset();
}

void Framebuffer::create()
{
    if (m_gl_framebuffer.has_value())
    {
        return;
    }

    m_gl_framebuffer.emplace(Gl_framebuffer{});
    for (auto& attachment : m_attachments)
    {
        if (attachment.texture != nullptr)
        {
            if (attachment.texture->sample_count() > 0)
            {
                // Workaround suspected NVIDIA driver bug.
                auto target = attachment.texture->target();
                gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, gl_name());
                gl::framebuffer_texture_2d(gl::Framebuffer_target::draw_framebuffer,
                                           attachment.attachment_point,
                                           target,
                                           attachment.texture->gl_name(),
                                           attachment.texture_level);
            }
            else
            if (attachment.texture->is_layered())
            {
                gl::named_framebuffer_texture_layer(gl_name(),
                                                    attachment.attachment_point,
                                                    attachment.texture->gl_name(),
                                                    attachment.texture_level,
                                                    attachment.texture_layer);
            }
            else
            {
                // configuration::s_glNamedFramebufferTextureEXT(gl_name(),
                //                                               static_cast<GLenum>(attachment.attachment_point),
                //                                               attachment.texture->gl_name(),
                //                                               attachment.texture_level);
                gl::named_framebuffer_texture(gl_name(),
                                              attachment.attachment_point,
                                              attachment.texture->gl_name(),
                                              attachment.texture_level);
            }
        }
        else if (attachment.renderbuffer != nullptr)
        {
            gl::named_framebuffer_renderbuffer(gl_name(),
                                               attachment.attachment_point,
                                               gl::Renderbuffer_target::renderbuffer,
                                               attachment.renderbuffer->gl_name());
        }
    }

    Ensures(check_status());
}

auto Framebuffer::check_status()
-> bool
{
    auto status = gl::check_named_framebuffer_status(gl_name(), gl::Framebuffer_target::draw_framebuffer);
    return status == gl::Framebuffer_status::framebuffer_complete;
}

auto Framebuffer::gl_name()
-> unsigned int
{
    return m_gl_framebuffer.has_value() ? m_gl_framebuffer.value().gl_name()
                                        : 0;
}

} // namespace erhe::graphics
