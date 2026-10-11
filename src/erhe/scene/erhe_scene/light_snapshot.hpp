#pragma once

#include "erhe_scene/light.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <string>

namespace erhe::scene {

// The shading parameters of a Light, copied for a render phase: the effective
// property values (type, color, intensity, range, spot angles, shadow flag),
// the orthonormal light frame derived from the light's world transform, and
// the identity (id, name) the diagnostics report. The renderers and the
// light projection fits read the copy, so the light node and its properties
// may be edited on another thread while a frame is rendered. capture() is
// called by the thread that owns the light (the editor's main thread in its
// prepare phase, erhe::scene_renderer::Light_set::resolve); the accessors
// mirror the Light ones they replace, and the projection methods are the
// ones Light::projection_transforms() delegates to.
class Light_snapshot
{
public:
    Light_snapshot() = default;
    explicit Light_snapshot(const Light& light);

    // The name string keeps its capacity across captures of the same light.
    void capture(const Light& light);

    [[nodiscard]] auto get_id              () const -> std::size_t        { return m_id; }
    [[nodiscard]] auto get_name            () const -> const std::string& { return m_name; }
    [[nodiscard]] auto get_light_type      () const -> Light_type         { return m_light_type; }
    [[nodiscard]] auto get_color           () const -> glm::vec3          { return m_color; }
    [[nodiscard]] auto get_effective_color () const -> glm::vec3          { return m_effective_color; }
    [[nodiscard]] auto get_intensity       () const -> float              { return m_intensity; }
    [[nodiscard]] auto get_range           () const -> float              { return m_range; }
    [[nodiscard]] auto get_inner_spot_angle() const -> float              { return m_inner_spot_angle; }
    [[nodiscard]] auto get_outer_spot_angle() const -> float              { return m_outer_spot_angle; }
    [[nodiscard]] auto get_cast_shadow     () const -> bool               { return m_cast_shadow; }
    [[nodiscard]] auto get_light_frame     () const -> const Light_frame& { return m_frame; }

    // Light::is_active() / Light::casts_shadow() evaluated on the copy.
    [[nodiscard]] auto is_active   () const -> bool;
    [[nodiscard]] auto casts_shadow() const -> bool { return is_active() && m_cast_shadow; }

    // The light's shadow projection for a view (see Light::projection_transforms).
    // Light_projection_transforms::light points at this snapshot.
    [[nodiscard]] auto projection           (const Light_projection_parameters& parameters) const -> Projection;
    [[nodiscard]] auto projection_transforms(const Light_projection_parameters& parameters, Light_shadow_use shadow_use) const -> Light_projection_transforms;

private:
    [[nodiscard]] auto stable_directional_light_projection(const Light_projection_parameters& parameters) const -> Projection;
    [[nodiscard]] auto spot_light_projection              (const Light_projection_parameters& parameters) const -> Projection;

    [[nodiscard]] auto directional_light_projection_transforms       (const Light_projection_parameters& parameters, Light_shadow_use shadow_use) const -> Light_projection_transforms;
    [[nodiscard]] auto stable_directional_light_projection_transforms(const Light_projection_parameters& parameters) const -> Light_projection_transforms;

    // Tight modular fit; defined in light_frustum_fit.cpp
    [[nodiscard]] auto tight_directional_light_projection_transforms(const Light_projection_parameters& parameters) const -> Light_projection_transforms;

    [[nodiscard]] auto spot_light_projection_transforms(const Light_projection_parameters& parameters) const -> Light_projection_transforms;

    // Point lights cast omnidirectional shadows into an R32F cube-map array
    // sampled by direction, so the per-face view-projections are computed in
    // Shadow_renderer at render time, not stored here. This only carries the
    // light world pose (so light_buffer derives the correct light position) and
    // a perspective projection with z_far = range; clip_from_world /
    // texture_from_world are unused by the cube sampling path (left identity).
    [[nodiscard]] auto point_light_projection_transforms(const Light_projection_parameters& parameters) const -> Light_projection_transforms;

    // Assembles the Light_projection_transforms for a directional light from
    // the resolved projection and the (snapped) light camera pose. Shared by
    // the stable and tight fit paths.
    [[nodiscard]] auto assemble_directional_light_projection_transforms(
        const Light_projection_parameters& parameters,
        const Projection&                  light_projection,
        const glm::mat4&                   world_from_light_camera,
        const glm::mat4&                   light_camera_from_world
    ) const -> Light_projection_transforms;

    std::size_t m_id              {0};
    std::string m_name            {};
    Light_type  m_light_type      {Light_type::directional};
    glm::vec3   m_color           {1.0f, 1.0f, 1.0f};
    glm::vec3   m_effective_color {1.0f, 1.0f, 1.0f};
    float       m_intensity       {1.0f};
    float       m_range           {100.0f};
    float       m_inner_spot_angle{0.0f};
    float       m_outer_spot_angle{0.0f};
    bool        m_cast_shadow     {true};
    Light_frame m_frame           {};
};

} // namespace erhe::scene
