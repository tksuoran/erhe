#include "erhe_scene_renderer/light_set.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include "erhe_scene/light.hpp"
#include "erhe_profile/profile.hpp"

#include <limits>

namespace erhe::scene_renderer {

namespace {

[[nodiscard]] auto operator==(const Light_count_limits& lhs, const Light_count_limits& rhs) -> bool
{
    for (std::size_t t = 0; t < light_type_count; ++t) {
        if (lhs.per_type_shadow    [t] != rhs.per_type_shadow    [t]) { return false; }
        if (lhs.per_type_unshadowed[t] != rhs.per_type_unshadowed[t]) { return false; }
    }
    return true;
}

}

void Light_set::invalidate()
{
    m_dirty.store(true, std::memory_order_release);
}

auto Light_set::resolve(
    const std::span<const std::shared_ptr<erhe::scene::Light>> lights,
    const Light_count_limits&                                  light_count_limits
) -> bool
{
    if (!m_dirty.exchange(false, std::memory_order_acq_rel) && (light_count_limits == m_light_count_limits)) {
        return false;
    }

    ERHE_PROFILE_FUNCTION();

    m_light_count_limits = light_count_limits;

    // Pass 1: the light layer partition = how many lights of each type get a
    // shadow / unshadowed slot. compute_light_layer_partition walks the lights
    // in input order against the limits with the same rule as pass 2 below,
    // so its counts are exactly the slots handed out.
    m_partition = compute_light_layer_partition(lights, light_count_limits);
    const std::size_t (&per_type_shadow)   [light_type_count] = m_partition.per_type_shadow;
    const std::size_t (&per_type_nonshadow)[light_type_count] = m_partition.per_type_nonshadow;

    // Base slots: type major (directional, spot, point, other), shadow minor
    // (shadow-mapped lights, then non-shadow lights within each type).
    std::size_t base_shadow   [light_type_count] = {0, 0, 0, 0};
    std::size_t base_nonshadow[light_type_count] = {0, 0, 0, 0};
    std::size_t slot_count = 0;
    for (std::size_t t = 0; t < light_type_count; ++t) {
        base_shadow   [t]  = slot_count;
        slot_count        += per_type_shadow[t];
        base_nonshadow[t]  = slot_count;
        slot_count        += per_type_nonshadow[t];
    }

    // Pass 2: walk the input order and place each light into its slot: a
    // shadow slot while the light casts shadows and its type's shadow count
    // has room, else an unshadowed slot while that count has room, else the
    // light is not shaded (not in the set). Inactive lights (e.g. a zero-range
    // point light) are never shaded.
    m_lights.assign(slot_count, std::shared_ptr<erhe::scene::Light>{});
    std::size_t cursor_shadow   [light_type_count] = {0, 0, 0, 0};
    std::size_t cursor_nonshadow[light_type_count] = {0, 0, 0, 0};
    std::size_t inactive_light_count{};
    std::size_t directional_light_count{};
    std::size_t spot_light_count{};
    std::size_t point_light_count{};
    for (const std::shared_ptr<erhe::scene::Light>& light : lights) {
        if (!light) {
            continue;
        }
        if (!light->is_active()) {
            ++inactive_light_count;
            continue;
        }

        switch (light->type) {
            case erhe::scene::Light_type::directional: ++directional_light_count; break;
            case erhe::scene::Light_type::spot:        ++spot_light_count;        break;
            case erhe::scene::Light_type::point:       ++point_light_count;       break;
            default: break;
        }

        const std::size_t t = light_type_index(light->type);
        if (light->cast_shadow && (cursor_shadow[t] < per_type_shadow[t])) {
            m_lights[base_shadow[t] + cursor_shadow[t]] = light;
            ++cursor_shadow[t];
        } else if (cursor_nonshadow[t] < per_type_nonshadow[t]) {
            m_lights[base_nonshadow[t] + cursor_nonshadow[t]] = light;
            ++cursor_nonshadow[t];
        }
    }

    // Dense shadow map orders: the directional + spot shadow slots share the
    // 2D shadow map array (layer i = slot m_shadow_map_2d_slots[i], directional
    // first), each point shadow slot gets a cube of the cube-map array.
    m_shadow_map_2d_slots.clear();
    m_point_shadow_slots.clear();
    for (std::size_t t = 0; t < 2; ++t) {
        for (std::size_t i = 0; i < per_type_shadow[t]; ++i) {
            m_shadow_map_2d_slots.push_back(base_shadow[t] + i);
        }
    }
    for (std::size_t i = 0; i < per_type_shadow[2]; ++i) {
        m_point_shadow_slots.push_back(base_shadow[2] + i);
    }

    log_light_set->debug(
        "Light_set::resolve\n  {} directional lights {} spot lights {} point lights",
        directional_light_count,
        spot_light_count,
        point_light_count
    );
    log_light_set->debug(
        "  directional lights: {} with shadow {} without shadow",
        m_partition.per_type_shadow   [light_type_index(erhe::scene::Light_type::directional)],
        m_partition.per_type_nonshadow[light_type_index(erhe::scene::Light_type::directional)]
    );
    log_light_set->debug(
        "  spot lights: {} with shadow {} without shadow",
        m_partition.per_type_shadow   [light_type_index(erhe::scene::Light_type::spot)],
        m_partition.per_type_nonshadow[light_type_index(erhe::scene::Light_type::spot)]
    );
    log_light_set->debug(
        "  point lights: {} with shadow {} without shadow",
        m_partition.per_type_shadow   [light_type_index(erhe::scene::Light_type::point)],
        m_partition.per_type_nonshadow[light_type_index(erhe::scene::Light_type::point)]
    );
    log_light_set->debug(
        "  shadow map 2d slots {}, shadow map point slots {}, lights total {}",
        m_shadow_map_2d_slots.size(),
        m_point_shadow_slots.size(),
        m_lights.size()
    );

    ++m_generation;
    return true;
}

auto Light_set::find_slot(const erhe::scene::Light* light) const -> std::size_t
{
    for (std::size_t slot = 0, end = m_lights.size(); slot < end; ++slot) {
        if (m_lights[slot].get() == light) {
            return slot;
        }
    }
    return std::numeric_limits<std::size_t>::max();
}

} // namespace erhe::scene_renderer
