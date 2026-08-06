#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include "app_message.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <glm/glm.hpp>
#include <imgui/imgui.h>

#include <array>
#include <cstdint>
#include <vector>

namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}

namespace editor {

class App_context;
class App_message_bus;
class Scene_view;

// Lightmap atlas viewer (doc/lightmap_texture_viewer_plan.md): shows the
// baked atlas (or the G-buffer position / normal / albedo debug views)
// with interactive mouse pan + zoom, and optional overlays in atlas UV
// space: chart edge lines of all lightmapped meshes, highlighted edges of
// the hovered mesh primitive, and highlighted edges of the hovered facet.
// Hover sources, closest-first: the mouse over the atlas image in this
// window (hit-tested with the same corner-fan triangulation the G-buffer
// raster uses), else the 3D viewport hover (Hover_entry content slot of
// the scene view under the pointer).
class Lightmap_texture_window : public erhe::imgui::Imgui_window
{
public:
    Lightmap_texture_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context,
        App_message_bus&             app_message_bus
    );

    // Implements Imgui_window
    void imgui() override;

    // Frame the selected meshes' atlas regions on the next draw: pan/zoom
    // so every selected mesh's chart UVs are visible. Also reachable over
    // MCP (lightmap_frame_selection).
    void request_frame_selection() { m_frame_selection_request = true; }

private:
    // Overlay geometry for one atlas region, in atlas UV space ([0,1]^2
    // over the page). Rebuilt (all regions at once) whenever the layout
    // signature changes.
    class Overlay_triangle
    {
    public:
        glm::vec2     a;
        glm::vec2     b;
        glm::vec2     c;
        std::uint32_t facet{0};
    };
    class Region_overlay
    {
    public:
        std::size_t                                  region_index{0};
        glm::vec2                                    rect_min{0.0f};
        glm::vec2                                    rect_max{0.0f};
        std::vector<std::array<glm::vec2, 2>>        edges;
        std::vector<Overlay_triangle>                triangles;
        // Sanity check: indices into `triangles` that overlap another
        // triangle of the same region with positive area (UV unwrap
        // defect - triangles must tile UV space uniquely). Drawn filled
        // red at 50% alpha.
        std::vector<std::uint32_t>                   broken_triangles;
    };
    // Cache key: anything that changes the overlay geometry. The rect and
    // uv_scale_offset alone are NOT enough: a re-unwrap with unchanged
    // density produces bit-identical rects/uvso (they derive from surface
    // area only) while the chart UVs change completely - the primitive and
    // geometry identities catch that (mesh operations swap primitives).
    class Region_signature
    {
    public:
        const void* mesh           {nullptr};
        const void* primitive      {nullptr};
        const void* geometry       {nullptr};
        std::size_t primitive_index{0};
        glm::vec4   uv_scale_offset{0.0f};
        int         x{0};
        int         y{0};
        int         width{0};
        int         height{0};
        auto operator==(const Region_signature&) const -> bool = default;
    };

    void refresh_overlay_cache();

    App_context& m_context;

    // View transform: atlas pixel -> canvas pixel scale, and the canvas-
    // local offset of the atlas origin. screen = canvas + pan + uv * page * zoom.
    float  m_zoom         {1.0f};
    ImVec2 m_pan          {0.0f, 0.0f};
    bool   m_fit_requested{true};
    int    m_last_page_width {0};
    int    m_last_page_height{0};

    int    m_texture_index    {0}; // 0 = atlas, 1 = position, 2 = normal, 3 = albedo
    float  m_exposure         {1.0f};
    bool   m_show_edges       {true};
    bool   m_highlight_mesh   {true};
    bool   m_highlight_facet  {true};
    // Which spatial tiles' chart overlays (edge wireframe, hover
    // highlights, overlap fills) draw; the texture image itself always
    // shows every resident slot regardless.
    int    m_triangle_scope   {0}; // 0 = all tiles, 1 = resident tiles, 2 = active tile
    // Resident-slot boundary rects, colored like the 3D viewport tile
    // bounds debug rendering (white = active / gathering, cyan = resident).
    bool   m_show_tile_bounds {true};
    // Yellow crosshair marking the camera's XZ position within its grid
    // tile's cell, drawn in that tile's display slot rect.
    bool   m_show_camera      {true};

    std::vector<Region_signature> m_cache_signature;
    std::vector<Region_overlay>   m_overlays;
    std::size_t                   m_broken_triangle_count{0};
    bool                          m_frame_selection_request{false};

    // Scene view currently under the pointer (viewport hover highlight
    // source); cleared when the pointer leaves all viewports or the view
    // is destroyed.
    erhe::message_bus::Subscription<Hover_scene_view_message> m_hover_scene_view_subscription;
    Scene_view*                   m_hover_scene_view{nullptr};
};

} // namespace editor
