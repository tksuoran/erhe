#pragma once

#include "windows/viewport_config.hpp"
#include "renderers/programs.hpp"
#include "scene/node_raytrace_mask.hpp"

#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
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
    class Multisample_resolve_node;
    class Rendergraph;
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
class Grid_tool;
class Id_renderer;
class Light_projections;
class Node_raytrace;
class Post_processing;
class Post_processing_node;
class Render_context;
class Scene_root;
class Shadow_render_node;
class Trs_tool;
class Viewport_window;
class Viewport_windows;

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

/// <summary>
/// Rendergraph producer node for rendering contents of scene into output rendergraph node.
/// </summary>
/// Typical output rendergraph nodes are:
///  - Multisample_resolve_node
///  - Post_processing_node
///  - default Rendergraph_node representing default framebuffer
/// Inputs:  "shadow_maps"
/// Outputs: "viewport"
class Viewport_window
    : public erhe::application::Rendergraph_node
{
public:
    static constexpr std::string_view c_type_name{"Viewport_window"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Viewport_window(
        const std::string_view              name,
        const erhe::components::Components& components,
        const std::shared_ptr<Scene_root>&  scene_root,
        erhe::scene::Camera*                camera
    );
    ~Viewport_window();

    // Implements Rendergraph_node
    [[nodiscard]] auto type_name() const -> std::string_view override { return c_type_name; }
    [[nodiscard]] auto type_hash() const -> uint32_t         override { return c_type_hash; }
    void execute_rendergraph_node() override;

    // Public API
    void connect            (Viewport_windows* viewport_windows);
    void set_window_viewport(int x, int y, int width, int height);
    void set_is_hovered     (bool is_hovered);
    void set_camera         (erhe::scene::Camera* camera);

    [[nodiscard]] auto to_scene_content   (const glm::vec2 position_in_root) const -> glm::vec2;
    [[nodiscard]] auto project_to_viewport(const glm::dvec3 position_in_world) const -> nonstd::optional<glm::dvec3>;
    [[nodiscard]] auto unproject_to_world (const glm::dvec3 position_in_window) const -> nonstd::optional<glm::dvec3>;
    [[nodiscard]] auto is_hovered         () const -> bool;
    [[nodiscard]] auto scene_root         () const -> Scene_root*;
    [[nodiscard]] auto window_viewport    () const -> const erhe::scene::Viewport&;
    [[nodiscard]] auto projection_viewport() const -> const erhe::scene::Viewport&;
    [[nodiscard]] auto camera             () const -> erhe::scene::Camera*;

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
    [[nodiscard]] auto get_hover                       (std::size_t slot) const -> const Hover_entry&;
    [[nodiscard]] auto get_nearest_hover               () const -> const Hover_entry&;

    auto get_shadow_render_node() const -> Shadow_render_node*;
    auto get_light_projections () const -> Light_projections*;
    auto get_shadow_texture    () const -> erhe::graphics::Texture*;

    void imgui_toolbar();

    // TODO Consider if these links are a good thing, or if they should
    //      be discovered from the graph instead.
    void link_to                 (std::weak_ptr<erhe::application::Multisample_resolve_node> node);
    void link_to                 (std::weak_ptr<Post_processing_node> node);
    auto get_post_processing_node() -> std::shared_ptr<Post_processing_node>;
    void set_final_output        (std::weak_ptr<Rendergraph_node> node);
    auto get_final_output        () -> std::weak_ptr<Rendergraph_node>;

    void clear() const;

private:
    [[nodiscard]] auto get_override_shader_stages() const -> erhe::graphics::Shader_stages*;

    static int s_serial;

    // Component dependencies
    std::shared_ptr<erhe::application::Configuration>     m_configuration;
    std::shared_ptr<erhe::application::Rendergraph>       m_render_graph;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Editor_rendering>                     m_editor_rendering;
    std::shared_ptr<Grid_tool>                            m_grid_tool;
    std::shared_ptr<Post_processing>                      m_post_processing;
    std::shared_ptr<Programs>                             m_programs;
    std::shared_ptr<Trs_tool>                             m_trs_tool;
    std::shared_ptr<Viewport_config>                      m_viewport_config;

    // TODO Consider if these links are a good thing, or if they should
    //      be discovered from the graph instead.
    std::weak_ptr<erhe::application::Multisample_resolve_node> m_multisample_resolve_node;
    std::weak_ptr<Post_processing_node>                        m_post_processing_node;
    std::weak_ptr<Rendergraph_node>                            m_final_output;

    std::string                 m_name;
    std::shared_ptr<Scene_root> m_scene_root;

    Viewport_windows*           m_viewport_windows      {nullptr};
    erhe::scene::Viewport       m_window_viewport       {0, 0, 0, 0, true};
    erhe::scene::Viewport       m_projection_viewport   {0, 0, 0, 0, true};
    erhe::scene::Camera*        m_camera                {nullptr};
    Shader_stages_variant       m_shader_stages_variant {Shader_stages_variant::standard};
    bool                        m_enable_post_processing{true};
    bool                        m_is_hovered            {false};

    // Pointer context data
    nonstd::optional<glm::vec2> m_position_in_viewport;
    nonstd::optional<glm::vec3> m_near_position_in_world;
    nonstd::optional<glm::vec3> m_far_position_in_world;

    std::array<Hover_entry, Hover_entry::slot_count> m_hover_entries;
    std::size_t                                      m_nearest_slot{0};
};

} // namespace editor
