#include "windows/composer_window.hpp"

#include "app_context.hpp"
#include "app_rendering.hpp"
#include "erhe_imgui/imgui_host.hpp"
#include "erhe_imgui/imgui_windows.hpp"

namespace editor {

Composer_window::Composer_window(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, App_context& app_context)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Composer", "composer"}
    , m_context                {app_context}
{
}

void Composer_window::imgui()
{
    m_context.app_rendering->imgui();
}

} // namespace editor