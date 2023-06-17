#include "erhe/application/imgui/scoped_imgui_context.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"

#include <imgui/imgui.h>

namespace erhe::application
{

Scoped_imgui_context::Scoped_imgui_context(
    Imgui_viewport& imgui_viewport
)
{
    g_imgui_windows->lock_mutex();
    g_imgui_windows->make_current(&imgui_viewport);
}

Scoped_imgui_context::~Scoped_imgui_context() noexcept
{
    g_imgui_windows->make_current(nullptr);
    g_imgui_windows->unlock_mutex();
}

}  // namespace erhe::application
