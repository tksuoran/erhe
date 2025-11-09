#pragma once

#include "erhe_graphics/render_pass.hpp"
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
    std::array<Render_pass_attachment_descriptor, 4> m_color_attachments;
    Render_pass_attachment_descriptor                m_depth_attachment;
    Render_pass_attachment_descriptor                m_stencil_attachment;
    int                                              m_render_target_width{0};
    int                                              m_render_target_height{0};
    std::string                                      m_debug_label;
    std::string                                      m_debug_group_name;
    bool                                             m_uses_multisample_resolve{false};
    bool                                             m_is_active{false};
};

} // namespace erhe::graphics
