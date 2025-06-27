#pragma once

#include "erhe_graphics/gl_objects.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_profile/profile.hpp"

#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace erhe::graphics {

class Device;
class Render_command_encoder;
class Renderbuffer;
class Texture;

enum class Load_action : unsigned int {
    Dont_care = 0,
    Clear,
    Load
};

enum class Store_action : unsigned int {
    Dont_care = 0,
    Store,
    Multisample_resolve,
    Store_and_multisample_resolve
};

class Render_pass_attachment_descriptor final
{
public:
    Render_pass_attachment_descriptor();
    ~Render_pass_attachment_descriptor();

    [[nodiscard]] auto is_defined     () const -> bool;
    [[nodiscard]] auto get_pixelformat() const -> erhe::dataformat::Format;

    unsigned int          texture_level  {0};
    unsigned int          texture_layer  {0};
    Texture*              texture        {nullptr};
    Renderbuffer*         renderbuffer   {nullptr};
    std::array<double, 4> clear_value;
    Load_action           load_action    {Load_action::Clear};
    Store_action          store_action   {Store_action::Store};
    Texture*              resolve_texture{nullptr};
    unsigned int          resolve_level  {0};
    unsigned int          resolve_layer  {0};
    bool                  use_default_framebuffer{false};
};

class Render_pass_descriptor
{
public:
    Render_pass_descriptor();
    ~Render_pass_descriptor();

    std::array<Render_pass_attachment_descriptor, 4> color_attachments   {};
    Render_pass_attachment_descriptor                depth_attachment    {};
    Render_pass_attachment_descriptor                stencil_attachment  {};
    int                                              render_target_width {0};
    int                                              render_target_height{0};
    std::string                                      debug_label;
};


class Render_pass final
{
public:
    Render_pass   (Device& device, const Render_pass_descriptor& render_pass_descriptor);
    ~Render_pass  () noexcept;
    Render_pass   (const Render_pass&) = delete;
    void operator=(const Render_pass&) = delete;
    Render_pass   (Render_pass&&)      = delete;
    void operator=(Render_pass&&)      = delete;

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
    static std::vector<Render_pass*>                  s_all_framebuffers;
    static Render_pass*                               s_active_render_pass;
};

} // namespace erhe::graphics
