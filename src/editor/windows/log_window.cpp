#include "windows/log_window.hpp"
#include "log.hpp"
#include "erhe/raytrace/log.hpp"
#include "editor_tools.hpp"

#include <imgui.h>

#include <algorithm>

namespace editor
{

Log_window::Log_window()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_title}
{
}

Log_window::~Log_window() = default;

void Log_window::initialize_component()
{
    get<Editor_tools>()->register_imgui_window(this);

    log_startup     .set_sink(this);
    log_programs    .set_sink(this);
    log_textures    .set_sink(this);
    log_input       .set_sink(this);
    log_parsers     .set_sink(this);
    log_render      .set_sink(this);
    //log_trs_tool    .set_sink(this);
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

    //hide();
}


void Log_window::write(std::string_view text) 
{
    m_frame_entries.emplace_back(text);
}

void Log_window::frame_write(const char* format, fmt::format_args args)
{
    m_frame_entries.emplace_back(fmt::vformat(format, args));
}

void Log_window::tail_write(const char* format, fmt::format_args args)
{
    std::string message = fmt::vformat(format, args);
    if (!m_tail_entries.empty())
    {
        auto& back = m_tail_entries.back();
        if (back.message == message)
        {
            ++back.repeat_count;
            return;
        }
    }

    m_tail_entries.emplace_back(fmt::vformat(format, args));
}

void Log_window::imgui()
{
    if (ImGui::TreeNodeEx("Tail", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
    {
        ImGui::SetNextItemWidth(100.0f);
        ImGui::DragInt("Show", &m_tail_buffer_show_size, 1.0f, 1, std::numeric_limits<int>::max(), "%d", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::DragInt("Trim", &m_tail_buffer_trim_size, 1.0f, 1, std::numeric_limits<int>::max(), "%d", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::Button("Clear"))
        {
            m_tail_entries.clear();
        }

        const auto trim_size = static_cast<size_t>(m_tail_buffer_trim_size);
        if (m_tail_entries.size() > trim_size)
        {
            const auto trim_count = m_tail_entries.size() - trim_size;
            m_tail_entries.erase(
                m_tail_entries.begin(),
                m_tail_entries.begin() + trim_count
            );
            const auto new_size = m_tail_entries.size();
            ERHE_VERIFY(new_size == trim_size);
        }

        const auto visible_count = (std::min)(
            static_cast<size_t>(m_tail_buffer_show_size),
            m_tail_entries.size()
        );
        for (
            auto i = m_tail_entries.rbegin(),
            end = m_tail_entries.rbegin() + visible_count;
            i != end;
            ++i
        )
        {
            auto& entry = *i;
            ImGui::TextUnformatted(entry.message.c_str());
            if (entry.repeat_count > 0)
            {
                ImGui::TextColored(
                    ImVec4{0.55f, 0.55f, 0.55f, 1.0f},
                    "Message repeated %u times",
                    entry.repeat_count
                );
            }
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Frame", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
    {
        for (const auto& entry : m_frame_entries)
        {
            ImGui::TextUnformatted(entry.c_str());
        }
        ImGui::TreePop();
    }
    m_frame_entries.clear();
}

}
