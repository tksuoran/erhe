#pragma once

#include "erhe_graphics/render_pass.hpp"
#include "erhe_utility/debug_label.hpp"

#include <mutex>
#include <vector>

namespace erhe::graphics {

class Render_pipeline_state;

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

    [[nodiscard]] auto get_render_target_width () const -> int;
    [[nodiscard]] auto get_render_target_height() const -> int;
    [[nodiscard]] auto get_swapchain           () const -> Swapchain*;
    [[nodiscard]] auto get_debug_label         () const -> erhe::utility::Debug_label;

private:
    friend class Render_pass;
    void start_render_pass();
    void end_render_pass  ();

private:
    Device&                    m_device;
    Swapchain*                 m_swapchain{nullptr};
    int                        m_render_target_width{0};
    int                        m_render_target_height{0};
    erhe::utility::Debug_label m_debug_label;

    static std::mutex                     s_mutex;
    static std::vector<Render_pass_impl*> s_all_framebuffers;
    static Render_pass_impl*              s_active_render_pass;
};

} // namespace erhe::graphics
