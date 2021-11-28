#include "windows/frame_log_window.hpp"
#include "erhe/raytrace/log.hpp"
#include "tools.hpp"

#include <imgui.h>

namespace editor
{

Frame_log_window::Frame_log_window()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_title}
{
}

Frame_log_window::~Frame_log_window() = default;

void Frame_log_window::initialize_component()
{
    get<Editor_tools>()->register_imgui_window(this);

    erhe::raytrace::log_geometry.set_sink(this);
}

void Frame_log_window::new_frame()
{
    m_entries.clear();
}

void Frame_log_window::write(std::string_view text) 
{
    m_entries.emplace_back(text);
}

void Frame_log_window::write(const char* format, fmt::format_args args)
{
    m_entries.emplace_back(fmt::vformat(format, args));
}

void Frame_log_window::imgui(Pointer_context&)
{
    ImGui::Begin(c_title.data());
    for (const auto& entry : m_entries)
    {
        ImGui::TextUnformatted(entry.c_str());
    }
    ImGui::End();
}

}
