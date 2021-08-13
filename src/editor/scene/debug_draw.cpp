#include "scene/debug_draw.hpp"

#include "renderers/line_renderer.hpp"
#include "renderers/text_renderer.hpp"
#include "scene/scene_root.hpp"

#include "erhe/physics/world.hpp"

#include "log.hpp"

#include <glm/glm.hpp>

#include "imgui.h"

namespace editor
{

Debug_draw::Debug_draw()
    : erhe::components::Component{c_name}
    , m_debug_mode{0}
{

}

Debug_draw::~Debug_draw() = default;

void Debug_draw::connect()
{
    require<Scene_root>();
    m_line_renderer = get<Line_renderer>();
    m_text_renderer = get<Text_renderer>();

    m_debug_mode = DBG_DrawWireframe         |
                   DBG_DrawAabb              |
                   DBG_DrawFeaturesText      |
                   DBG_DrawContactPoints     |
                   //DBG_NoDeactivation        |
                   //DBG_NoHelpText            |
                   DBG_DrawText              |
                   //DBG_ProfileTimings        |
                   //DBG_EnableSatComparison   |
                   //DBG_DisableBulletLCP      |
                   //DBG_EnableCCD             |
                   //DBG_DrawConstraints       |
                   //DBG_DrawConstraintLimits  |
                   DBG_FastWireframe         |
                   DBG_DrawNormals           |
                   DBG_DrawFrames;
}

void Debug_draw::initialize_component()
{
    get<Scene_root>()->physics_world().bullet_dynamics_world.setDebugDrawer(this);
}

void Debug_draw::drawLine(const btVector3& from, const btVector3& to, const btVector3& color)
{
    auto color_ui32 = ImGui::ColorConvertFloat4ToU32(ImVec4(color.x(), color.y(), color.z(), 1.0f));
    glm::vec3 p0{from.x(), from.y(), from.z()};
    glm::vec3 p1{to.x(), to.y(), to.z()};
    m_line_renderer->set_line_color(color_ui32);
    m_line_renderer->add_lines( { {p0, p1} }, line_width);
}

void Debug_draw::draw3dText(const btVector3& location, const char* textString)
{
    uint32_t text_color = 0xffffffffu; // abgr
    glm::vec3 text_position{location.x(), location.y(), location.z()};
    m_text_renderer->print(text_position, text_color, textString);
}

void Debug_draw::setDebugMode(int debugMode)
{
    m_debug_mode = debugMode;
}

auto Debug_draw::getDebugMode() const -> int
{
    return m_debug_mode;
}

void Debug_draw::drawContactPoint(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector3& color)
{
	drawLine(PointOnB, PointOnB + normalOnB * distance, color);
	btVector3 ncolor(0, 0, 0);
	drawLine(PointOnB, PointOnB + normalOnB * 0.01, ncolor);
}

void Debug_draw::reportErrorWarning(const char* warningString)
{
    if (warningString == nullptr)
    {
        return;
    }
    log_physics.warn("{}", warningString);
}


}
