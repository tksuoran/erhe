#pragma once

#include <imgui/imgui.h>

#include <memory>
#include <optional>

namespace editor {

class App_context;
class Brush;

// What draw_brush_thumbnail() put on the screen, so the caller knows whether
// its own fallback decoration is still needed.
enum class Brush_thumbnail_result : unsigned int
{
    thumbnail, // the brush preview image was submitted (an ImGui item exists)
    spinner,   // the geometry is not ready yet: a spinner holds the square
    icon       // nothing was drawn: the caller draws its own icon in the square
};

// Where the square the thumbnail occupies is (doc/plans/deferred_brush_geometry.md
// D5). The spinner keeps that square, so a row is the same height whether its
// brush is ready or not.
class Brush_thumbnail_placement final
{
public:
    // Side of the square, in ImGui pixels.
    float size{0.0f};

    // Top-left corner of the square. When set, the spinner goes straight on
    // the window draw list and no ImGui item is emitted - the item tree draws
    // its row decorations that way, at a position its row layout computed.
    // When unset, the spinner is drawn inside an ImGui dummy item of `size`
    // placed at the cursor, which is what a slot grid (hotbar, inventory)
    // needs to keep its layout.
    std::optional<ImVec2> top_left{};
};

// Draws the thumbnail of one palette brush: the rendered preview once the
// brush's geometry is `ready`, a spinner while it is being prepared, and
// nothing at all when preparation failed (R9) or the thumbnail slot was only
// just claimed - in both of those cases the caller draws its own icon.
//
// This is the tier 2 consumer of the brush geometry (R3, R7): it calls
// Brush::request_geometry(), which returns at once, and never reaches
// Brush::get_geometry(). There is no poll: the same row asks again on the next
// frame it is visible, which is the draw that is already happening.
[[nodiscard]] auto draw_brush_thumbnail(
    App_context&                        context,
    const std::shared_ptr<Brush>&       brush,
    const Brush_thumbnail_placement&    placement
) -> Brush_thumbnail_result;

}
