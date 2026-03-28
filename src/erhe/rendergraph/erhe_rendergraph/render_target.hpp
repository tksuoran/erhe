#pragma once

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_utility/debug_label.hpp"

#include <memory>

namespace erhe::graphics {
    class Device;
    class Render_pass;
    class Swapchain;
    class Texture;
}

namespace erhe::rendergraph {

class Render_target_create_info
{
public:
    erhe::graphics::Device&    graphics_device;
    erhe::utility::Debug_label debug_label;
    erhe::dataformat::Format   color_format        {erhe::dataformat::Format::format_undefined};
    erhe::dataformat::Format   depth_stencil_format {erhe::dataformat::Format::format_undefined};
    int                        sample_count         {0};
};

// Manages color texture (possibly multisampled), depth/stencil texture,
// and render pass lifecycle. Extracted from Texture_rendergraph_node so
// render target management is composable rather than requiring inheritance.
class Render_target
{
public:
    explicit Render_target(const Render_target_create_info& create_info);
    ~Render_target() noexcept;

    void update          (int width, int height, erhe::graphics::Swapchain* swapchain);
    void reconfigure     (int sample_count);
    void set_reverse_depth(bool reverse_depth);

    [[nodiscard]] auto get_color_texture        () const -> const std::shared_ptr<erhe::graphics::Texture>&;
    [[nodiscard]] auto get_depth_stencil_texture() const -> erhe::graphics::Texture*;
    [[nodiscard]] auto get_render_pass          () const -> erhe::graphics::Render_pass*;

private:
    erhe::graphics::Device&                      m_graphics_device;
    erhe::utility::Debug_label                   m_debug_label;
    erhe::dataformat::Format                     m_color_format;
    erhe::dataformat::Format                     m_depth_stencil_format;
    int                                          m_sample_count;
    bool                                         m_reverse_depth{true};
    std::shared_ptr<erhe::graphics::Texture>     m_color_texture;
    std::shared_ptr<erhe::graphics::Texture>     m_multisampled_color_texture;
    std::unique_ptr<erhe::graphics::Texture>     m_depth_stencil_texture;
    std::unique_ptr<erhe::graphics::Render_pass> m_render_pass;
};

} // namespace erhe::rendergraph
