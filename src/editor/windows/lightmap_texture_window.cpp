#include "windows/lightmap_texture_window.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "renderers/lightmap_baker.hpp"
#include "scene/scene_view.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"

#include <fmt/format.h>
#include <geogram/mesh/mesh.h>
#include <imgui/imgui.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace editor {

namespace {

[[nodiscard]] auto hash_edge(std::uint32_t v0, std::uint32_t v1, const glm::vec2& uv0, const glm::vec2& uv1) -> std::uint64_t
{
    // FNV-1a over the vertex-id pair and the UV bit patterns, so the two
    // sides of a seam edge (same vertices, different UVs) both survive
    // dedup while an interior edge (both facets agree on UVs) draws once.
    std::uint64_t hash = 0xcbf29ce484222325ull;
    const auto mix = [&hash](std::uint32_t value) {
        hash = (hash ^ value) * 0x100000001b3ull;
    };
    mix(v0);
    mix(v1);
    mix(std::bit_cast<std::uint32_t>(uv0.x));
    mix(std::bit_cast<std::uint32_t>(uv0.y));
    mix(std::bit_cast<std::uint32_t>(uv1.x));
    mix(std::bit_cast<std::uint32_t>(uv1.y));
    return hash;
}

// Positive-area triangle-triangle overlap in 2D via the separating axis
// theorem. Contact (shared edges/vertices of adjacent triangles) does not
// count: any axis whose interval overlap is <= eps separates.
[[nodiscard]] auto triangles_overlap(
    const glm::vec2& a0, const glm::vec2& a1, const glm::vec2& a2,
    const glm::vec2& b0, const glm::vec2& b1, const glm::vec2& b2,
    const float      eps
) -> bool
{
    const glm::vec2 tri_a[3] = { a0, a1, a2 };
    const glm::vec2 tri_b[3] = { b0, b1, b2 };
    const auto separated_on_axis = [&](const glm::vec2& axis) -> bool {
        float min_a =  std::numeric_limits<float>::max();
        float max_a = -std::numeric_limits<float>::max();
        float min_b =  std::numeric_limits<float>::max();
        float max_b = -std::numeric_limits<float>::max();
        for (int i = 0; i < 3; ++i) {
            const float pa = glm::dot(axis, tri_a[i]);
            const float pb = glm::dot(axis, tri_b[i]);
            min_a = std::min(min_a, pa); max_a = std::max(max_a, pa);
            min_b = std::min(min_b, pb); max_b = std::max(max_b, pb);
        }
        return (std::min(max_a, max_b) - std::max(min_a, min_b)) <= eps;
    };
    for (int i = 0; i < 3; ++i) {
        const glm::vec2 ea = tri_a[(i + 1) % 3] - tri_a[i];
        const glm::vec2 eb = tri_b[(i + 1) % 3] - tri_b[i];
        const float la = glm::length(ea);
        const float lb = glm::length(eb);
        if ((la > 0.0f) && separated_on_axis(glm::vec2{-ea.y, ea.x} / la)) {
            return false;
        }
        if ((lb > 0.0f) && separated_on_axis(glm::vec2{-eb.y, eb.x} / lb)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto point_in_triangle(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) -> bool
{
    const auto edge_sign = [](const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2) -> float {
        return (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
    };
    const float d0 = edge_sign(a, b, p);
    const float d1 = edge_sign(b, c, p);
    const float d2 = edge_sign(c, a, p);
    const bool has_negative = (d0 < 0.0f) || (d1 < 0.0f) || (d2 < 0.0f);
    const bool has_positive = (d0 > 0.0f) || (d1 > 0.0f) || (d2 > 0.0f);
    return !(has_negative && has_positive); // either winding
}

} // anonymous namespace

Lightmap_texture_window::Lightmap_texture_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context,
    App_message_bus&             app_message_bus
)
    : Imgui_window{imgui_renderer, imgui_windows, "Lightmap Texture", "lightmap_texture", true}
    , m_context   {app_context}
{
    m_hover_scene_view_subscription = app_message_bus.hover_scene_view.subscribe(
        [this](Hover_scene_view_message& message) {
            if ((message.destroyed_scene_view != nullptr) && (m_hover_scene_view == message.destroyed_scene_view)) {
                m_hover_scene_view = nullptr;
                return;
            }
            // Track the CURRENT hover (null when the pointer leaves all
            // viewports) - a stale highlight would be misleading here, so
            // this deliberately does not keep a last-hover cache.
            m_hover_scene_view = message.scene_view;
        }
    );
}

void Lightmap_texture_window::refresh_overlay_cache()
{
    const Lightmap_baker::Atlas_layout& layout = m_context.lightmap_baker->get_layout();

    std::vector<Region_signature> signature;
    signature.reserve(layout.regions.size());
    for (const Lightmap_baker::Instance_region& region : layout.regions) {
        const erhe::primitive::Primitive* primitive = nullptr;
        const erhe::geometry::Geometry*   geometry  = nullptr;
        if (region.mesh) {
            const std::vector<erhe::scene::Mesh_primitive>& primitives = region.mesh->get_primitives();
            if (region.primitive_index < primitives.size()) {
                primitive = primitives[region.primitive_index].primitive.get();
                if ((primitive != nullptr) && primitive->render_shape) {
                    geometry = primitive->render_shape->get_geometry().get();
                }
            }
        }
        signature.push_back(
            Region_signature{
                .mesh            = region.mesh.get(),
                .primitive       = primitive,
                .geometry        = geometry,
                .primitive_index = region.primitive_index,
                .uv_scale_offset = region.uv_scale_offset,
                .x               = region.x,
                .y               = region.y,
                .width           = region.width,
                .height          = region.height
            }
        );
    }
    if (signature == m_cache_signature) {
        return;
    }
    m_cache_signature = std::move(signature);
    m_overlays.clear();

    const glm::vec2 page_size{static_cast<float>(layout.width), static_cast<float>(layout.height)};
    for (std::size_t region_index = 0; region_index < layout.regions.size(); ++region_index) {
        const Lightmap_baker::Instance_region& region = layout.regions[region_index];
        if (!region.mesh) {
            continue;
        }
        const std::vector<erhe::scene::Mesh_primitive>& primitives = region.mesh->get_primitives();
        if (region.primitive_index >= primitives.size()) {
            continue;
        }
        const erhe::primitive::Primitive* const primitive = primitives[region.primitive_index].primitive.get();
        if ((primitive == nullptr) || !primitive->render_shape) {
            continue;
        }
        const std::shared_ptr<erhe::geometry::Geometry>& geometry = primitive->render_shape->get_geometry();
        if (!geometry) {
            continue;
        }
        erhe::geometry::Mesh_attributes& attributes = geometry->get_attributes();
        if (!attributes.corner_texcoord_2.has(0)) {
            continue;
        }
        const GEO::Mesh& geo_mesh = geometry->get_mesh();
        const glm::vec2 uv_scale {region.uv_scale_offset.x, region.uv_scale_offset.y};
        const glm::vec2 uv_offset{region.uv_scale_offset.z, region.uv_scale_offset.w};
        const auto atlas_uv = [&](const GEO::vec2f& uv) -> glm::vec2 {
            return glm::vec2{uv.x, uv.y} * uv_scale + uv_offset;
        };

        Region_overlay overlay;
        overlay.region_index = region_index;
        overlay.rect_min = glm::vec2{static_cast<float>(region.x),                static_cast<float>(region.y)}                 / page_size;
        overlay.rect_max = glm::vec2{static_cast<float>(region.x + region.width), static_cast<float>(region.y + region.height)} / page_size;

        std::unordered_set<std::uint64_t> seen_edges;
        seen_edges.reserve(geo_mesh.facet_corners.nb());
        std::vector<glm::vec2> facet_uvs;
        for (GEO::index_t facet : geo_mesh.facets) {
            const GEO::index_t corner_count = geo_mesh.facets.nb_corners(facet);
            facet_uvs.clear();
            bool complete = true;
            for (GEO::index_t k = 0; k < corner_count; ++k) {
                const GEO::index_t corner = geo_mesh.facets.corner(facet, k);
                const std::optional<GEO::vec2f> uv_opt = attributes.corner_texcoord_2.try_get(corner);
                if (!uv_opt.has_value()) {
                    complete = false;
                    break;
                }
                facet_uvs.push_back(atlas_uv(uv_opt.value()));
            }
            if (!complete || (facet_uvs.size() < 3)) {
                continue;
            }
            // Edges, deduped across facets (order-normalized by vertex id,
            // UVs included in the key so both seam sides draw).
            for (GEO::index_t k = 0; k < corner_count; ++k) {
                const GEO::index_t k_next = (k + 1) % corner_count;
                std::uint32_t v0  = geo_mesh.facet_corners.vertex(geo_mesh.facets.corner(facet, k));
                std::uint32_t v1  = geo_mesh.facet_corners.vertex(geo_mesh.facets.corner(facet, k_next));
                glm::vec2     uv0 = facet_uvs[k];
                glm::vec2     uv1 = facet_uvs[k_next];
                if (v0 == v1) {
                    continue;
                }
                if (v1 < v0) {
                    std::swap(v0, v1);
                    std::swap(uv0, uv1);
                }
                if (seen_edges.insert(hash_edge(v0, v1, uv0, uv1)).second) {
                    overlay.edges.push_back({uv0, uv1});
                }
            }
            // Corner-fan triangulation, matching the G-buffer raster, for
            // the hover hit test.
            for (std::size_t k = 1; k + 1 < facet_uvs.size(); ++k) {
                overlay.triangles.push_back(
                    Overlay_triangle{
                        .a     = facet_uvs[0],
                        .b     = facet_uvs[k],
                        .c     = facet_uvs[k + 1],
                        .facet = static_cast<std::uint32_t>(facet)
                    }
                );
            }
        }
        // Sanity check (UV unwrap defect detector): the triangles of a
        // region must tile UV space uniquely - any pair overlapping with
        // positive area is broken (bowties / folded charts / outlier corner
        // UVs; see doc/geogram_atlas_packing_feature_request.md). Grid-
        // bucketed pairwise SAT with a quarter-texel epsilon, so shared
        // edges of adjacent triangles do not count.
        {
            const float       eps            = 0.25f / std::max(page_size.x, 1.0f);
            const std::size_t triangle_count = overlay.triangles.size();
            std::vector<glm::vec2> box_lo(triangle_count);
            std::vector<glm::vec2> box_hi(triangle_count);
            std::vector<std::uint8_t> degenerate(triangle_count, 0u);
            glm::vec2 chart_lo{ std::numeric_limits<float>::max()};
            glm::vec2 chart_hi{-std::numeric_limits<float>::max()};
            for (std::size_t i = 0; i < triangle_count; ++i) {
                const Overlay_triangle& t = overlay.triangles[i];
                box_lo[i] = glm::min(t.a, glm::min(t.b, t.c));
                box_hi[i] = glm::max(t.a, glm::max(t.b, t.c));
                chart_lo  = glm::min(chart_lo, box_lo[i]);
                chart_hi  = glm::max(chart_hi, box_hi[i]);
                const glm::vec2 ab = t.b - t.a;
                const glm::vec2 ac = t.c - t.a;
                degenerate[i] = (std::abs(ab.x * ac.y - ab.y * ac.x) * 0.5f) <= (eps * eps) ? 1u : 0u;
            }
            const glm::vec2 chart_extent = glm::max(chart_hi - chart_lo, glm::vec2{1.0e-6f});
            const int grid_dim = std::clamp(static_cast<int>(std::ceil(std::sqrt(static_cast<float>(std::max<std::size_t>(triangle_count, 1))))), 1, 64);
            std::vector<std::vector<std::uint32_t>> cells(static_cast<std::size_t>(grid_dim) * grid_dim);
            const auto cell_range = [&](const glm::vec2& lo, const glm::vec2& hi, int& x0, int& y0, int& x1, int& y1) {
                x0 = std::clamp(static_cast<int>((lo.x - chart_lo.x) / chart_extent.x * grid_dim), 0, grid_dim - 1);
                y0 = std::clamp(static_cast<int>((lo.y - chart_lo.y) / chart_extent.y * grid_dim), 0, grid_dim - 1);
                x1 = std::clamp(static_cast<int>((hi.x - chart_lo.x) / chart_extent.x * grid_dim), 0, grid_dim - 1);
                y1 = std::clamp(static_cast<int>((hi.y - chart_lo.y) / chart_extent.y * grid_dim), 0, grid_dim - 1);
            };
            for (std::size_t i = 0; i < triangle_count; ++i) {
                if (degenerate[i]) {
                    continue;
                }
                int x0, y0, x1, y1;
                cell_range(box_lo[i], box_hi[i], x0, y0, x1, y1);
                for (int y = y0; y <= y1; ++y) {
                    for (int x = x0; x <= x1; ++x) {
                        cells[static_cast<std::size_t>(y) * grid_dim + x].push_back(static_cast<std::uint32_t>(i));
                    }
                }
            }
            std::vector<std::uint8_t>         broken(triangle_count, 0u);
            std::unordered_set<std::uint64_t> tested;
            for (const std::vector<std::uint32_t>& cell : cells) {
                for (std::size_t a = 0; a < cell.size(); ++a) {
                    for (std::size_t b = a + 1; b < cell.size(); ++b) {
                        const std::uint32_t i = cell[a];
                        const std::uint32_t j = cell[b];
                        const Overlay_triangle& ti = overlay.triangles[i];
                        const Overlay_triangle& tj = overlay.triangles[j];
                        if (ti.facet == tj.facet) {
                            continue; // fan triangles of one facet
                        }
                        if (broken[i] && broken[j]) {
                            continue;
                        }
                        if ((box_hi[i].x < box_lo[j].x) || (box_hi[j].x < box_lo[i].x) ||
                            (box_hi[i].y < box_lo[j].y) || (box_hi[j].y < box_lo[i].y)) {
                            continue;
                        }
                        if (!tested.insert((static_cast<std::uint64_t>(std::min(i, j)) << 32) | std::max(i, j)).second) {
                            continue;
                        }
                        if (triangles_overlap(ti.a, ti.b, ti.c, tj.a, tj.b, tj.c, eps)) {
                            broken[i] = 1u;
                            broken[j] = 1u;
                        }
                    }
                }
            }
            for (std::size_t i = 0; i < triangle_count; ++i) {
                if (broken[i]) {
                    overlay.broken_triangles.push_back(static_cast<std::uint32_t>(i));
                }
            }
        }
        m_overlays.push_back(std::move(overlay));
    }
    m_broken_triangle_count = 0;
    for (const Region_overlay& overlay : m_overlays) {
        m_broken_triangle_count += overlay.broken_triangles.size();
    }
}

void Lightmap_texture_window::imgui()
{
    Lightmap_baker* const baker = m_context.lightmap_baker;
    if (baker == nullptr) {
        ImGui::TextUnformatted("Lightmap baker is not available.");
        return;
    }
    if (!baker->is_bake_supported()) {
        const bool capture_layer =
            (m_context.graphics_device != nullptr) &&
            m_context.graphics_device->get_info().ray_query_disabled_by_capture_layer;
        if (capture_layer) {
            ImGui::TextColored(
                ImVec4{1.0f, 0.8f, 0.2f, 1.0f},
                "Baking disabled: Xcode GPU frame-capture layer is loaded (no GPU ray tracing). "
                "Fix: scheme GPU Frame Capture = Disabled, then relaunch. Viewing still works."
            );
        } else {
            ImGui::TextColored(
                ImVec4{1.0f, 0.8f, 0.2f, 1.0f},
                "Baking disabled: GPU ray tracing is not supported by this device / backend. Viewing still works."
            );
        }
    }
    const Lightmap_baker::Atlas_layout& layout = baker->get_layout();

    // Toolbar.
    const char* const texture_names[] = { "Atlas", "Position", "Normal", "Albedo" };
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("##texture", &m_texture_index, texture_names, IM_ARRAYSIZE(texture_names));
    ImGui::SameLine();
    if (ImGui::Button("Fit")) {
        m_fit_requested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Frame Selection")) {
        m_frame_selection_request = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Pan/zoom so every selected mesh's atlas region is visible (also: MCP lightmap_frame_selection)");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Edges", &m_show_edges);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Chart edge lines of all lightmapped meshes");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Mesh", &m_highlight_mesh);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Highlight edges of the hovered mesh primitive");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Triangle", &m_highlight_facet);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Highlight edges of the hovered triangle (facet)");
    }
    if (m_texture_index == 0) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderFloat("Exposure", &m_exposure, 0.0f, 4.0f, "%.2f");
    }
    ImGui::SameLine();
    if (m_broken_triangle_count > 0) {
        ImGui::TextColored(ImVec4{1.0f, 0.2f, 0.2f, 1.0f}, "Overlaps: %zu", m_broken_triangle_count);
    } else {
        ImGui::TextDisabled("Overlaps: 0");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "UV sanity check: triangles overlapping another triangle of the same region\n"
            "with positive area (unwrap defect); drawn filled red in the canvas."
        );
    }

    const std::shared_ptr<erhe::graphics::Texture>& texture =
        (m_texture_index == 1) ? baker->get_position_texture() :
        (m_texture_index == 2) ? baker->get_normal_texture  () :
        (m_texture_index == 3) ? baker->get_albedo_texture  () :
                                 baker->get_lightmap_texture();
    if (!texture || (layout.width == 0)) {
        ImGui::TextUnformatted("No lightmap atlas yet. Generate UVs and bake in the Lightmap window.");
        return;
    }

    refresh_overlay_cache();

    // Canvas child: no scrolling (wheel is zoom), clips the image and
    // overlays.
    const ImVec2 avail{
        std::max(64.0f, ImGui::GetContentRegionAvail().x),
        std::max(64.0f, ImGui::GetContentRegionAvail().y)
    };
    ImGui::BeginChild("canvas", avail, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 canvas_pos  = ImGui::GetCursorScreenPos();
    const ImVec2 canvas_size{
        std::max(1.0f, ImGui::GetContentRegionAvail().x),
        std::max(1.0f, ImGui::GetContentRegionAvail().y)
    };
    const glm::vec2 page_size{static_cast<float>(layout.width), static_cast<float>(layout.height)};
    const float fit_zoom = std::min(canvas_size.x / page_size.x, canvas_size.y / page_size.y);
    if (m_fit_requested || (m_last_page_width != layout.width) || (m_last_page_height != layout.height)) {
        m_fit_requested    = false;
        m_last_page_width  = layout.width;
        m_last_page_height = layout.height;
        m_zoom = fit_zoom;
        m_pan  = ImVec2{
            (canvas_size.x - page_size.x * m_zoom) * 0.5f,
            (canvas_size.y - page_size.y * m_zoom) * 0.5f
        };
    }

    // Frame selection (button / MCP): pan/zoom so every selected mesh's
    // atlas region is visible, with a 10% margin.
    if (m_frame_selection_request) {
        m_frame_selection_request = false;
        glm::vec2 frame_lo{ std::numeric_limits<float>::max()};
        glm::vec2 frame_hi{-std::numeric_limits<float>::max()};
        bool      frame_any{false};
        if (m_context.selection != nullptr) {
            for (const std::shared_ptr<erhe::Item_base>& item : m_context.selection->get_selected_items()) {
                const std::shared_ptr<erhe::scene::Mesh> selected_mesh = erhe::scene::get_mesh(item);
                if (!selected_mesh) {
                    continue;
                }
                for (const Region_overlay& overlay : m_overlays) {
                    const Lightmap_baker::Instance_region& region = layout.regions[overlay.region_index];
                    if (region.mesh.get() != selected_mesh.get()) {
                        continue;
                    }
                    frame_lo  = glm::min(frame_lo, overlay.rect_min);
                    frame_hi  = glm::max(frame_hi, overlay.rect_max);
                    frame_any = true;
                }
            }
        }
        if (frame_any) {
            const glm::vec2 extent = glm::max(frame_hi - frame_lo, glm::vec2{1.0e-4f});
            m_zoom = std::clamp(
                std::min(canvas_size.x / (extent.x * page_size.x), canvas_size.y / (extent.y * page_size.y)) * 0.9f,
                0.1f * fit_zoom,
                256.0f
            );
            const glm::vec2 center = (frame_lo + frame_hi) * 0.5f;
            m_pan = ImVec2{
                canvas_size.x * 0.5f - center.x * page_size.x * m_zoom,
                canvas_size.y * 0.5f - center.y * page_size.y * m_zoom
            };
        }
    }

    // screen = canvas_pos + m_pan + uv * page_size * m_zoom
    const auto screen_from_uv = [&](const glm::vec2& uv) -> ImVec2 {
        return ImVec2{
            canvas_pos.x + m_pan.x + uv.x * page_size.x * m_zoom,
            canvas_pos.y + m_pan.y + uv.y * page_size.y * m_zoom
        };
    };
    const auto uv_from_screen = [&](const ImVec2& screen) -> glm::vec2 {
        return glm::vec2{
            (screen.x - canvas_pos.x - m_pan.x) / (page_size.x * m_zoom),
            (screen.y - canvas_pos.y - m_pan.y) / (page_size.y * m_zoom)
        };
    };

    // Image (atlas row 0 = v 0 at the top; nearest magnification past 1:1
    // so texel boundaries are visible).
    ImGui::SetCursorScreenPos(ImVec2{canvas_pos.x + m_pan.x, canvas_pos.y + m_pan.y});
    const float tint = (m_texture_index == 0) ? m_exposure : 1.0f;
    m_context.imgui_renderer->image(
        erhe::imgui::Draw_texture_parameters{
            .texture_reference = texture,
            .width             = static_cast<int>(page_size.x * m_zoom),
            .height            = static_cast<int>(page_size.y * m_zoom),
            .uv0               = glm::vec2{0.0f, 0.0f},
            .uv1               = glm::vec2{1.0f, 1.0f},
            .tint_color        = glm::vec4{tint, tint, tint, 1.0f},
            .filter            = (m_zoom > 1.0f) ? erhe::graphics::Filter::nearest : erhe::graphics::Filter::linear,
            .debug_label       = erhe::utility::Debug_label{"lightmap texture view"}
        }
    );

    // Interaction surface covering the whole canvas (drawn over the image
    // item so hover and drags belong to the canvas, and left button stays
    // free for future picking).
    ImGui::SetCursorScreenPos(canvas_pos);
    ImGui::InvisibleButton(
        "canvas_input",
        canvas_size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight
    );
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    const bool canvas_hovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();

    // Zoom about the cursor: the atlas UV under the mouse stays fixed.
    if (canvas_hovered && (io.MouseWheel != 0.0f)) {
        const glm::vec2 uv_under_cursor = uv_from_screen(io.MousePos);
        m_zoom = std::clamp(m_zoom * std::pow(1.2f, io.MouseWheel), 0.1f * fit_zoom, 256.0f);
        m_pan = ImVec2{
            io.MousePos.x - canvas_pos.x - uv_under_cursor.x * page_size.x * m_zoom,
            io.MousePos.y - canvas_pos.y - uv_under_cursor.y * page_size.y * m_zoom
        };
    }
    // Pan with middle or right drag started on the canvas.
    if (
        ImGui::IsItemActive() &&
        (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f))
    ) {
        m_pan.x += io.MouseDelta.x;
        m_pan.y += io.MouseDelta.y;
    }
    if (canvas_hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        m_fit_requested = true;
    }

    // Hover hit test, atlas mouse first: mouse -> atlas UV -> region rect
    // -> facet fan triangles (the same triangulation the G-buffer raster
    // uses). A whole hovered facet is highlighted (one fan triangle from
    // the atlas hit, all of the facet's from a viewport hit).
    const Region_overlay*                 hovered_overlay = nullptr;
    std::vector<const Overlay_triangle*>  hovered_triangles;
    std::uint32_t                         hovered_facet{0};
    bool                                  show_tooltip{false};
    if (canvas_hovered) {
        const glm::vec2 uv = uv_from_screen(io.MousePos);
        for (const Region_overlay& overlay : m_overlays) {
            if ((uv.x < overlay.rect_min.x) || (uv.x > overlay.rect_max.x) ||
                (uv.y < overlay.rect_min.y) || (uv.y > overlay.rect_max.y)) {
                continue;
            }
            for (const Overlay_triangle& triangle : overlay.triangles) {
                if (point_in_triangle(uv, triangle.a, triangle.b, triangle.c)) {
                    hovered_overlay = &overlay;
                    hovered_facet   = triangle.facet;
                    hovered_triangles.push_back(&triangle);
                    show_tooltip    = true;
                    break;
                }
            }
            if (hovered_overlay != nullptr) {
                break;
            }
        }
    }
    // Fallback: the 3D viewport hover (content slot of the scene view
    // under the pointer) highlights the matching chart / facet.
    if ((hovered_overlay == nullptr) && (m_hover_scene_view != nullptr)) {
        const Hover_entry& entry = m_hover_scene_view->get_hover(Hover_entry::content_slot);
        const std::shared_ptr<erhe::scene::Mesh> hover_mesh = entry.valid ? entry.scene_mesh_weak.lock() : std::shared_ptr<erhe::scene::Mesh>{};
        if (hover_mesh) {
            for (const Region_overlay& overlay : m_overlays) {
                const Lightmap_baker::Instance_region& region = layout.regions[overlay.region_index];
                if ((region.mesh.get() != hover_mesh.get()) || (region.primitive_index != entry.scene_mesh_primitive_index)) {
                    continue;
                }
                hovered_overlay = &overlay;
                if (entry.facet != GEO::NO_INDEX) {
                    hovered_facet = static_cast<std::uint32_t>(entry.facet);
                    for (const Overlay_triangle& triangle : overlay.triangles) {
                        if (triangle.facet == hovered_facet) {
                            hovered_triangles.push_back(&triangle);
                        }
                    }
                }
                break;
            }
        }
    }

    ImDrawList* const draw_list = ImGui::GetWindowDrawList();
    // Texel grid: once a texel is larger than 2x2 pixels, draw texel
    // boundary lines over the visible part of the atlas (alpha fades in
    // with zoom) so chart sizes can be judged in texels. Drawn under the
    // chart overlays; only visible lines are emitted.
    if (m_zoom > 2.0f) {
        const float grid_fade  = std::clamp((m_zoom - 2.0f) / 6.0f, 0.0f, 1.0f);
        const ImU32 grid_color = IM_COL32(140, 140, 140, static_cast<int>(24.0f + 72.0f * grid_fade));
        const glm::vec2 uv_view_min = uv_from_screen(canvas_pos);
        const glm::vec2 uv_view_max = uv_from_screen(ImVec2{canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y});
        const int texel_x0 = std::clamp(static_cast<int>(std::floor(uv_view_min.x * page_size.x)), 0, layout.width);
        const int texel_x1 = std::clamp(static_cast<int>(std::ceil (uv_view_max.x * page_size.x)), 0, layout.width);
        const int texel_y0 = std::clamp(static_cast<int>(std::floor(uv_view_min.y * page_size.y)), 0, layout.height);
        const int texel_y1 = std::clamp(static_cast<int>(std::ceil (uv_view_max.y * page_size.y)), 0, layout.height);
        const float grid_screen_x0 = screen_from_uv(glm::vec2{static_cast<float>(texel_x0) / page_size.x, 0.0f}).x;
        const float grid_screen_x1 = screen_from_uv(glm::vec2{static_cast<float>(texel_x1) / page_size.x, 0.0f}).x;
        const float grid_screen_y0 = screen_from_uv(glm::vec2{0.0f, static_cast<float>(texel_y0) / page_size.y}).y;
        const float grid_screen_y1 = screen_from_uv(glm::vec2{0.0f, static_cast<float>(texel_y1) / page_size.y}).y;
        for (int x = texel_x0; x <= texel_x1; ++x) {
            const float sx = screen_from_uv(glm::vec2{static_cast<float>(x) / page_size.x, 0.0f}).x;
            draw_list->AddLine(ImVec2{sx, grid_screen_y0}, ImVec2{sx, grid_screen_y1}, grid_color, 1.0f);
        }
        for (int y = texel_y0; y <= texel_y1; ++y) {
            const float sy = screen_from_uv(glm::vec2{0.0f, static_cast<float>(y) / page_size.y}).y;
            draw_list->AddLine(ImVec2{grid_screen_x0, sy}, ImVec2{grid_screen_x1, sy}, grid_color, 1.0f);
        }
    }
    // Broken (overlapping) triangles: always-on sanity check, filled red
    // at 50% alpha under the edge lines.
    {
        const ImU32 broken_color = IM_COL32(255, 0, 0, 128);
        for (const Region_overlay& overlay : m_overlays) {
            for (const std::uint32_t index : overlay.broken_triangles) {
                const Overlay_triangle& triangle = overlay.triangles[index];
                draw_list->AddTriangleFilled(
                    screen_from_uv(triangle.a),
                    screen_from_uv(triangle.b),
                    screen_from_uv(triangle.c),
                    broken_color
                );
            }
        }
    }
    if (m_show_edges) {
        const ImU32 edge_color = IM_COL32(255, 0, 0, 128); // red
        for (const Region_overlay& overlay : m_overlays) {
            for (const std::array<glm::vec2, 2>& edge : overlay.edges) {
                draw_list->AddLine(screen_from_uv(edge[0]), screen_from_uv(edge[1]), edge_color, 1.0f);
            }
        }
    }
    if (m_highlight_mesh && (hovered_overlay != nullptr)) {
        const ImU32 mesh_color = IM_COL32(255, 105, 180, 255); // hot pink
        for (const std::array<glm::vec2, 2>& edge : hovered_overlay->edges) {
            draw_list->AddLine(screen_from_uv(edge[0]), screen_from_uv(edge[1]), mesh_color, 1.5f);
        }
    }
    if (m_highlight_facet) {
        const ImU32 facet_color = IM_COL32(255, 165, 0, 255); // orange
        for (const Overlay_triangle* triangle : hovered_triangles) {
            const ImVec2 a = screen_from_uv(triangle->a);
            const ImVec2 b = screen_from_uv(triangle->b);
            const ImVec2 c = screen_from_uv(triangle->c);
            draw_list->AddLine(a, b, facet_color, 2.0f);
            draw_list->AddLine(b, c, facet_color, 2.0f);
            draw_list->AddLine(c, a, facet_color, 2.0f);
        }
    }

    if (show_tooltip && (hovered_overlay != nullptr)) {
        const Lightmap_baker::Instance_region& region = layout.regions[hovered_overlay->region_index];
        const glm::vec2 uv    = uv_from_screen(io.MousePos);
        const glm::vec2 texel = uv * page_size;
        ImGui::SetTooltip(
            "%s[%zu]\nfacet %u\ntexel (%.0f, %.0f)",
            region.mesh ? region.mesh->get_name().c_str() : "<gone>",
            region.primitive_index,
            hovered_facet,
            texel.x, texel.y
        );
    }

    ImGui::EndChild();
}

} // namespace editor
