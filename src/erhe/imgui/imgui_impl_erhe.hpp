#pragma once

namespace erhe::graphics {
    class OpenGL_state_tracker;
}

struct ImDrawData;

bool ImGui_ImplErhe_Init(erhe::graphics::OpenGL_state_tracker* pipeline_state_tracker);
void ImGui_ImplErhe_Shutdown();
void ImGui_ImplErhe_NewFrame();
void ImGui_ImplErhe_RenderDrawData(const ImDrawData* draw_data);
