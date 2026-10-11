#include "erhe_scene/light_snapshot.hpp"

namespace erhe::scene {

Light_snapshot::Light_snapshot(const Light& light)
{
    capture(light);
}

void Light_snapshot::capture(const Light& light)
{
    m_id               = light.get_id();
    m_name             = light.get_name();
    m_light_type       = light.get_light_type();
    m_color            = light.get_color();
    m_effective_color  = light.get_effective_color();
    m_intensity        = light.get_intensity();
    m_range            = light.get_range();
    m_inner_spot_angle = light.get_inner_spot_angle();
    m_outer_spot_angle = light.get_outer_spot_angle();
    m_cast_shadow      = light.get_cast_shadow();
    m_frame            = light.get_light_frame();
}

auto Light_snapshot::is_active() const -> bool
{
    return
        (
            (m_color.r > 0) || (m_color.g > 0) || (m_color.b > 0)
        ) &&
        (m_intensity > 0.0f) &&
        (
            (m_light_type != Light_type::point) || (m_range > 0.0f)
        ) &&
        (
            (m_light_type != Light_type::spot) || (m_outer_spot_angle > 0.0f)
        );
}

} // namespace erhe::scene
