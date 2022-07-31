#pragma once

#include "windows/viewport_config.hpp"
#include "renderers/programs.hpp"
#include "scene/node_raytrace_mask.hpp"

#include "erhe/application/render_graph_node.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/windows/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/viewport.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::application
{
    class Configuration;
    class Imgui_viewport;
    class Imgui_windows;
    class Render_graph;
    class View;
}

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::graphics
{
    class Framebuffer;
    class Texture;
    class Renderbuffer;
    class OpenGL_state_tracker;
}

namespace editor
{

class Editor_rendering;
class Id_renderer;
class Node_raytrace;
class Post_processing;
class Render_context;
class Scene_root;
#if defined(ERHE_XR_LIBRARY_OPENXR)
class Headset_renderer;
#endif
class Viewport_window;

class Hover_entry
{
public:
    static constexpr std::size_t content_slot      = 0;
    static constexpr std::size_t tool_slot         = 1;
    static constexpr std::size_t brush_slot        = 2;
    static constexpr std::size_t rendertarget_slot = 3;
    static constexpr std::size_t slot_count   = 4;

    static constexpr std::array<uint32_t, slot_count> slot_masks = {
        Raytrace_node_mask::content,
        Raytrace_node_mask::tool,
        Raytrace_node_mask::brush,
        Raytrace_node_mask::rendertarget
    };

    static constexpr std::array<const char*, slot_count> slot_names = {
        "content",
        "tool",
        "brush",
        "rendertarget"
    };

    uint32_t                           mask         {0};
    bool                               valid        {false};
    Node_raytrace*                     raytrace_node{nullptr};
    std::shared_ptr<erhe::scene::Mesh> mesh         {};
    erhe::geometry::Geometry*          geometry     {nullptr};
    nonstd::optional<glm::vec3>        position     {};
    nonstd::optional<glm::vec3>        normal       {};
    std::size_t                        primitive    {0};
    std::size_t                        local_index  {0};
};

class Viewport_window
    : public erhe::application::Imgui_window
    , public erhe::application::Mouse_input_sink
    , public erhe::application::Render_graph_node
{
public:
    static constexpr std::string_view c_label{"Viewport_window"};
    static constexpr uint32_t hash {
        compiletime_xxhash::xxh32(
            c_label.data(),
            c_label.size(),
            {}
        )
    };

    Viewport_window(
        const std::string_view              name,
        const erhe::components::Components& components,
        const std::shared_ptr<Scene_root>&  scene_root,
        erhe::scene::Camera*                camera
    );

    // Implements Imgui_window
    void imgui               () override;
    auto get_window_type_hash() const -> uint32_t override { return hash; }
    auto consumes_mouse_input() const -> bool override;
    void on_begin            () override;
    void on_end              () override;
    void set_viewport        (erhe::application::Imgui_viewport* imgui_viewport) override;

    // Implements Render_graph_node
    void execute_render_graph_node() override;

    // Public API
    void bind_framebuffer_main     ();
    void bind_framebuffer_overlay  ();
    void resolve                   ();
    void set_viewport              (const int x, const int y, const int width, const int height);
    void set_post_processing_enable(bool value);

    [[nodiscard]] auto to_scene_content    (const glm::vec2 position_in_root) const -> glm::vec2;
    [[nodiscard]] auto project_to_viewport (const glm::dvec3 position_in_world) const -> nonstd::optional<glm::dvec3>;
    [[nodiscard]] auto unproject_to_world  (const glm::dvec3 position_in_window) const -> nonstd::optional<glm::dvec3>;
    [[nodiscard]] auto is_framebuffer_ready() const -> bool;
    [[nodiscard]] auto content_region_size () const -> glm::ivec2;
    [[nodiscard]] auto is_hovered          () const -> bool;
    [[nodiscard]] auto scene_root          () const -> Scene_root*;
    [[nodiscard]] auto viewport            () const -> const erhe::scene::Viewport&;
    [[nodiscard]] auto camera              () const -> erhe::scene::Camera*;

    // Pointer context API
#if !defined(ERHE_RAYTRACE_LIBRARY_NONE)
    void raytrace(uint32_t mask);
    void raytrace();
#endif

    // call with const glm::vec2 position_in_window = m_window.to_scene_content(position);
    void reset_pointer_context();
    void update_pointer_context(
        Id_renderer& id_renderer,
        glm::vec2    position_in_viewport
    );

    [[nodiscard]] auto position_in_viewport            () const -> nonstd::optional<glm::vec2>;
    [[nodiscard]] auto position_in_world_viewport_depth(double viewport_depth) const -> nonstd::optional<glm::dvec3>;
    [[nodiscard]] auto near_position_in_world          () const -> nonstd::optional<glm::vec3>;
    [[nodiscard]] auto far_position_in_world           () const -> nonstd::optional<glm::vec3>;
    [[nodiscard]] auto position_in_world_distance      (float distance) const -> nonstd::optional<glm::vec3>;
    [[nodiscard]] auto pointer_in_content_area         () const -> bool;
    [[nodiscard]] auto get_hover                       (std::size_t slot) const -> const Hover_entry&;
    [[nodiscard]] auto get_nearest_hover               () const -> const Hover_entry&;

private:
    [[nodiscard]] auto should_render             () const -> bool;
    [[nodiscard]] auto should_post_process       () const -> bool;
    [[nodiscard]] auto get_override_shader_stages() const -> erhe::graphics::Shader_stages*;

    void update_framebuffer();
    void clear             () const;

    static int s_serial;

    // Component dependencies
    std::shared_ptr<erhe::application::Configuration>     m_configuration;
    std::shared_ptr<erhe::application::Render_graph>      m_render_graph;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Editor_rendering>                     m_editor_rendering;
    std::shared_ptr<Post_processing>                      m_post_processing;
    std::shared_ptr<Programs>                             m_programs;
    std::shared_ptr<Viewport_config>                      m_viewport_config;

    std::string                                   m_name;
    std::shared_ptr<Scene_root>                   m_scene_root;
    erhe::scene::Viewport                         m_viewport              {0, 0, 0, 0, true};
    erhe::scene::Camera*                          m_camera                {nullptr};
    Shader_stages_variant                         m_shader_stages_variant {Shader_stages_variant::standard};
    bool                                          m_enable_post_processing{true};
    bool                                          m_is_hovered            {false};
    bool                                          m_can_present           {false};
    glm::ivec2                                    m_content_region_min    {0.0f, 0.0f};
    glm::ivec2                                    m_content_region_max    {0.0f, 0.0f};
    glm::ivec2                                    m_content_region_size   {0.0f, 0.0f};
    std::unique_ptr<erhe::graphics::Texture>      m_color_texture_multisample;
    std::shared_ptr<erhe::graphics::Texture>      m_color_texture_resolved;
    std::shared_ptr<erhe::graphics::Texture>      m_color_texture_resolved_for_present;
    std::unique_ptr<erhe::graphics::Renderbuffer> m_depth_stencil_multisample_renderbuffer;
    std::unique_ptr<erhe::graphics::Renderbuffer> m_depth_stencil_resolved_renderbuffer;
    std::unique_ptr<erhe::graphics::Framebuffer>  m_framebuffer_multisample;
    std::unique_ptr<erhe::graphics::Framebuffer>  m_framebuffer_resolved;

    // Pointer context data
    nonstd::optional<glm::vec2> m_position_in_viewport;
    nonstd::optional<glm::vec3> m_near_position_in_world;
    nonstd::optional<glm::vec3> m_far_position_in_world;

    std::array<Hover_entry, Hover_entry::slot_count> m_hover_entries;
    std::size_t                                      m_nearest_slot{0};
};

inline auto is_viewport_window(erhe::application::Imgui_window* window) -> bool
{
    return
        (window != nullptr) &&
        (window->get_window_type_hash() == Viewport_window::hash);
}

inline auto as_viewport_window(erhe::application::Imgui_window* window) -> Viewport_window*
{
    return
        (window != nullptr) &&
        (window->get_window_type_hash() == Viewport_window::hash)
            ? reinterpret_cast<Viewport_window*>(window)
            : nullptr;
}

} // namespace editor
