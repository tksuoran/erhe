#include "developer/ddgi_window.hpp"

#include "app_context.hpp"
#include "config/generated/ddgi_config.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "renderers/ddgi_renderer.hpp"

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
    // section; only the master switch is repeated here so the diagnostics
    // and the toggle sit together.
    Ddgi_config& config = m_context.editor_settings->ddgi;
    ImGui::Checkbox("Enabled", &config.enabled);
    if (!config.enabled) {
        ImGui::TextUnformatted("Enable to fit a probe volume to the scene content.");
        return;
    }

    const Ddgi_renderer::Grid& grid = renderer->get_grid();
    if (!grid.is_valid()) {
        ImGui::TextUnformatted("No visible content to fit a probe volume to.");
        return;
    }

    ImGui::Text("Probes:  %d x %d x %d = %d", grid.counts.x, grid.counts.y, grid.counts.z, grid.get_probe_count());
    ImGui::Text("Spacing: %.2f %.2f %.2f m", grid.spacing.x, grid.spacing.y, grid.spacing.z);
    ImGui::Text("Origin:  %.2f %.2f %.2f",   grid.origin.x,  grid.origin.y,  grid.origin.z );
    ImGui::Text("Rays per probe: %d",        renderer->get_rays_per_probe());
    ImGui::Text("Octahedral: %d irradiance / %d distance texels", renderer->get_irradiance_texels(), renderer->get_distance_texels());
    ImGui::Text("Probe textures: %.1f MB",   static_cast<double>(renderer->get_texture_byte_count()) / (1024.0 * 1024.0));

    const auto preview = [this](const char* label, const std::shared_ptr<erhe::graphics::Texture>& texture) {
        if (!texture) {
            return;
        }
        if (!ImGui::CollapsingHeader(label)) {
            return;
        }
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
                // The atlases are texel grids; magnify without smoothing so
                // individual probe tiles stay distinguishable.
                .filter            = erhe::graphics::Filter::nearest,
                .debug_label       = erhe::utility::Debug_label{"ddgi atlas"}
            }
        );
    };
    preview("Irradiance atlas", renderer->get_irradiance_texture());
    preview("Distance atlas",   renderer->get_distance_texture  ());
    preview("Probe data",       renderer->get_probe_data_texture());
    preview("Ray data",         renderer->get_ray_data_texture  ());
}

} // namespace editor
