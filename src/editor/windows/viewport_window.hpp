#pragma once

#include "windows/imgui_window.hpp"
#include "windows/viewport_config.hpp"

#include "erhe/scene/viewport.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::graphics
{
    class Framebuffer;
    class Texture;
    class Renderbuffer;
    class OpenGL_state_tracker;
}

namespace erhe::scene
{
    class ICamera;
}

namespace editor
{

class Configuration;
class Editor_rendering;
class Pointer_context;
class Render_context;

class Viewport_window
    : public Imgui_window
{
public:
    Viewport_window(
        const std::string_view name,
        erhe::scene::ICamera*  camera
    );
    ~Viewport_window();

    // Implements Imgui_window
    void imgui() override;

    auto to_scene_content            (const glm::vec2 position_in_root) -> glm::vec2;
    auto project_to_viewport         (const glm::vec3 position_in_world) const -> glm::vec3;
    auto unproject_to_world          (const glm::vec3 position_in_window) const -> glm::vec3;
    void bind_multisample_framebuffer();
    auto is_framebuffer_ready        () const -> bool;
    void multisample_resolve         ();
    auto content_region_size         () const -> glm::ivec2;
    auto is_focused                  () const -> bool;
    auto viewport                    () const -> erhe::scene::Viewport;
    auto camera                      () const -> erhe::scene::ICamera*;
    auto hit_test                    (int x, int y) const -> bool;

    void render(
        const Configuration*                  configuration,
        Editor_rendering*                     editor_rendering,
        erhe::graphics::OpenGL_state_tracker* pipeline_state_tracker
    );

private:
    void update_framebuffer();
    auto should_render     () const -> bool;
    void clear(
        const Configuration*                  configuration,
        erhe::graphics::OpenGL_state_tracker* pipeline_state_tracker
    ) const;

    static constexpr int                          s_sample_count{16};

    std::string                                   m_name;
    erhe::scene::Viewport                         m_viewport           {0, 0, 0, 0, true};
    erhe::scene::ICamera*                         m_camera             {nullptr};
    bool                                          m_is_focused         {false};
    bool                                          m_can_present        {false};
    glm::ivec2                                    m_content_region_min {0.0f, 0.0f};
    glm::ivec2                                    m_content_region_max {0.0f, 0.0f};
    glm::ivec2                                    m_content_region_size{0.0f, 0.0f};
    std::unique_ptr<erhe::graphics::Texture>      m_color_texture_multisample;
    std::shared_ptr<erhe::graphics::Texture>      m_color_texture_resolved;
    std::shared_ptr<erhe::graphics::Texture>      m_color_texture_resolved_for_present;
    std::unique_ptr<erhe::graphics::Renderbuffer> m_depth_stencil_renderbuffer;
    std::unique_ptr<erhe::graphics::Framebuffer>  m_framebuffer_multisample;
    std::unique_ptr<erhe::graphics::Framebuffer>  m_framebuffer_resolved;
    Viewport_config                               m_viewport_config;
};

class Viewport_windows
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name {"Viewport_windows"};
    static constexpr std::string_view c_title{"View"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Viewport_windows ();
    ~Viewport_windows() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    auto create_window(
        const std::string_view name,
        erhe::scene::ICamera*  camera
    ) -> Viewport_window*;

    //auto to_scene_content   (const glm::vec2 position_in_root) const -> glm::vec2;
    //auto is_content_in_focus() const -> bool;
    void update();
    void render();

private:
    Configuration*                                m_configuration         {nullptr};
    Editor_rendering*                             m_editor_rendering      {nullptr};
    erhe::graphics::OpenGL_state_tracker*         m_pipeline_state_tracker{nullptr};
    Pointer_context*                              m_pointer_context       {nullptr};
    std::vector<std::shared_ptr<Viewport_window>> m_windows;
};

} // namespace editor
