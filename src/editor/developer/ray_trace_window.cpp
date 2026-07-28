#include "developer/ray_trace_window.hpp"

#include "app_context.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/ray_trace_config.hpp"
#include "renderers/ray_trace_renderer.hpp"

#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"

#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>

namespace editor {

Ray_trace_window::Ray_trace_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context
)
    : Imgui_window{imgui_renderer, imgui_windows, "Ray Trace", "ray_trace", true}
    , m_context   {app_context}
{
    // Large enough that the 16:9 output image is readable by default.
    set_min_size(512.0f, 360.0f);
}

void Ray_trace_window::imgui()
{
    Ray_trace_renderer* renderer = m_context.ray_trace_renderer;
    if (renderer == nullptr) {
        ImGui::TextUnformatted("Ray trace renderer is not available.");
        return;
    }
    if (!renderer->is_supported()) {
        ImGui::TextUnformatted("GPU ray tracing (ray query + position fetch) is not supported by this device / backend.");
        return;
    }

    bool enabled = renderer->is_enabled();
    if (ImGui::Checkbox("Enable", &enabled)) {
        renderer->set_enabled(enabled);
    }
    if (!enabled) {
        ImGui::TextUnformatted("Enable to trace primary rays from the viewport camera.");
        return;
    }

    ImGui::Text("Instances: %zu", renderer->get_instance_count());

    // Ray_trace_config lives in the editor settings (autosaved to
    // editor_settings.json by Editor_settings_store); the renderer holds a
    // live reference and picks changes up on its next render.
    // Integer downscale factors magnify with nearest (crisp NxN pixel
    // blocks); fractional factors use linear to hide the uneven footprint.
    erhe::graphics::Filter magnification_filter = erhe::graphics::Filter::nearest;
    if (m_context.editor_settings != nullptr) {
        Ray_trace_config& config = m_context.editor_settings->ray_trace;
        ImGui::SliderFloat("Downscale",   &config.downscale,   1.0f, 8.0f, "%.2f");
        ImGui::SliderInt  ("Max Rays",    &config.max_rays,    1,    256);
        // 12 = the shader's compile-time branching-stack bound (the
        // renderer clamps to it regardless).
        ImGui::SliderInt  ("Max Bounces", &config.max_bounces, 0,    12);

        const float downscale         = std::clamp(config.downscale, 1.0f, 8.0f);
        const bool  integer_downscale = std::abs(downscale - std::round(downscale)) < 1e-3f;
        magnification_filter = integer_downscale ? erhe::graphics::Filter::nearest : erhe::graphics::Filter::linear;
    }

    const std::shared_ptr<erhe::graphics::Texture> texture = renderer->get_output_texture();
    if (!texture) {
        return;
    }
    // Fit the image to the window (keep aspect) so the whole output is
    // visible regardless of window size.
    const float avail_width    = std::max(64.0f, ImGui::GetContentRegionAvail().x);
    const float aspect         = static_cast<float>(texture->get_height()) / static_cast<float>(texture->get_width());
    const int   display_width  = static_cast<int>(avail_width);
    const int   display_height = static_cast<int>(avail_width * aspect);
    m_context.imgui_renderer->image(
        erhe::imgui::Draw_texture_parameters{
            .texture_reference = texture,
            .width             = display_width,
            .height            = display_height,
            // The compute output's row 0 is the top row; sample it unflipped.
            .uv0               = glm::vec2{0.0f, 0.0f},
            .uv1               = glm::vec2{1.0f, 1.0f},
            .filter            = magnification_filter,
            .debug_label       = erhe::utility::Debug_label{"ray_trace output"}
        }
    );
}

} // namespace editor
