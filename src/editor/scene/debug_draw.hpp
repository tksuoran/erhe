#pragma once

#include "erhe_physics/idebug_draw.hpp"

namespace editor {

class App_context;

class Debug_draw : public erhe::physics::IDebug_draw
{
public:
    explicit Debug_draw(App_context& context);
    virtual ~Debug_draw() noexcept; // TODO override - make virtual ~IDebug_draw()

    // Implemnents IDebug_draw
    auto get_colors          () const -> Colors                                                override;
    void set_colors          (const Colors& colors)                                            override;
    void draw_line           (const glm::vec3 from, const glm::vec3 to, const glm::vec3 color) override;
    void draw_3d_text        (const glm::vec3 location, const char* text)                      override;
    void set_debug_mode      (int debug_mode)                                                  override;
    auto get_debug_mode      () const -> int                                                   override;
    void draw_contact_point(
        const glm::vec3 point,
        const glm::vec3 normal,
        float           distance,
        int             life_time,
        const glm::vec3 color
    )                                                                                          override;
    void report_error_warning(const char* warning)                                             override;

    float line_width{4.0f};

private:
    App_context& m_context;
    int          m_debug_mode{0};
    Colors       m_colors;
};

}
