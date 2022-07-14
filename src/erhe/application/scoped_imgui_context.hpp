#pragma once

#include <cstdint>

struct ImGuiContext;

namespace erhe::application
{

class Scoped_imgui_context
{
public:
    explicit Scoped_imgui_context(::ImGuiContext* context);
    ~Scoped_imgui_context() noexcept;

private:
    ::ImGuiContext* m_old_context{nullptr};
};


} // namespace erhe::application
