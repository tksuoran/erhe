#include "windows/composer_window.hpp"

#include "editor_context.hpp"
#include "editor_rendering.hpp"
//#include "graphics/icon_set.hpp"

//#include "erhe_item/item.hpp"
#include "erhe_imgui/imgui_host.hpp"
#include "erhe_imgui/imgui_windows.hpp"

namespace editor {

Composer_window::Composer_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Composer", "composer"}
    , m_context                {editor_context}
{
}

void Composer_window::imgui()
{
    m_context.editor_rendering->imgui();
}

} // namespace editor