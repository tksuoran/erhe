#pragma once

#include "windows/viewport_config.hpp"
#include "renderers/programs.hpp"
#include "scene/node_raytrace_mask.hpp"

#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/viewport.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>

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
}

namespace erhe::scene
{
    class Camera;
}

namespace editor
{

class Light_projections;
class Node_raytrace;
class Render_context;
class Scene_root;
class Shadow_render_node;

class Hover_entry
{
public:
    static constexpr std::size_t content_slot      = 0;
    static constexpr std::size_t tool_slot         = 1;
    static constexpr std::size_t brush_slot        = 2;
    static constexpr std::size_t rendertarget_slot = 3;
    static constexpr std::size_t slot_count        = 4;

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

    void reset();

    uint32_t                           mask         {0};
    bool                               valid        {false};
    Node_raytrace*                     raytrace_node{nullptr};
    std::shared_ptr<erhe::scene::Mesh> mesh         {};
    erhe::geometry::Geometry*          geometry     {nullptr};
    std::optional<glm::dvec3>          position     {};
    std::optional<glm::dvec3>          normal       {};
    std::size_t                        primitive    {std::numeric_limits<std::size_t>::max()};
    std::size_t                        local_index  {std::numeric_limits<std::size_t>::max()};
};

constexpr int Input_context_type_scene_view      = 0;
constexpr int Input_context_type_viewport_window = 1;
constexpr int Input_context_type_headset_view    = 2;

class Scene_view
    : public erhe::application::Input_context
{
public:
    // Implements erhe::application::Input_context
    [[nodiscard]] auto get_type() const -> int override;

    // Virtual interface
    [[nodiscard]] virtual auto get_scene_root        () const -> std::shared_ptr<Scene_root> = 0;
    [[nodiscard]] virtual auto get_camera            () const -> std::shared_ptr<erhe::scene::Camera> = 0;
    [[nodiscard]] virtual auto get_shadow_render_node() const -> Shadow_render_node* = 0;

    // "Pointing"
    void reset_control_ray();
    void raytrace_update(
        const glm::vec3 ray_origim,
        const glm::vec3 ray_direction,
        Scene_root*     tool_scene_root = nullptr
    );
    [[nodiscard]] auto get_position_in_viewport                 () const -> std::optional<glm::dvec2>;
    [[nodiscard]] auto get_control_ray_origin_in_world          () const -> std::optional<glm::dvec3>;
    [[nodiscard]] auto get_control_ray_direction_in_world       () const -> std::optional<glm::dvec3>;
    [[nodiscard]] auto get_control_position_in_world_at_distance(double distance) const -> std::optional<glm::dvec3>;
    [[nodiscard]] auto get_hover                                (std::size_t slot) const -> const Hover_entry&;
    [[nodiscard]] auto get_nearest_hover                        () const -> const Hover_entry&;
    [[nodiscard]] auto get_light_projections                    () const -> Light_projections*;
    [[nodiscard]] auto get_shadow_texture                       () const -> erhe::graphics::Texture*;

protected:
    std::optional<glm::dvec2>                        m_position_in_viewport;
    std::optional<glm::dvec3>                        m_control_ray_origin_in_world;
    std::optional<glm::dvec3>                        m_control_ray_direction_in_world;
    std::array<Hover_entry, Hover_entry::slot_count> m_hover_entries;
    std::size_t                                      m_nearest_slot{0};
};

} // namespace editor
