#pragma once

#include "windows/imgui_window.hpp"
#include "windows/viewport_config.hpp"

#include "erhe/components/component.hpp"
#include "erhe/scene/camera.hpp"
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

namespace editor
{

class Configuration;
class Editor_rendering;
class Editor_view;
class Pointer_context;
class Render_context;
class Scene_root;
#if defined(ERHE_XR_LIBRARY_OPENXR)
class Headset_renderer;
#endif

class Viewport_window
    : public Imgui_window
{
public:
    Viewport_window(
        const std::string_view                  name,
        const std::shared_ptr<Configuration>&   configuration,
        const std::shared_ptr<Scene_root>&      scene_root,
        const std::shared_ptr<Viewport_config>& viewport_config,
        erhe::scene::ICamera*                   camera
    );

    // Implements Imgui_window
    void imgui   () override;
    void on_begin() override;
    void on_end  () override;

    // Public API
    void try_hover                   (const int px, const int py);
    void update                      ();
    void bind_multisample_framebuffer();
    void multisample_resolve         ();
    void set_viewport                (const int x, const int y, const int width, const int height);
    void render(
        Editor_rendering&                     editor_rendering,
        erhe::graphics::OpenGL_state_tracker& pipeline_state_tracker
    );

    [[nodiscard]] auto to_scene_content    (const glm::vec2 position_in_root) const -> glm::vec2;
    [[nodiscard]] auto project_to_viewport (const glm::dvec3 position_in_world) const -> glm::dvec3;
    [[nodiscard]] auto unproject_to_world  (const glm::dvec3 position_in_window) const -> std::optional<glm::dvec3>;
    [[nodiscard]] auto is_framebuffer_ready() const -> bool;
    [[nodiscard]] auto content_region_size () const -> glm::ivec2;
    [[nodiscard]] auto is_hovered          () const -> bool;
    [[nodiscard]] auto viewport            () const -> const erhe::scene::Viewport&;
    [[nodiscard]] auto camera              () const -> erhe::scene::ICamera*;

private:
    [[nodiscard]] auto should_render() const -> bool;

    void update_framebuffer();

    void clear(erhe::graphics::OpenGL_state_tracker& pipeline_state_tracker) const;

    static constexpr int                          s_sample_count{16};

    std::string                                   m_name;

    // Component dependencies
    std::shared_ptr<Configuration>                m_configuration;
    std::shared_ptr<Scene_root>                   m_scene_root;
    std::shared_ptr<Viewport_config>              m_viewport_config;

    erhe::scene::Viewport                         m_viewport           {0, 0, 0, 0, true};
    erhe::scene::ICamera*                         m_camera             {nullptr};
    erhe::scene::Projection_transforms            m_projection_transforms;
    bool                                          m_is_hovered         {false};
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
};

class Viewport_windows
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name {"Viewport_windows"};
    static constexpr std::string_view c_title{"View"};
    static constexpr uint32_t hash{
        compiletime_xxhash::xxh32(
            c_name.data(),
            c_name.size(),
            {}
        )
    };

    Viewport_windows ();
    ~Viewport_windows() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Public API
    auto create_window(
        const std::string_view name,
        erhe::scene::ICamera*  camera
    ) -> Viewport_window*;

    void update();
    void render();

private:
    // Component dependencies
    std::shared_ptr<Configuration>                        m_configuration;
    std::shared_ptr<Editor_rendering>                     m_editor_rendering;
    std::shared_ptr<Editor_view>                          m_editor_view;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Pointer_context>                      m_pointer_context;
    std::shared_ptr<Scene_root>                           m_scene_root;
    std::shared_ptr<Viewport_config>                      m_viewport_config;

#if defined(ERHE_XR_LIBRARY_OPENXR)
    std::shared_ptr<Headset_renderer>                     m_headset_renderer;
#endif

    std::vector<std::shared_ptr<Viewport_window>> m_windows;
    Viewport_window*                              m_hover_window{nullptr};
};

} // namespace editor
