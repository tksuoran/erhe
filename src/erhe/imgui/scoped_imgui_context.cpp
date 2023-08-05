#include "erhe/imgui/scoped_imgui_context.hpp"
#include "erhe/imgui/imgui_viewport.hpp"
#include "erhe/imgui/imgui_renderer.hpp"

#include <imgui/imgui.h>

namespace erhe::imgui
{

Scoped_imgui_context::Scoped_imgui_context(
    Imgui_viewport& imgui_viewport
)
    : m_imgui_renderer{imgui_viewport.get_imgui_renderer()}
{
    m_imgui_renderer.lock_mutex();
    m_imgui_renderer.make_current(&imgui_viewport);
}

Scoped_imgui_context::~Scoped_imgui_context() noexcept
{
    m_imgui_renderer.make_current(nullptr);
    m_imgui_renderer.unlock_mutex();
}

}  // namespace erhe::imgui
