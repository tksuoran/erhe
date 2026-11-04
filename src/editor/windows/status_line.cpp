#include "windows/status_line.hpp"

#include "app_context.hpp"
#include "scene/scene_save_service.hpp"
#include "time.hpp"

#include <imgui/imgui.h>

namespace editor {

void Status_line::imgui(App_context& context)
{
    const float frame_ms          = (context.time != nullptr) ? context.time->get_frame_time_average_ms() : 0.0f;
    const int   running_async_ops = context.running_async_ops.load();
    const int   pending_async_ops = context.pending_async_ops.load();
    if ((running_async_ops > 0) && (pending_async_ops > 0)) {
        ImGui::Text("%.2f ms %d running operations, %d queued operations", frame_ms, running_async_ops, pending_async_ops);
    } else if (running_async_ops > 0) {
        ImGui::Text("%.2f ms %d running operations", frame_ms, running_async_ops);
    } else if (pending_async_ops > 0) {
        ImGui::Text("%.2f ms %d queued operations", frame_ms, pending_async_ops);
    } else {
        ImGui::Text("%.2f ms", frame_ms);
    }
    ImGui::SameLine();

    // A running or waiting scene save (scene_save_service.hpp).
    if ((context.scene_save_service != nullptr) && context.scene_save_service->imgui_status()) {
        ImGui::SameLine();
    }

    // Geogram operation progress (Remesh / atlas / ...); false when idle.
    if (erhe::geometry::get_geogram_progress(m_geogram_progress)) {
        ImGui::Text("%s %u%%", m_geogram_progress.task_name.c_str(), m_geogram_progress.percent);
        ImGui::SameLine();
        ImGui::ProgressBar(static_cast<float>(m_geogram_progress.percent) / 100.0f, ImVec2{160.0f, 0.0f});
        ImGui::SameLine();
    }
}

}
