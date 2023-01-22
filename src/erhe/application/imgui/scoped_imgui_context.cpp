#include "erhe/application/imgui/scoped_imgui_context.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"

#include <imgui/imgui.h>

namespace erhe::application
{

Scoped_imgui_context::Scoped_imgui_context(
    Imgui_viewport& imgui_viewport
)
    : m_lock{g_imgui_windows->get_mutex()}
{
    g_imgui_windows->make_current(&imgui_viewport);
}

Scoped_imgui_context::~Scoped_imgui_context()
{
    g_imgui_windows->make_current(nullptr);
}

}  // namespace editor
