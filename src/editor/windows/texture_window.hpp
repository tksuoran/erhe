#pragma once

#include "windows/window.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::graphics
{
    class Texture;
}

namespace editor
{

class Scene_manager;

class Texture_window
    : public Window
{
public:
    virtual ~Texture_window() = default;

    // Window
    void window() override;

    glm::vec2 to_scene_content(glm::vec2 position_in_root);

    void bind_multisample_framebuffer();


    void multisample_resolve();

    auto to_scene_content(glm::vec2 position_in_root) const -> glm::vec2;

    auto content_region_size() -> glm::ivec2
    {
        return m_content_region_size;
    }

    auto is_focused() -> bool;

private:
    void update_framebuffer();

    static constexpr int                          s_sample_count{16};

    glm::ivec2                                    m_content_region_min{0.0f, 0.0f};
    glm::ivec2                                    m_content_region_max{0.0f, 0.0f};
    glm::ivec2                                    m_content_region_size{0.0f, 0.0f};
    glm::vec2                                     m_scene_window_position{0.0f, 0.0f};
    glm::vec2                                     m_scene_window_size{0.0f, 0.0f};

    std::unique_ptr<erhe::graphics::Texture>      m_color_texture_multisample;
    std::shared_ptr<erhe::graphics::Texture>      m_color_texture_resolved;
    std::shared_ptr<erhe::graphics::Texture>      m_color_texture_resolved_for_present;
    std::unique_ptr<erhe::graphics::Renderbuffer> m_depth_stencil_renderbuffer;
    std::unique_ptr<erhe::graphics::Framebuffer>  m_framebuffer_multisample;
    std::unique_ptr<erhe::graphics::Framebuffer>  m_framebuffer_resolved;

    bool                                          m_is_focused {false};
    bool                                          m_can_present{false};
};

} // namespace editor
