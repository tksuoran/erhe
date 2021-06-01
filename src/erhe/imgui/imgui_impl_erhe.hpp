#pragma once

#include "imgui.h"

#include "erhe/graphics/opengl_state_tracker.hpp"

bool ImGui_ImplErhe_Init(std::shared_ptr<erhe::graphics::OpenGL_state_tracker> pipeline_state_tracker);
void ImGui_ImplErhe_Shutdown();
void ImGui_ImplErhe_NewFrame();
void ImGui_ImplErhe_RenderDrawData(ImDrawData* draw_data);
