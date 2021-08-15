#pragma once

#include "erhe/components/component.hpp"
#include "btBulletDynamicsCommon.h"

namespace editor
{

class Line_renderer;
class Text_renderer;

class Debug_draw
    : public erhe::components::Component
    , public btIDebugDraw
{
public:
    static constexpr std::string_view c_name{"Debug_draw"};
    Debug_draw();
    ~Debug_draw() override;

    // Implements Component
    void connect             () override;
    void initialize_component() override;

    // Implements btIDebugDraw
    void drawLine          (const btVector3& from, const btVector3& to, const btVector3& color) override;
    void draw3dText        (const btVector3& location, const char* textString)                  override;
    void setDebugMode      (int debugMode)                                                      override;
    auto getDebugMode      () const -> int                                                      override;
    void drawContactPoint  (const btVector3& PointOnB, const btVector3& normalOnB,
                            btScalar distance, int lifeTime, const btVector3& color)            override;
    void reportErrorWarning(const char* warningString)                                          override;

    float line_width{4.0f};

private:
    std::shared_ptr<Line_renderer> m_line_renderer;
    std::shared_ptr<Text_renderer> m_text_renderer;
    int                            m_debug_mode{0};
};

} // namespace editor
