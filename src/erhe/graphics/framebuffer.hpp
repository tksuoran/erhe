#ifndef framebuffer_hpp_erhe_graphics
#define framebuffer_hpp_erhe_graphics

#include "erhe/graphics/gl_objects.hpp"

#include <gsl/pointers>
#include <optional>

#include <mutex>

namespace erhe::graphics
{

class Renderbuffer;
class Texture;

class Framebuffer
{
public:
    class Attachment
    {
    public:
        Attachment(gl::Framebuffer_attachment attachment_point,
                   gsl::not_null<Texture*>    texture,
                   unsigned int               level,
                   unsigned int               layer)
            : attachment_point{attachment_point}
            , texture_level   {level}
            , texture_layer   {layer}
            , texture         {texture}
        {
        }

        Attachment(gl::Framebuffer_attachment   attachment_point,
                   gsl::not_null<Renderbuffer*> renderbuffer)
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
        void attach(gl::Framebuffer_attachment attachment_point,
                    gsl::not_null<Texture*>    texture,
                    unsigned int               level = 0,
                    unsigned int               layer = 0);

        void attach(gl::Framebuffer_attachment   attachment_point,
                    gsl::not_null<Renderbuffer*> renderbuffer);

        std::vector<Attachment> attachments;
    };

    explicit Framebuffer(const Create_info& create_info);

    ~Framebuffer();

    Framebuffer(const Framebuffer&) = delete;

    auto operator=(const Framebuffer&)
    -> Framebuffer& = delete;

    Framebuffer(Framebuffer&& other) noexcept
        : m_gl_framebuffer{std::move(other.m_gl_framebuffer)}
    {
    }

    auto operator=(Framebuffer&& other) noexcept
    -> Framebuffer&
    {
        m_gl_framebuffer = std::move(other.m_gl_framebuffer);
        return *this;
    }

    static void on_thread_enter()
    {
        std::lock_guard lock{s_mutex};
        for (auto* framebuffer : s_all_framebuffers)
        {
            framebuffer->create();
        }
    }

    static void on_thread_exit()
    {
        std::lock_guard lock{s_mutex};
        gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, 0);
        gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
        for (auto* framebuffer : s_all_framebuffers)
        {
            framebuffer->reset();
        }
    }

    auto gl_name()
    -> unsigned int;

    void create();

    void reset();

    auto check_status()
    -> bool;

    void set_debug_label(const std::string& label)
    {
        gl::object_label(gl::Object_identifier::framebuffer,
                         gl_name(), static_cast<GLsizei>(label.length()), label.c_str());
    }

private:
    std::optional<Gl_framebuffer> m_gl_framebuffer;
    std::vector<Attachment>       m_attachments;

    static std::mutex                s_mutex;
    static std::vector<Framebuffer*> s_all_framebuffers;
};

} // namespace erhe::graphics

#endif
