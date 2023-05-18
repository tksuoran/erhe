#pragma once


namespace editor
{

class Transform_tool_settings
{
public:
    bool  cast_rays            {false};
    bool  show_translate       {true};
    bool  show_rotate          {false};
    bool  show_scale           {false};
    bool  hide_inactive        {true};
    bool  local                {false};
    float gizmo_scale          {1.0f};

    bool  translate_snap_enable{false};
    float translate_snap       {0.1f};

    bool  rotate_snap_enable   {false};
    float rotate_snap          {15.0f};
};

} // namespace editor
