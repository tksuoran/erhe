#pragma once

#include "erhe_graphics/sampler.hpp"

#include <memory>
#include <vector>

namespace erhe::graphics {
    class Framebuffer;
    class Device;
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

    [[nodiscard]] auto allocate() -> uint32_t;
    void free             (uint32_t slot);
    void begin_capture    (uint32_t slot) const;
    void draw             (uint32_t slot) const;
    auto get_color_texture() -> std::shared_ptr<erhe::graphics::Texture> { return m_color_texture; }

private:
    std::shared_ptr<erhe::graphics::Texture>      m_color_texture;
    std::shared_ptr<erhe::graphics::Renderbuffer> m_depth_renderbuffer;
    std::shared_ptr<erhe::graphics::Framebuffer>  m_framebuffer;
    erhe::graphics::Sampler                       m_color_sampler;
    uint64_t                                      m_color_texture_handle{0};
    std::vector<bool>                             m_in_use;
    unsigned int                                  m_capacity_root {0};
    unsigned int                                  m_capacity      {0};
    unsigned int                                  m_size_pixels   {0};
};

} // namespace editor
