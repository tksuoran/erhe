// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "scene/scene_view.hpp"
#include "rendergraph/shadow_render_node.hpp"

#include "erhe/toolkit/math_util.hpp"

namespace editor
{

auto Scene_view::get_light_projections() const -> Light_projections*
{
    auto* shadow_render_node = get_shadow_render_node();
    if (shadow_render_node == nullptr)
    {
        return nullptr;
    }
    Light_projections& light_projections = shadow_render_node->get_light_projections();
    return &light_projections;
}

auto Scene_view::get_shadow_texture() const -> erhe::graphics::Texture*
{
    const auto* shadow_render_node = get_shadow_render_node();
    if (shadow_render_node == nullptr)
    {
        return nullptr;
    }
    return shadow_render_node->get_texture().get();
}

auto Scene_view::near_position_in_world() const -> std::optional<glm::vec3>
{
    return m_near_position_in_world;
}

auto Scene_view::far_position_in_world() const -> std::optional<glm::vec3>
{
    return m_far_position_in_world;
}

auto Scene_view::position_in_world_distance(
    const float distance
) const -> std::optional<glm::vec3>
{
    if (
        !m_near_position_in_world.has_value() ||
        !m_far_position_in_world.has_value()
    )
    {
        return {};
    }

    const glm::vec3 p0        = m_near_position_in_world.value();
    const glm::vec3 p1        = m_far_position_in_world.value();
    const glm::vec3 origin    = p0;
    const glm::vec3 direction = glm::normalize(p1 - p0);
    return origin + distance * direction;
}

auto Scene_view::position_in_viewport() const -> std::optional<glm::vec2>
{
    return m_position_in_viewport;
}

auto Scene_view::get_hover(size_t slot) const -> const Hover_entry&
{
    return m_hover_entries.at(slot);
}

auto Scene_view::get_nearest_hover() const -> const Hover_entry&
{
    return m_hover_entries.at(m_nearest_slot);
}

} // namespace editor
