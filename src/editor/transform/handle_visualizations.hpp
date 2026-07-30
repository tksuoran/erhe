#pragma once

#include "erhe_math/aabb.hpp"
#include "erhe_scene/trs_transform.hpp"

#include <glm/glm.hpp>

#include <optional>

namespace editor {

class App_context;
class Render_context;
class Scene_view;

enum class Handle : unsigned int;

// Analytic pick result for a gizmo handle.
class Handle_pick
{
public:
    Handle    handle  {};
    float     t       {0.0f};   // distance along the pick ray
    glm::vec3 position{0.0f};   // world-space point on the handle
};

// The transform gizmo, drawn entirely with the debug primitive renderer
// (x-ray lines and filled triangles - no scene meshes) and hit tested
// analytically. render() runs per view from Transform_tool::tool_render;
// pick() runs from Transform_tool::update_hover with the control ray.
// Both share the same handle visibility rules, so a handle is pickable
// exactly when it is drawn.
class Handle_visualizations
{
public:
    explicit Handle_visualizations(App_context& app_context);

    // Bounding-box scale gizmo
    [[nodiscard]] auto is_box_valid () const -> bool             { return m_box_valid; }
    [[nodiscard]] auto get_box_frame() const -> const glm::mat4& { return m_box_frame; }
    [[nodiscard]] auto get_box_aabb () const -> const erhe::math::Aabb& { return m_box_aabb; }

    // World-space length corresponding to the gizmo's overall radius (the rotate ring),
    // using the view-dependent scale applied to the handles. Scale_tool uses this
    // to normalize the uniform-scale drag displacement.
    [[nodiscard]] auto get_gizmo_radius() const -> float;

    void viewport_toolbar ();
    void update_for_view  (Scene_view* scene_view);
    void update_transforms();

    void set_anchor(const erhe::scene::Trs_transform& world_from_anchor);

    // Draws all handles visible in the view being rendered. hover / active
    // brighten and thicken the corresponding handle.
    void render(const Render_context& context, Handle hover_handle, Handle active_handle);

    // Analytic hit test with the control ray; returns the nearest handle
    // within pick range, or nullopt. Only handles that render() would draw
    // are pickable (in visible-arcs mode, only the unoccluded ring arcs).
    [[nodiscard]] auto pick(const glm::vec3& ray_origin, const glm::vec3& ray_direction) const -> std::optional<Handle_pick>;

private:
    void compute_selection_box();

    // Anchor orientation basis (world axes in Global reference mode).
    [[nodiscard]] auto get_basis() const -> glm::mat3;

    // Whether the gizmo is present at all (a selection or component anchor exists).
    [[nodiscard]] auto has_target() const -> bool;

    // Per-handle visibility: tool toggles (show_translate / rotate / scale),
    // the scale gizmo mode, positive-only translate mode, and the
    // hide-inactive-during-drag rule. Shared by render() and pick().
    [[nodiscard]] auto is_handle_shown(Handle handle) const -> bool;

    App_context&               m_context;
    Scene_view*                m_scene_view{nullptr};
    erhe::scene::Trs_transform m_world_from_anchor;
    float                      m_view_distance{1.0f};
    float                      m_view_scale   {1.0f}; // world units per gizmo unit, from update_transforms()
    glm::mat4                  m_box_frame{1.0f};
    erhe::math::Aabb           m_box_aabb{};
    bool                       m_box_valid{false};
};

}
