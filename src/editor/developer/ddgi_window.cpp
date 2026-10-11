#include "developer/ddgi_window.hpp"

#include "app_context.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/indirect_diffuse_source.hpp"
#include "renderers/ddgi_renderer.hpp"
#include "renderers/indirect_diffuse.hpp"
#include "windows/config_ui.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"

#include <imgui/imgui.h>

#include <algorithm>

namespace editor {

Ddgi_window::Ddgi_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context
)
    : Imgui_window{imgui_renderer, imgui_windows, "DDGI", "ddgi", true}
    , m_context   {app_context}
{
    set_min_size(420.0f, 320.0f);
}

void Ddgi_window::imgui()
{
    Ddgi_renderer* renderer = m_context.ddgi_renderer;
    if (renderer == nullptr) {
        ImGui::TextUnformatted("DDGI renderer is not available.");
        return;
    }
    if (!renderer->is_supported()) {
        ImGui::TextUnformatted("DDGI needs GPU ray query, which this device / backend does not support.");
        return;
    }
    if (m_context.editor_settings == nullptr) {
        return;
    }

    // The full knob set is reflection-rendered in the Settings window's DDGI
    // section; only the source selection is repeated here so the
    // diagnostics and the switch sit together.
    Indirect_diffuse_source source = m_context.editor_settings->indirect_diffuse_source;
    if (imgui_enum_combo("Indirect Diffuse", source)) {
        set_indirect_diffuse_source(m_context, source);
    }
    if (!renderer->is_selected()) {
        ImGui::TextUnformatted("Select DDGI as the indirect diffuse source to fit a probe volume to the scene content.");
        return;
    }

    // The grid and cost as of the last rendered frame's tick.
    const Ddgi_renderer::Rendered_state* const state = renderer->get_rendered_state(m_context.last_rendered_frame_index);
    if (state == nullptr) {
        ImGui::TextUnformatted("No frame rendered with DDGI yet.");
        return;
    }
    const Ddgi_renderer::Grid& grid = state->grid;
    if (!grid.is_valid()) {
        ImGui::TextUnformatted("No visible content to fit a probe volume to.");
        return;
    }

    ImGui::Text("Probes:  %d x %d x %d = %d", grid.counts.x, grid.counts.y, grid.counts.z, grid.get_probe_count());
    ImGui::Text("Spacing: %.2f %.2f %.2f m", grid.spacing.x, grid.spacing.y, grid.spacing.z);
    ImGui::Text("Origin:  %.2f %.2f %.2f",   grid.origin.x,  grid.origin.y,  grid.origin.z );
    ImGui::Text("Rays per probe: %d",        state->rays_per_probe);
    ImGui::Text("Octahedral: %d irradiance / %d distance texels", state->irradiance_texels, state->distance_texels);
    ImGui::Text("Probe textures: %.1f MB",   static_cast<double>(state->texture_byte_count) / (1024.0 * 1024.0));

    // GPU cost of the probe update (doc/editor/ddgi.md "Performance").
    const Ddgi_renderer::Stats& stats = state->stats;
    if (ImGui::CollapsingHeader("GPU time", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("ddgi_gpu_time", 3, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Pass");
            ImGui::TableSetupColumn("Last ms");
            ImGui::TableSetupColumn("Avg ms");
            ImGui::TableHeadersRow();
            const auto row = [](const char* label, const Ddgi_renderer::Pass_time& pass_time) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(label);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", pass_time.last_ms);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", pass_time.average_ms);
            };
            for (std::size_t i = 0; i < c_ddgi_pass_count; ++i) {
                row(c_str(static_cast<Ddgi_pass>(i)), stats.passes[i]);
            }
            row("total", stats.total);
            ImGui::EndTable();
        }
        ImGui::Text("Probes per update: %d",       state->probes_per_update);
        ImGui::Text("Rays per update: %lld",       static_cast<long long>(stats.rays_per_update));
        ImGui::Text("Cost: %.3f ms per million rays", stats.ms_per_million_rays);
        ImGui::Text("Full refresh: %d updates, %.2f ms", stats.updates_per_full_refresh, stats.full_refresh_ms);
        ImGui::Text("Updates: %llu",               static_cast<unsigned long long>(stats.update_count));
    }

    // The atlases are created and replaced by the render phase's ticks: the
    // previews take their sizes from the last rendered frame's state and draw
    // through references the render thread resolves.
    const auto preview = [this, renderer, state](const char* label, const Ddgi_renderer::Atlas atlas) {
        const glm::ivec2 size = state->atlas_sizes[static_cast<std::size_t>(atlas)];
        if ((size.x < 1) || (size.y < 1)) {
            return;
        }
        // Default-open: the previews are the whole point of this window, and
        // a headless screenshot cannot expand a collapsed header.
        if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
            return;
        }
        const float avail_width    = std::max(64.0f, ImGui::GetContentRegionAvail().x);
        const float aspect         = static_cast<float>(size.y) / static_cast<float>(size.x);
        const int   display_width  = static_cast<int>(avail_width);
        const int   display_height = std::max(1, static_cast<int>(avail_width * aspect));
        m_context.imgui_renderer->image(
            erhe::imgui::Draw_texture_parameters{
                .texture_reference = renderer->get_texture_reference(atlas),
                .width             = display_width,
                .height            = display_height,
                .uv0               = glm::vec2{0.0f, 0.0f},
                .uv1               = glm::vec2{1.0f, 1.0f},
                // The atlases are texel grids; magnify without smoothing so
                // individual probe tiles stay distinguishable.
                .filter            = erhe::graphics::Filter::nearest,
                .debug_label       = erhe::utility::Debug_label{"ddgi atlas"}
            }
        );
    };
    preview("Irradiance atlas", Ddgi_renderer::Atlas::irradiance);
    preview("Distance atlas",   Ddgi_renderer::Atlas::distance  );
    preview("Probe data",       Ddgi_renderer::Atlas::probe_data);
    preview("Ray data",         Ddgi_renderer::Atlas::ray_data  );
}

} // namespace editor
