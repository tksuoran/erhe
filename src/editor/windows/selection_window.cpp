#include "windows/selection_window.hpp"

#include "editor_context.hpp"
#include "graphics/icon_set.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_item/item.hpp"
#include "erhe_imgui/imgui_viewport.hpp"
#include "erhe_imgui/imgui_windows.hpp"

namespace editor
{

Selection_window::Selection_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Selection", "selection"}
    , m_context                {editor_context}
{
}

void Selection_window::imgui()
{
    const float scale = get_scale_value();

    const auto& items = m_context.selection->get_selection();
    for (const auto& item : items) {
        m_context.icon_set->item_icon(item, scale);
        ImGui::TextUnformatted(item->get_name().c_str());
    }
}

}