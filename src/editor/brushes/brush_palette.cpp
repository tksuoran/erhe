
#include "brushes/brush_palette.hpp"
#include "brushes/brushes.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

Brush_palette::Brush_palette()
    : erhe::components::Component    {c_type_name}
    , erhe::application::Imgui_window{c_title}
{
}

Brush_palette::~Brush_palette() noexcept
{
}

void Brush_palette::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
}

void Brush_palette::initialize_component()
{
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
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
