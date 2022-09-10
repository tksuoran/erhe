#pragma once

#include "erhe/graphics/gl_objects.hpp"

#include <gsl/pointers>

#include <mutex>
#include <optional>
#include <thread>
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
        Attachment(
            const gl::Framebuffer_attachment attachment_point,
            const gsl::not_null<Texture*>    texture,
            const unsigned int               level,
            const unsigned int               layer
        )
            : attachment_point{attachment_point}
            , texture_level   {level}
            , texture_layer   {layer}
            , texture         {texture}
        {
        }

        Attachment(
            const gl::Framebuffer_attachment   attachment_point,
            const gsl::not_null<Renderbuffer*> renderbuffer
        )
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
        void attach(
            const gl::Framebuffer_attachment attachment_point,
            const gsl::not_null<Texture*>    texture,
            const unsigned int               level = 0,
            const unsigned int               layer = 0
        );

        void attach(
            const gl::Framebuffer_attachment   attachment_point,
            const gsl::not_null<Renderbuffer*> renderbuffer
        );

        std::vector<Attachment> attachments;
    };

    explicit Framebuffer(const Create_info& create_info);
    ~Framebuffer        () noexcept;
    Framebuffer         (const Framebuffer&) = delete;
    void operator=      (const Framebuffer&) = delete;
    Framebuffer         (Framebuffer&&)      = delete;
    void operator=      (Framebuffer&&)      = delete;

    static void on_thread_enter();
    static void on_thread_exit ();

    [[nodiscard]] auto gl_name() const -> unsigned int;

    void create         ();
    void reset          ();
    auto check_status   () const -> bool;
    void set_debug_label(const std::string& label);

private:
    std::optional<Gl_framebuffer> m_gl_framebuffer;
    std::vector<Attachment>       m_attachments;
    std::thread::id               m_owner_thread;
    std::string                   m_debug_label;

    static std::mutex                s_mutex;
    static std::vector<Framebuffer*> s_all_framebuffers;
};

} // namespace erhe::graphics
