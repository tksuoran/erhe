
#include "brushes/brush_tool.hpp"

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_profile/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

Brush_palette::Brush_palette()
    : erhe::imgui::Imgui_window{c_title}
{
}

Brush_palette::~Brush_palette() noexcept
{
}

void Brush_palette::declare_required_components()
{
    require<erhe::imgui::Imgui_windows>();
}

void Brush_palette::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    get<erhe::imgui::Imgui_windows>()->register_imgui_window(this);
}

void Brush_palette::post_initialize()
{
    m_brushes = get<Brushes>();
}

void Brush_palette::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    m_brushes->brush_palette(m_selected_brush_index);
#endif
}

} // namespace editor
