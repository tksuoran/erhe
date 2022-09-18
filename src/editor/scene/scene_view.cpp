// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "scene/scene_view.hpp"
#include "rendergraph/shadow_render_node.hpp"

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


} // namespace editor
