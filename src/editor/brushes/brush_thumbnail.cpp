#include "brushes/brush_thumbnail.hpp"
#include "brushes/brush.hpp"

#include "app_context.hpp"
#include "graphics/thumbnails.hpp"
#include "preview/brush_preview.hpp"

#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_helpers.hpp"

#include <algorithm>

namespace editor {

namespace {

// The square the spinner occupies, and the spinner inside it. Returns the
// centre of that square; emits the ImGui dummy item when the placement asks
// for one, so the slot grids keep their layout.
[[nodiscard]] auto place_spinner_square(const Brush_thumbnail_placement& placement) -> ImVec2
{
    const float side = placement.size;
    if (placement.top_left.has_value()) {
        const ImVec2 top_left = placement.top_left.value();
        return ImVec2{top_left.x + (0.5f * side), top_left.y + (0.5f * side)};
    }
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2{side, side});
    // The thumbnail path advances the layout with SameLine (Thumbnails::draw);
    // the spinner stands in for it, so it does the same.
    ImGui::SameLine();
    return ImVec2{cursor.x + (0.5f * side), cursor.y + (0.5f * side)};
}

} // anonymous namespace

auto draw_brush_thumbnail(
    App_context&                     context,
    const std::shared_ptr<Brush>&    brush,
    const Brush_thumbnail_placement& placement
) -> Brush_thumbnail_result
{
    if (!brush || (context.thumbnails == nullptr) || (context.brush_preview == nullptr)) {
        return Brush_thumbnail_result::icon;
    }

    // Tier 2 (R3, R7): ask and return. A brush that is already ready, failed or
    // preparing is left alone by the request.
    static_cast<void>(brush->request_geometry());

    const Brush_geometry_state state = brush->get_geometry_state();
    if (state == Brush_geometry_state::failed) {
        // R9: the caller's icon stands for a brush that has no geometry at all.
        return Brush_thumbnail_result::icon;
    }
    if (state != Brush_geometry_state::ready) {
        const ImVec2 center = place_spinner_square(placement);
        const float  radius = 0.32f * placement.size;
        const float  width  = std::max(1.0f, 0.12f * placement.size);
        erhe::imgui::draw_spinner(center, radius, width, ImGui::GetColorU32(ImGuiCol_Text));
        return Brush_thumbnail_result::spinner;
    }

    // Ready: the preview render (tier 1 internally, D6) is reached only from
    // here, and only for a brush whose geometry is already built. No thumbnail
    // slot was claimed for the waiting brush above, because Thumbnails::draw is
    // what claims one and the not-ready branch never calls it.
    const bool thumbnail_drawn = context.thumbnails->draw(
        brush,
        // Deferred callback (see Thumbnails::draw): captures only whole-app
        // lifetime state and shared ownership of the brush.
        [&app_context = context, brush](const std::shared_ptr<erhe::graphics::Texture>& texture, unsigned int texture_layer, int64_t time) {
            app_context.brush_preview->render_preview(texture, texture_layer, brush, time);
        },
        placement.size
    );
    return thumbnail_drawn ? Brush_thumbnail_result::thumbnail : Brush_thumbnail_result::icon;
}

}
