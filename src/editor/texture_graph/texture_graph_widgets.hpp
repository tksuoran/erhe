#pragma once

#include "erhe_texgen/node_descriptor.hpp"

#include <vector>

namespace editor {

// Canvas-safe control-point editors for gradient and curve parameters. Both are
// drawn with the ImGui draw list plus InvisibleButtons (no popups, so they are
// legal inside the ax::NodeEditor canvas) and return true when the control-point
// data changed this frame.
//
// Every mutating interaction goes through an ImGui item that registers as the
// active item (InvisibleButton drag, DragFloat4, SmallButton), so the node's
// existing parameter-undo gesture capture (Texture_graph_node::node_editor)
// commits exactly one undo operation per completed gesture: it flips the node
// dirty when data changes and pushes the undo entry once no item is active again.
//
// Selection / in-progress-drag state is kept in the host window's ImGuiStorage
// keyed by the pushed id, so the editors need no per-node members.
//
// Both take a `scale` multiplying every fixed pixel constant - extents, line
// thicknesses, handle sizes and the screen-space hit thresholds. Callers on the
// node canvas pass Graph_editor_node::content_scale() (the view zoom); the
// properties panel passes 1. Without it these widgets keep their unzoomed size
// while the node around them grows, and their hit areas drift off their
// handles. Note the hit thresholds must scale with the drawing: they are
// compared against distances in the same scaled screen space.

// Gradient bar editor: draws the ramp, drag stops horizontally, double-click the
// bar to add a stop, select a stop to recolor (DragFloat4 + swatch) or delete
// it, pick the interpolation with a stepper.
[[nodiscard]] auto texture_gradient_editor(
    const char*                                id,
    std::vector<erhe::texgen::Gradient_stop>&  stops,
    erhe::texgen::Gradient_interpolation&      interpolation,
    float                                      scale = 1.0f
) -> bool;

// Curve box editor: draws the curve, drag points (endpoints keep their x),
// double-click empty space to add a point, double-click a point to remove it.
// Tangent slopes are recomputed (Catmull-Rom finite differences) on every edit,
// so the curve stays smooth without separate tangent-handle UI.
[[nodiscard]] auto texture_curve_editor(
    const char*                             id,
    std::vector<erhe::texgen::Curve_point>& points,
    float                                   scale = 1.0f
) -> bool;

} // namespace editor
