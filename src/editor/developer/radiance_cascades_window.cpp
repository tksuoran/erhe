#include "developer/radiance_cascades_window.hpp"

#include "app_context.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/indirect_diffuse_source.hpp"
#include "renderers/indirect_diffuse.hpp"
#include "renderers/radiance_cascades_renderer.hpp"
#include "windows/config_ui.hpp"

#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"

#include <imgui/imgui.h>

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
    if (!renderer->has_field()) {
        ImGui::TextUnformatted("Layout only: no trace / merge / reduce yet; the forward pass uses the flat ambient term.");
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
    ImGui::TextUnformatted("Memory: raw + merged RGBA16F atlas per cascade.");
}

} // namespace editor
