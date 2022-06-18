#include "scene/debug_draw.hpp"
#include "log.hpp"
#include "scene/scene_root.hpp"

#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/renderers/text_renderer.hpp"
#include "erhe/physics/iworld.hpp"

#include <glm/glm.hpp>

namespace editor
{

Debug_draw::Debug_draw()
    : erhe::components::Component{c_label}
    , m_debug_mode{0}
{

}

Debug_draw::~Debug_draw() noexcept
{
}

void Debug_draw::declare_required_components()
{
    using IDebug_draw = erhe::physics::IDebug_draw;

    require<Scene_root>();

    m_debug_mode =
        IDebug_draw::c_Draw_wireframe           |
        IDebug_draw::c_Draw_aabb                |
        IDebug_draw::c_Draw_features_text       |
        IDebug_draw::c_Draw_contact_points      |
        //IDebug_draw::c_No_deactivation        |
        //IDebug_draw::c_No_nelp_text           |
        IDebug_draw::c_Draw_text                |
        //IDebug_draw::c_Profile_timings        |
        //IDebug_draw::c_Enable_sat-comparison  |
        //IDebug_draw::c_Disable_bullet_lcp     |
        //IDebug_draw::c_Enable_ccd             |
        //IDebug_draw::c_Draw_constraints       |
        //IDebug_draw::c_Draw_constraint_limits |
        IDebug_draw::c_Fast_wireframe           |
        IDebug_draw::c_Draw_normals             |
        IDebug_draw::c_Draw_frames;
}

void Debug_draw::initialize_component()
{
    get<Scene_root>()->physics_world().set_debug_drawer(this);
}

void Debug_draw::post_initialize()
{
    m_line_renderer_set = get<erhe::application::Line_renderer_set>();
    m_text_renderer     = get<erhe::application::Text_renderer>();
}

auto Debug_draw::get_colors() const -> Colors
{
    return m_colors;
}

void Debug_draw::set_colors(const Colors& colors)
{
    m_colors = colors;
}

void Debug_draw::draw_line(const glm::vec3 from, const glm::vec3 to, const glm::vec3 color)
{
    auto color_ui32 = erhe::toolkit::convert_float4_to_uint32(color);
    m_line_renderer_set->visible.set_line_color(color_ui32);
    m_line_renderer_set->visible.add_lines( { {from, to} }, line_width);
}

void Debug_draw::draw_3d_text(const glm::vec3 location, const char* text)
{
    uint32_t text_color = 0xffffffffu; // abgr
    m_text_renderer->print(location, text_color, text);
}

void Debug_draw::set_debug_mode(int debug_mode)
{
    m_debug_mode = debug_mode;
}

auto Debug_draw::get_debug_mode() const -> int
{
    return m_debug_mode;
}

void Debug_draw::draw_contact_point(
    const glm::vec3 point,
    const glm::vec3 normal,
    float           distance,
    int             lifeTime,
    const glm::vec3 color)
{
    static_cast<void>(lifeTime);

	draw_line(point, point + normal * distance, color);
    glm::vec3 ncolor{0};
	draw_line(point, point + (normal * 0.01f), ncolor);
}

void Debug_draw::report_error_warning(const char* warning)
{
    if (warning == nullptr)
    {
        return;
    }
    log_physics->warn(warning);
}

}
