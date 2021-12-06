#include "windows/frame_log_window.hpp"
#include "log.hpp"
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

    log_startup     .set_sink(this);
    log_programs    .set_sink(this);
    log_textures    .set_sink(this);
    log_input       .set_sink(this);
    log_parsers     .set_sink(this);
    log_render      .set_sink(this);
    log_trs_tool    .set_sink(this);
    log_tools       .set_sink(this);
    log_selection   .set_sink(this);
    log_id_render   .set_sink(this);
    log_framebuffer .set_sink(this);
    log_pointer     .set_sink(this);
    log_input_events.set_sink(this);
    log_materials   .set_sink(this);
    log_renderdoc   .set_sink(this);
    log_brush       .set_sink(this);
    log_physics     .set_sink(this);
    log_gl          .set_sink(this);
    log_headset     .set_sink(this);
    log_scene       .set_sink(this);

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

void Frame_log_window::imgui()
{
    ImGui::Begin(c_title.data());
    for (const auto& entry : m_entries)
    {
        ImGui::TextUnformatted(entry.c_str());
    }
    ImGui::End();
}

}
