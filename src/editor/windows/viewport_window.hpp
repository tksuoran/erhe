#pragma once

#include "erhe/scene/viewport.hpp"

#include "windows/imgui_window.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::graphics
{
    class Texture;
    class Renderbuffer;
    class Framebuffer;
}

namespace editor
{

class Scene_manager;

class Viewport_window
    : public erhe::components::Component
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name{"Viewport_window"};

    Viewport_window ();
    ~Viewport_window() override;

    // Implements Component
    void initialize_component() override;

    // Implements Imgui_window
    void imgui(Pointer_context& pointer_context) override;

    auto to_scene_content            (glm::vec2 position_in_root) -> glm::vec2;
    void bind_multisample_framebuffer();
    auto is_framebuffer_ready        () -> bool;
    void multisample_resolve         ();
    auto content_region_size         () -> glm::ivec2;
    auto is_focused                  () -> bool;

private:
    void update_framebuffer();

    static constexpr int                          s_sample_count{16};

    glm::ivec2                                    m_content_region_min{0.0f, 0.0f};
    glm::ivec2                                    m_content_region_max{0.0f, 0.0f};
    glm::ivec2                                    m_content_region_size{0.0f, 0.0f};

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
