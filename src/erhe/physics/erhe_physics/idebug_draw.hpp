#pragma once

#include <glm/glm.hpp>

namespace erhe::physics {

class IDebug_draw
{
public:
    struct Colors
    {
        glm::vec3 active_object               {1, 1, 1};
        glm::vec3 deactivated_object          {0, 1, 0};
        glm::vec3 wants_deactivation_object   {0, 1, 1};
        glm::vec3 disabled_deactivation_object{1, 0, 0};
        glm::vec3 disabled_simulation_object  {1, 1, 0};
        glm::vec3 aabb                        {1, 0, 0};
        glm::vec3 contact_point               {1, 1, 0};
    };

    static constexpr int c_No_debug               =    0;
    static constexpr int c_Draw_wireframe         =    1;
    static constexpr int c_Draw_aabb              =    2;
    static constexpr int c_Draw_features_text     =    4;
    static constexpr int c_Draw_contact_points    =    8;
    static constexpr int c_No_deactivation        =   16;
    static constexpr int c_No_help_text           =   32;
    static constexpr int c_Draw_text              =   64;
    static constexpr int c_Profile_timings        =  128;
    static constexpr int c_Enable_sat_comparison  =  256;
    static constexpr int c_Disable_bullet_lcp     =  512;
    static constexpr int c_Enable_ccd             = 1024;
    static constexpr int c_Draw_constraints       = (1u << 11u);
    static constexpr int c_Draw_constraint_limits = (1u << 12u);
    static constexpr int c_Fast_wireframe         = (1u << 13u);
    static constexpr int c_Draw_normals           = (1u << 14u);
    static constexpr int c_Draw_frames            = (1u << 15u);

    virtual auto get_colors          () const -> Colors                                                = 0;
    virtual void set_colors          (const Colors& colors)                                            = 0;
    virtual void draw_line           (const glm::vec3 from, const glm::vec3 to, const glm::vec3 color) = 0;
    virtual void draw_3d_text        (const glm::vec3 location, const char* text)                      = 0;
    virtual void set_debug_mode      (int debug_mode)                                                  = 0;
    virtual auto get_debug_mode      () const -> int                                                   = 0;
    virtual void draw_contact_point  (
        const glm::vec3 point,
        const glm::vec3 normal,
        float           distance,
        int             life_time,
        const glm::vec3 color)                                                                         = 0;
    virtual void report_error_warning(const char* warning)                                             = 0;
};

} // namespace erhe::physics
