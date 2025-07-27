#pragma once

#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/gl/gl_objects.hpp"
#include "erhe_profile/profile.hpp"

#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace erhe::graphics {
    
class Render_pass_impl final
{
public:
    Render_pass_impl (Device& device, const Render_pass_descriptor& render_pass_descriptor);
    ~Render_pass_impl() noexcept;
    Render_pass_impl (const Render_pass_impl&) = delete;
    void operator=   (const Render_pass_impl&) = delete;
    Render_pass_impl (Render_pass_impl&&)      = delete;
    void operator=   (Render_pass_impl&&)      = delete;

    static void on_thread_enter();
    static void on_thread_exit ();

    [[nodiscard]] auto gl_name                    () const -> unsigned int;
    [[nodiscard]] auto gl_multisample_resolve_name() const -> unsigned int;
    [[nodiscard]] auto get_sample_count           () const -> unsigned int;

    void create      ();
    void reset       ();
    auto check_status() const -> bool;

    [[nodiscard]] auto get_render_target_width() const -> int;
    [[nodiscard]] auto get_render_target_height() const -> int;
    [[nodiscard]] auto get_debug_label() const -> const std::string&;

private:
    friend class Render_command_encoder;
    void start_render_pass();
    void end_render_pass  ();

private:
    Device&                                          m_device;
    std::optional<Gl_framebuffer>                    m_gl_framebuffer;
    std::optional<Gl_framebuffer>                    m_gl_multisample_resolve_framebuffer;
    std::array<Render_pass_attachment_descriptor, 4> m_color_attachments;
    std::vector<gl::Color_buffer>                    m_draw_buffers;
    Render_pass_attachment_descriptor                m_depth_attachment;
    Render_pass_attachment_descriptor                m_stencil_attachment;
    int                                              m_render_target_width{0};
    int                                              m_render_target_height{0};
    std::string                                      m_debug_label;
    std::string                                      m_debug_group_name;
    bool                                             m_uses_multisample_resolve{false};
    bool                                             m_uses_default_framebuffer{false};
    std::thread::id                                  m_owner_thread;
    bool                                             m_is_active{false};

    static ERHE_PROFILE_MUTEX_DECLARATION(std::mutex, s_mutex);
    static std::vector<Render_pass_impl*>             s_all_framebuffers;
    static Render_pass_impl*                          s_active_render_pass;
};

} // namespace erhe::graphics
