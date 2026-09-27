#include "developer/radiance_cascades_window.hpp"

#include "app_context.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/indirect_diffuse_source.hpp"
#include "config/generated/radiance_cascades_config.hpp"
#include "config/generated/radiance_cascades_merge_mode.hpp"
#include "renderers/indirect_diffuse.hpp"
#include "renderers/radiance_cascades_renderer.hpp"
#include "windows/config_ui.hpp"

#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"

#include <imgui/imgui.h>

#include <algorithm>
#include <string>

namespace editor {

namespace {

[[nodiscard]] auto to_mib(const double bytes) -> double
{
    return bytes / (1024.0 * 1024.0);
}

} // anonymous namespace

Radiance_cascades_window::Radiance_cascades_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context
)
    : Imgui_window{imgui_renderer, imgui_windows, "Radiance Cascades", "radiance_cascades", true}
    , m_context   {app_context}
{
    set_min_size(520.0f, 240.0f);
}

void Radiance_cascades_window::imgui()
{
    Radiance_cascades_renderer* renderer = m_context.radiance_cascades_renderer;
    if (renderer == nullptr) {
        ImGui::TextUnformatted("Radiance cascades renderer is not available.");
        return;
    }
    if (!renderer->is_supported()) {
        ImGui::TextUnformatted("Radiance cascades need GPU ray query, which this device / backend does not support.");
        return;
    }
    if (m_context.editor_settings == nullptr) {
        return;
    }

    Indirect_diffuse_source source = m_context.editor_settings->indirect_diffuse_source;
    if (imgui_enum_combo("Indirect Diffuse", source)) {
        set_indirect_diffuse_source(m_context, source);
    }
    if (!renderer->is_selected()) {
        ImGui::TextUnformatted("Select radiance cascades as the indirect diffuse source to fit the cascades to the scene content.");
        return;
    }

    const Radiance_cascades_layout& layout = renderer->get_layout();
    if (!layout.is_valid()) {
        ImGui::TextUnformatted("No visible content to fit the cascades to.");
        return;
    }
    if (renderer->has_field()) {
        // The field the forward pass samples (doc/editor/radiance_cascades.md
        // "Reduce"): cascade 0's grid in the DDGI atlas format.
        const erhe::scene_renderer::Ddgi_parameters field = renderer->get_forward_parameters();
        ImGui::Text(
            "Probe field: %d x %d x %d probes, irradiance %d / distance %d texels, %d tiles per row",
            field.grid_counts.x, field.grid_counts.y, field.grid_counts.z,
            field.irradiance_texels, field.distance_texels, field.tiles_per_row
        );
    } else {
        ImGui::TextUnformatted("No probe field yet; the forward pass uses the flat ambient term.");
    }

    const Radiance_cascade& cascade0 = layout.cascades[0];
    ImGui::Text("Cascade 0 origin: %.3f %.3f %.3f", cascade0.grid.origin.x, cascade0.grid.origin.y, cascade0.grid.origin.z);
    ImGui::Text("r0: %.3f m", layout.r0);

    const ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("radiance_cascades_layout", 8, flags)) {
        ImGui::TableSetupColumn("Cascade");
        ImGui::TableSetupColumn("Probes");
        ImGui::TableSetupColumn("Count");
        ImGui::TableSetupColumn("Spacing (m)");
        ImGui::TableSetupColumn("Tile");
        ImGui::TableSetupColumn("Interval (m)");
        ImGui::TableSetupColumn("Texels");
        ImGui::TableSetupColumn("Atlas / MB");
        ImGui::TableHeadersRow();
        for (int i = 0; i < layout.cascade_count; ++i) {
            const Radiance_cascade& cascade = layout.cascades[static_cast<std::size_t>(i)];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%d", i);
            ImGui::TableSetColumnIndex(1); ImGui::Text("%d x %d x %d", cascade.grid.counts.x, cascade.grid.counts.y, cascade.grid.counts.z);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%d", cascade.get_probe_count());
            ImGui::TableSetColumnIndex(3); ImGui::Text("%.3f %.3f %.3f", cascade.grid.spacing.x, cascade.grid.spacing.y, cascade.grid.spacing.z);
            ImGui::TableSetColumnIndex(4); ImGui::Text("%d x %d", cascade.tile_texels, cascade.tile_texels);
            ImGui::TableSetColumnIndex(5); ImGui::Text("%.3f - %.3f", cascade.interval_start, cascade.interval_end);
            ImGui::TableSetColumnIndex(6); ImGui::Text("%lld", static_cast<long long>(cascade.get_texel_count()));
            ImGui::TableSetColumnIndex(7); ImGui::Text(
                "%d x %d / %.2f",
                cascade.get_atlas_width(), cascade.get_atlas_height(),
                to_mib(static_cast<double>(renderer->get_cascade_texture_byte_count(i)))
            );
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("total");
        ImGui::TableSetColumnIndex(2); ImGui::Text("%lld", static_cast<long long>(layout.get_total_probes()));
        ImGui::TableSetColumnIndex(6); ImGui::Text("%lld", static_cast<long long>(layout.get_total_texels()));
        ImGui::TableSetColumnIndex(7); ImGui::Text("%.2f", to_mib(static_cast<double>(renderer->get_texture_byte_count())));
        ImGui::EndTable();
    }
    ImGui::TextUnformatted("Memory: raw + merged RGBA16F atlas per cascade (plus the 4 x 2 neighbour atlas in the per_neighbour_trace merge mode), plus the cascade 0 R32F distance texture; the total includes the probe field atlases.");

    // GPU cost of the trace, merge and reduce
    // (doc/editor/radiance_cascades.md "Trace", "Merge", "Reduce").
    const Radiance_cascades_renderer::Stats stats = renderer->get_stats();
    if (ImGui::CollapsingHeader("GPU time", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("radiance_cascades_gpu_time", 3, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Pass");
            ImGui::TableSetupColumn("Last ms");
            ImGui::TableSetupColumn("Avg ms");
            ImGui::TableHeadersRow();
            const auto row = [](const char* label, const Radiance_cascades_renderer::Pass_time& time) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(label);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", time.last_ms);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", time.average_ms);
            };
            row("trace", stats.trace);
            row("neighbour trace", stats.neighbour_trace);
            row("merge", stats.merge);
            row("reduce", stats.reduce);
            row("total", stats.total);
            ImGui::EndTable();
        }
        ImGui::Text(
            "Visibility (on layout / geometry change): %.3f ms, %llu runs",
            stats.visibility_last_ms,
            static_cast<unsigned long long>(stats.visibility_update_count)
        );
        ImGui::Text(
            "Texels per update: %lld, rays per update: %lld (connecting segments %lld)",
            static_cast<long long>(stats.texels_per_update),
            static_cast<long long>(stats.rays_per_update),
            static_cast<long long>(stats.neighbour_rays_per_update)
        );
        ImGui::Text("Cost: %.3f ms per million rays", stats.ms_per_million_rays);
        ImGui::Text("Full refresh: %lld updates, %.2f ms", static_cast<long long>(stats.updates_per_full_refresh), stats.full_refresh_ms);
        ImGui::Text("Updates: %llu, full sweeps: %llu", static_cast<unsigned long long>(stats.update_count), static_cast<unsigned long long>(stats.completed_sweeps));
    }

    // Merge mode (doc/editor/radiance_cascades.md "Merge"): the combo is
    // the change site; it stores the setting and tells the renderer.
    {
        Radiance_cascades_merge_mode mode = m_context.editor_settings->radiance_cascades.merge_mode;
        if (imgui_enum_combo("Merge mode", mode)) {
            m_context.editor_settings->radiance_cascades.merge_mode = mode;
            renderer->set_merge_mode(mode);
        }
    }

    // Debug cascade mask (doc/editor/radiance_cascades.md "Merge"): a
    // masked cascade keeps its transparency but contributes no radiance, so
    // the merged atlases show the unmasked interval bands only. The
    // renderer reads the setting on its next tick.
    if (ImGui::CollapsingHeader("Debug cascade mask")) {
        int& mask = m_context.editor_settings->radiance_cascades.debug_cascade_mask;
        ImGui::TextUnformatted("Checked bands contribute radiance to the merge.");
        for (int i = 0; i <= layout.cascade_count; ++i) {
            const bool is_sky = (i == layout.cascade_count);
            const int  bit    = is_sky ? Radiance_cascades_renderer::c_sky_mask_bit : i;
            bool       shown  = ((mask & (1 << bit)) == 0);
            const std::string label = is_sky ? std::string{"Sky"} : ("Cascade " + std::to_string(i));
            if (i > 0) {
                ImGui::SameLine();
            }
            if (ImGui::Checkbox(label.c_str(), &shown)) {
                mask = shown ? (mask & ~(1 << bit)) : (mask | (1 << bit));
            }
        }
        if ((mask != 0) && ImGui::Button("Show all")) {
            mask = 0;
        }
    }

    // Atlas preview. The atlases carry beta in alpha, which the image
    // widget would use as opacity, so the renderer writes an opaque copy of
    // the chosen cascade, atlas and channel (rc_preview.comp). Requested
    // each frame the preview is shown; the copy lags the request by one
    // frame.
    if (ImGui::CollapsingHeader("Atlas preview", ImGuiTreeNodeFlags_DefaultOpen)) {
        m_preview_cascade = std::clamp(m_preview_cascade, 0, layout.cascade_count - 1);
        ImGui::SliderInt("Cascade", &m_preview_cascade, 0, layout.cascade_count - 1);
        int preview_source = static_cast<int>(m_preview_source);
        const char* const source_names[] = { "Raw", "Merged" };
        if (ImGui::Combo("Atlas", &preview_source, source_names, IM_ARRAYSIZE(source_names))) {
            m_preview_source = static_cast<Rc_preview_source>(preview_source);
        }
        int channel = static_cast<int>(m_preview_channel);
        const char* const channel_names[] = { "Radiance", "Beta", "Distance (cascade 0)" };
        if (ImGui::Combo("Channel", &channel, channel_names, IM_ARRAYSIZE(channel_names))) {
            m_preview_channel = static_cast<Rc_preview_channel>(channel);
        }
        if (m_preview_channel == Rc_preview_channel::radiance) {
            ImGui::SliderFloat("Radiance scale", &m_preview_radiance_scale, 0.1f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        }
        const int preview_cascade = (m_preview_channel == Rc_preview_channel::distance) ? 0 : m_preview_cascade;
        renderer->request_preview(preview_cascade, m_preview_source, m_preview_channel, m_preview_radiance_scale);

        const std::shared_ptr<erhe::graphics::Texture>& texture = renderer->get_preview_texture();
        if (texture) {
            const float avail_width    = std::max(64.0f, ImGui::GetContentRegionAvail().x);
            const float aspect         = static_cast<float>(texture->get_height()) / static_cast<float>(texture->get_width());
            const int   display_width  = static_cast<int>(avail_width);
            const int   display_height = std::max(1, static_cast<int>(avail_width * aspect));
            m_context.imgui_renderer->image(
                erhe::imgui::Draw_texture_parameters{
                    .texture_reference = texture,
                    .width             = display_width,
                    .height            = display_height,
                    .uv0               = glm::vec2{0.0f, 0.0f},
                    .uv1               = glm::vec2{1.0f, 1.0f},
                    // Texel grids: magnify without smoothing so individual
                    // directions stay distinguishable.
                    .filter            = erhe::graphics::Filter::nearest,
                    .debug_label       = erhe::utility::Debug_label{"radiance cascades atlas"}
                }
            );
        }
    }
}

} // namespace editor
