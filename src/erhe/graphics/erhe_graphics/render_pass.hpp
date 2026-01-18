#pragma once

#include "erhe_dataformat/dataformat.hpp"

#include <array>
#include <memory>
#include <string>

namespace erhe::graphics {

class Device;
class Render_command_encoder;
class Renderbuffer;
class Texture;
class Swapchain;

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
    ~Render_pass_attachment_descriptor() noexcept;

    [[nodiscard]] auto is_defined     () const -> bool;
    [[nodiscard]] auto get_pixelformat() const -> erhe::dataformat::Format;

    unsigned int          texture_level  {0};
    unsigned int          texture_layer  {0};
    const Texture*        texture        {nullptr};
    std::array<double, 4> clear_value;
    Load_action           load_action    {Load_action::Clear};
    Store_action          store_action   {Store_action::Store};
    Texture*              resolve_texture{nullptr};
    unsigned int          resolve_level  {0};
    unsigned int          resolve_layer  {0};
};

class Render_pass_descriptor
{
public:
    Render_pass_descriptor();
    ~Render_pass_descriptor() noexcept;

    Swapchain*                                       swapchain           {nullptr};
    std::array<Render_pass_attachment_descriptor, 4> color_attachments   {};
    Render_pass_attachment_descriptor                depth_attachment    {};
    Render_pass_attachment_descriptor                stencil_attachment  {};
    int                                              render_target_width {0};
    int                                              render_target_height{0};
    std::string                                      debug_label;
};

class Render_pass_impl;
class Render_pass final
{
public:
    Render_pass   (Device& device, const Render_pass_descriptor& render_pass_descriptor);
    ~Render_pass  () noexcept;
    Render_pass   (const Render_pass&) = delete;
    void operator=(const Render_pass&) = delete;
    Render_pass   (Render_pass&&)      = delete;
    void operator=(Render_pass&&)      = delete;

    [[nodiscard]] auto get_sample_count        () const -> unsigned int;
    [[nodiscard]] auto get_render_target_width () const -> int;
    [[nodiscard]] auto get_render_target_height() const -> int;
    [[nodiscard]] auto get_swapchain           () const -> Swapchain*;
    [[nodiscard]] auto get_debug_label         () const -> const std::string&;
    [[nodiscard]] auto get_impl                () -> Render_pass_impl&;
    [[nodiscard]] auto get_impl                () const -> const Render_pass_impl&;

private:
    std::unique_ptr<Render_pass_impl> m_impl;
};

} // namespace erhe::graphics
