#pragma once

#include "erhe_graphics/sampler.hpp"

#include <memory>
#include <vector>

namespace erhe::graphics {
    class Device;
    class Render_command_encoder;
    class Render_pass;
    class Renderbuffer;
    class Texture;
}

namespace editor {

class Editor_context;
class Icon_settings;
class Programs;

class Icon_settings;

class Thumbnails
{
public:
    Thumbnails(erhe::graphics::Device& graphics_device, unsigned int capacity, unsigned int size_pixels);

    [[nodiscard]] auto allocate     () -> uint32_t;
    [[nodiscard]] auto begin_capture(uint32_t slot) -> std::unique_ptr<erhe::graphics::Render_command_encoder>;

    void free             (uint32_t slot);
    void draw             (uint32_t slot) const;
    auto get_color_texture() -> std::shared_ptr<erhe::graphics::Texture> { return m_color_texture; }

private:
    erhe::graphics::Device&                                   m_graphics_device;
    std::shared_ptr<erhe::graphics::Texture>                  m_color_texture;
    std::shared_ptr<erhe::graphics::Renderbuffer>             m_depth_renderbuffer;
    std::unique_ptr<erhe::graphics::Render_pass>              m_render_pass;
    erhe::graphics::Sampler                                   m_color_sampler;
    std::vector<bool>                                         m_in_use;
    int                                                       m_capacity   {0};
    int                                                       m_size_pixels{0};
    std::vector<std::shared_ptr<erhe::graphics::Texture>>     m_texture_views;
    std::vector<std::unique_ptr<erhe::graphics::Render_pass>> m_render_passes;
    std::vector<uint64_t>                                     m_color_texture_handles;
};

} // namespace editor
