#pragma once

#include "erhe_scene_renderer/shader_key.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace erhe::scene {
    class Light;
}

namespace erhe::scene_renderer {

// The resolved set of lights a scene is shaded with: which lights get a light
// UBO slot, in slot order, and which of those are shadow-mapped - the result
// of walking the scene's lights against Light_count_limits (see there for the
// hand-out rule). Owned by the scene host (editor Scene_root) and resolved
// once, not per frame / per view: the host invalidates it from its light
// hooks (light registered / unregistered / changed - Scene_host::
// on_light_changed via Light::notify_changed()) and resolve() recomputes only
// when invalidated or when called with different limits. Everything downstream
// (Light_projections::apply, Light_buffer::update, Shadow_renderer, the
// forward pass light layer partition) indexes this by slot and never walks
// the raw light layer again.
//
// Slot order is type-major (directional, spot, point, other), shadow-mapped
// lights first within each type - the layout the standard shader's per-type
// shadow-prefix / non-shadow-suffix light loops assume; partition holds the
// loop bounds. shadow_map_2d_slots lists the directional + spot shadow lights
// in 2D shadow map layer order (layer / render pass i is slot
// shadow_map_2d_slots[i]); point_shadow_slots likewise for the point shadow
// cubes.
class Light_set
{
public:
    // Any thread: mark the resolution stale (a light was added / removed /
    // changed). Applied by the next resolve() on the main thread.
    void invalidate();

    // Main thread. Recomputes from lights (the scene's light layer, in its
    // order = priority within a type) when invalidated or when
    // light_count_limits differ from the last resolve. Returns true when the
    // resolution changed.
    auto resolve(std::span<const std::shared_ptr<erhe::scene::Light>> lights, const Light_count_limits& light_count_limits) -> bool;

    [[nodiscard]] auto get_lights             () const -> const std::vector<std::shared_ptr<erhe::scene::Light>>& { return m_lights; }
    [[nodiscard]] auto get_partition          () const -> const Light_layer_partition&                            { return m_partition; }
    [[nodiscard]] auto get_shadow_map_2d_slots() const -> const std::vector<std::size_t>&                         { return m_shadow_map_2d_slots; }
    [[nodiscard]] auto get_point_shadow_slots () const -> const std::vector<std::size_t>&                         { return m_point_shadow_slots; }
    [[nodiscard]] auto get_light_count_limits () const -> const Light_count_limits&                               { return m_light_count_limits; }
    // Bumped by every resolve() that changed the resolution.
    [[nodiscard]] auto get_generation         () const -> uint64_t                                                { return m_generation; }
    // Slot of light, or SIZE_MAX when the light is not shaded.
    [[nodiscard]] auto find_slot              (const erhe::scene::Light* light) const -> std::size_t;

private:
    std::atomic<bool>                                m_dirty{true};
    Light_count_limits                               m_light_count_limits{};
    std::vector<std::shared_ptr<erhe::scene::Light>> m_lights;
    Light_layer_partition                            m_partition{};
    std::vector<std::size_t>                         m_shadow_map_2d_slots;
    std::vector<std::size_t>                         m_point_shadow_slots;
    uint64_t                                         m_generation{0};
};

} // namespace erhe::scene_renderer
