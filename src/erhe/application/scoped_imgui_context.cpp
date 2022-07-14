#include "erhe/application/scoped_imgui_context.hpp"

#include <imgui/imgui.h>

namespace erhe::application
{

Scoped_imgui_context::Scoped_imgui_context(::ImGuiContext* context)
{
    m_old_context = ImGui::GetCurrentContext();

#if defined(ERHE_GUI_LIBRARY_IMGUI)
    // TODO Currently messy Expects(m_old_context == nullptr);
    ImGui::SetCurrentContext(context);
#endif
}

Scoped_imgui_context::~Scoped_imgui_context() noexcept
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::SetCurrentContext(m_old_context);
#endif
}

}  // namespace editor
