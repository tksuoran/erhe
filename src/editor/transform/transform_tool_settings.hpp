#pragma once

namespace editor {

enum class Scale_gizmo_mode : unsigned int {
    basic        = 0,
    bounding_box = 1
};

static constexpr const char* c_scale_gizmo_mode_strings[] = {
    "Basic",
    "Bounding Box"
};

class Transform_tool_settings
{
public:
    bool             cast_rays            {false};
    bool             show_translate       {true};
    bool             show_rotate          {false};
    bool             show_scale           {false};
    bool             hide_inactive        {true};
    bool             local                {false};
    bool             translate_snap_enable{false};
    float            translate_snap       {0.1f};
    bool             rotate_snap_enable   {true};
    float            rotate_snap          {15.0f};
    Scale_gizmo_mode scale_gizmo_mode     {Scale_gizmo_mode::basic};
};

}
