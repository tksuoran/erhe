#pragma once

#include "erhe/graphics/gl_objects.hpp"

#include <gsl/pointers>
#include <optional>

#include <mutex>
#include <vector>

namespace erhe::graphics
{

class Renderbuffer;
class Texture;

class Framebuffer final
{
public:
    class Attachment
    {
    public:
        Attachment(const gl::Framebuffer_attachment attachment_point,
                   const gsl::not_null<Texture*>    texture,
                   const unsigned int               level,
                   const unsigned int               layer)
            : attachment_point{attachment_point}
            , texture_level   {level}
            , texture_layer   {layer}
            , texture         {texture}
        {
        }

        Attachment(const gl::Framebuffer_attachment   attachment_point,
                   const gsl::not_null<Renderbuffer*> renderbuffer)
            : attachment_point{attachment_point}
            , renderbuffer    {renderbuffer}
        {
        }

        gl::Framebuffer_attachment attachment_point;
        unsigned int               texture_level{0};
        unsigned int               texture_layer{0};
        Texture*                   texture     {nullptr};
        Renderbuffer*              renderbuffer{nullptr};
    };

    class Create_info
    {
    public:
        void attach(const gl::Framebuffer_attachment attachment_point,
                    const gsl::not_null<Texture*>    texture,
                    const unsigned int               level = 0,
                    const unsigned int               layer = 0);

        void attach(const gl::Framebuffer_attachment   attachment_point,
                    const gsl::not_null<Renderbuffer*> renderbuffer);

        std::vector<Attachment> attachments;
    };

    explicit Framebuffer(const Create_info& create_info);
    ~Framebuffer        ();
    Framebuffer         (const Framebuffer&) = delete;
    void operator=      (const Framebuffer&) = delete;
    Framebuffer         (Framebuffer&&)      = delete;
    void operator=      (Framebuffer&&)      = delete;

    static void on_thread_enter()
    {
        std::lock_guard lock{s_mutex};

        for (auto* framebuffer : s_all_framebuffers)
        {
            if (framebuffer->m_owner_thread == std::thread::id{})
            {
                framebuffer->create();
            }
        }
    }

    static void on_thread_exit()
    {
        std::lock_guard lock{s_mutex};

        gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, 0);
        gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
        auto this_thread_id = std::this_thread::get_id();
        for (auto* framebuffer : s_all_framebuffers)
        {
            if (framebuffer->m_owner_thread == this_thread_id)
            {
                framebuffer->reset();
            }
        }
    }

    auto gl_name     () const -> unsigned int;
    void create      ();
    void reset       ();
    auto check_status() const -> bool;

    void set_debug_label(const std::string& label)
    {
        gl::object_label(gl::Object_identifier::framebuffer,
                         gl_name(), static_cast<GLsizei>(label.length()), label.c_str());
    }

private:
    std::optional<Gl_framebuffer> m_gl_framebuffer;
    std::vector<Attachment>       m_attachments;
    std::thread::id               m_owner_thread;

    static std::mutex                s_mutex;
    static std::vector<Framebuffer*> s_all_framebuffers;
};

} // namespace erhe::graphics
