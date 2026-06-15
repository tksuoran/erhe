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

// Orientation reference frame for the transform gizmo.
//   global    - world axes (identity basis)
//   local     - the selection's own orientation
//   reference - the orientation of an arbitrary user-chosen reference node
//   selection - a frame derived from the active mesh component selection
//               (face centroid/normal/tangent, edge midpoint/direction/normal,
//               or vertex position/normal/tangent)
enum class Transform_reference_mode : unsigned int {
    global    = 0,
    local     = 1,
    reference = 2,
    selection = 3
};

static constexpr const char* c_transform_reference_mode_strings[] = {
    "Global",
    "Local",
    "Reference",
    "Selection"
};

class Transform_tool_settings
{
public:
    bool                     cast_rays            {false};
    bool                     show_translate       {true};
    bool                     show_rotate          {false};
    bool                     show_scale           {false};
    bool                     hide_inactive        {true};
    Transform_reference_mode reference_mode       {Transform_reference_mode::global};
    // [0,1] normalized-lerp weight between the two faces sharing a selected edge,
    // used to orient the temp frame's normal in Selection mode.
    float                    edge_normal_blend    {0.5f};
    bool                     translate_snap_enable{false};
    float                    translate_snap       {0.1f};
    bool                     rotate_snap_enable   {true};
    float                    rotate_snap          {15.0f};
    Scale_gizmo_mode         scale_gizmo_mode     {Scale_gizmo_mode::basic};

    // The gizmo uses the anchor's stored orientation in every mode except Global,
    // which strips it back to world axes (identity basis).
    [[nodiscard]] auto use_anchor_orientation() const -> bool
    {
        return reference_mode != Transform_reference_mode::global;
    }
    [[nodiscard]] auto is_local() const -> bool
    {
        return reference_mode == Transform_reference_mode::local;
    }
};

}
