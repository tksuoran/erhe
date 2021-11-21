#pragma once

#include "erhe/graphics/opengl_state_tracker.hpp"

#include <imgui.h>

bool ImGui_ImplErhe_Init(erhe::graphics::OpenGL_state_tracker* pipeline_state_tracker);
void ImGui_ImplErhe_Shutdown();
void ImGui_ImplErhe_NewFrame();
void ImGui_ImplErhe_RenderDrawData(const ImDrawData* draw_data);
