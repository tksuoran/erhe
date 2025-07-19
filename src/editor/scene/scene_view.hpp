#pragma once

#include "renderers/viewport_config.hpp"
#include "scene/node_raytrace_mask.hpp"

#include <geogram/mesh/mesh.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>

namespace erhe::rendergraph { class Rendergraph_node; }
namespace erhe::geometry    { class Geometry; }
namespace erhe::graphics    { class Texture; }
namespace erhe::scene {
    class Camera;
    class Mesh;
}
namespace erhe::scene_renderer { class Light_projections; }

namespace editor {

class App_context;
class App_message;
class App_message_bus;
class Grid;
class Raytrace_primitive;
class Scene_root;
class Shadow_render_node;
class Viewport_scene_view;

class Hover_entry
{
public:
    static constexpr uint32_t content_slot      = 0;
    static constexpr uint32_t tool_slot         = 1;
    static constexpr uint32_t brush_slot        = 2;
    static constexpr uint32_t rendertarget_slot = 3;
    static constexpr uint32_t grid_slot         = 4;
    static constexpr uint32_t slot_count        = 5;
    static constexpr uint32_t content_bit       = (1 << 0u);
    static constexpr uint32_t tool_bit          = (1 << 1u);
    static constexpr uint32_t brush_bit         = (1 << 2u);
    static constexpr uint32_t rendertarget_bit  = (1 << 3u);
    static constexpr uint32_t grid_bit          = (1 << 4u);
    static constexpr uint32_t all_bits          = 0xffffffffu;

    static constexpr std::array<uint32_t, slot_count> raytrace_slot_masks = {
        Raytrace_node_mask::content,
        Raytrace_node_mask::tool,
        Raytrace_node_mask::brush,
        Raytrace_node_mask::rendertarget,
        Raytrace_node_mask::grid
    };

    static constexpr std::array<const char*, slot_count> slot_names = {
        "content",
        "tool",
        "brush",
        "rendertarget",
        "grid"
    };

    [[nodiscard]] auto get_name() const -> const std::string&;

    void reset();

    std::size_t                               slot                      {slot_count};
    uint32_t                                  mask                      {0};
    bool                                      valid                     {false};

    // For now, these are weak pointers to avoid dangling pointers
    std::weak_ptr<erhe::scene::Mesh>          scene_mesh_weak           {};
    std::weak_ptr<Grid>                       grid_weak                 {};

    std::size_t                               scene_mesh_primitive_index{std::numeric_limits<std::size_t>::max()};
    std::shared_ptr<erhe::geometry::Geometry> geometry                  {};
    std::optional<glm::vec3>                  position                  {};
    std::optional<glm::vec3>                  normal                    {};
    std::optional<glm::vec2>                  uv                        {};
    uint32_t                                  triangle                  {std::numeric_limits<uint32_t>::max()};
    GEO::index_t                              facet                     {GEO::NO_INDEX};
};

class Scene_view
{
public:
    Scene_view(App_context& context, Viewport_config viewport_config);
    virtual ~Scene_view() noexcept;

    // Virtual interface
    [[nodiscard]] virtual auto get_scene_root        () const -> std::shared_ptr<Scene_root> = 0;
    [[nodiscard]] virtual auto get_camera            () const -> std::shared_ptr<erhe::scene::Camera> = 0;
    [[nodiscard]] virtual auto get_perspective_scale () const -> float = 0;
    [[nodiscard]] virtual auto get_shadow_render_node() const -> Shadow_render_node* { return nullptr; }
    [[nodiscard]] virtual auto get_shadow_texture    () const -> erhe::graphics::Texture*;
    [[nodiscard]] virtual auto get_rendergraph_node  () -> erhe::rendergraph::Rendergraph_node* = 0;
    [[nodiscard]] virtual auto get_light_projections () const -> const erhe::scene_renderer::Light_projections*;
    [[nodiscard]] virtual auto as_viewport_scene_view() -> Viewport_scene_view*;
    [[nodiscard]] virtual auto as_viewport_scene_view() const -> const Viewport_scene_view*;

    // "Pointing"
    void set_world_from_control(glm::vec3 near_position_in_world, glm::vec3 far_position_in_world);

    [[nodiscard]] virtual auto get_closest_point_on_line (const glm::vec3 P0, const glm::vec3 P1) -> std::optional<glm::vec3>;
    [[nodiscard]] virtual auto get_closest_point_on_plane(const glm::vec3 N,  const glm::vec3 P ) -> std::optional<glm::vec3>;

    void set_world_from_control    (const glm::mat4& world_from_control);
    void reset_control_transform   ();
    void reset_hover_slots         ();
    void update_hover_with_raytrace();
    void update_grid_hover         ();

    [[nodiscard]] auto get_config                               () -> Viewport_config&;
    [[nodiscard]] auto get_world_from_control                   () const -> std::optional<glm::mat4>;
    [[nodiscard]] auto get_control_from_world                   () const -> std::optional<glm::mat4>;
    [[nodiscard]] auto get_control_ray_origin_in_world          () const -> std::optional<glm::vec3>;
    [[nodiscard]] auto get_control_ray_direction_in_world       () const -> std::optional<glm::vec3>;
    [[nodiscard]] auto get_control_position_in_world_at_distance(float distance) const -> std::optional<glm::vec3>;
    [[nodiscard]] auto get_hover                                (std::size_t slot) const -> const Hover_entry&;
    [[nodiscard]] auto get_nearest_hover                        (uint32_t slot_mask) const -> const Hover_entry*;

protected:
    void set_hover(std::size_t slot, const Hover_entry& entry);

    App_context&             m_context;
    std::optional<glm::mat4> m_world_from_control;
    std::optional<glm::mat4> m_control_from_world;
    Viewport_config          m_viewport_config;

private:
    std::array<Hover_entry, Hover_entry::slot_count> m_hover_entries;
};

}
