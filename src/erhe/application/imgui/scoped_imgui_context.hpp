#pragma once

#include <cstdint>
#include <mutex>

struct ImGuiContext;

namespace erhe::application
{

class Imgui_viewport;
class Imgui_windows;

class Scoped_imgui_context
{
public:
    Scoped_imgui_context(Imgui_viewport& imgui_viewport);
    ~Scoped_imgui_context();

private:
    std::lock_guard<std::mutex> m_lock;
};

} // namespace erhe::application
