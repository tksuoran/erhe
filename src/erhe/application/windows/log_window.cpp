#include "erhe/application/windows/log_window.hpp"

#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/view.hpp"
#include "erhe/application/log.hpp"

//#include "renderers/shadow_renderer.hpp"

//#include "erhe/graphics/debug.hpp"
//#include "erhe/physics/log.hpp"
//#include "erhe/raytrace/log.hpp"
#include "erhe/toolkit/timestamp.hpp"


#include <imgui.h>

#include <algorithm>

namespace erhe::application
{

Log_window_sink::Log_window_sink(Log_window& log_window)
    : m_log_window{log_window}
{
}

void Log_window_sink::write(const erhe::log::Color& color, const std::string_view text)
{
    m_log_window.tail_log_write(color, text);
}

Frame_log_window_sink::Frame_log_window_sink(Log_window& log_window)
    : m_log_window{log_window}
{
}

void Frame_log_window_sink::write(const erhe::log::Color& color, const std::string_view text)
{
    m_log_window.frame_log_write(color, text);
}

auto Log_window_toggle_pause_command::try_call(Command_context& context) -> bool
{
    static_cast<void>(context);

    m_log_window.toggle_pause();
    return true;
}

Log_window::Log_window()
    : erhe::components::Component{c_label}
    , Imgui_window               {c_title, c_label}
    , m_tail_log_sink            {*this}
    , m_frame_log_sink           {*this}
    , m_toggle_pause_command     {*this}
{
}

Log_window::Entry::Entry(
    const ImVec4&      color,
    const std::string& message,
    const unsigned int repeat_count
)
    : color       {color}
    , message     {message}
    , repeat_count{repeat_count}
{
    timestamp = erhe::toolkit::timestamp();
}

Log_window::~Log_window() = default;

void Log_window::connect()
{
    require<Imgui_windows>();
}

void Log_window::initialize_component()
{
    get<Imgui_windows>()->register_imgui_window(this);
    m_min_size = glm::vec2{220.0f, 120.0f};

#if 0
    log_brush                   .set_sink(&m_tail_log_sink);
    log_command_state_transition.set_sink(&m_tail_log_sink);
    log_fly_camera              .set_sink(&m_tail_log_sink);
    log_framebuffer             .set_sink(&m_frame_log_sink, false);
    log_gl                      .set_sink(&m_tail_log_sink);
    log_headset                 .set_sink(&m_tail_log_sink);
    log_id_render               .set_sink(&m_tail_log_sink);
    log_input                   .set_sink(&m_tail_log_sink);
    log_input_event             .set_sink(&m_tail_log_sink);
    log_input_events            .set_sink(&m_tail_log_sink);
    log_input_event_consumed    .set_sink(&m_tail_log_sink);
    log_input_event_filtered    .set_sink(&m_tail_log_sink);
    log_materials               .set_sink(&m_tail_log_sink);
    log_node_properties         .set_sink(&m_tail_log_sink, false);
    log_parsers                 .set_sink(&m_tail_log_sink);
    log_performance             .set_sink(&m_tail_log_sink);
    log_physics                 .set_sink(&m_tail_log_sink);
    log_pointer                 .set_sink(&m_frame_log_sink, false);
    log_programs                .set_sink(&m_tail_log_sink);
    log_raytrace                .set_sink(&m_tail_log_sink);
    log_render                  .set_sink(&m_frame_log_sink, false);
    log_renderdoc               .set_sink(&m_tail_log_sink);
    log_scene                   .set_sink(&m_tail_log_sink);
    log_selection               .set_sink(&m_tail_log_sink);
    log_startup                 .set_sink(&m_tail_log_sink);
    log_textures                .set_sink(&m_tail_log_sink);
    log_tools                   .set_sink(&m_tail_log_sink);
    log_trs_tool                .set_sink(&m_tail_log_sink);

    hextiles::log_map_window    .set_sink(&m_tail_log_sink);

    erhe::raytrace::log_geometry.set_sink(&m_tail_log_sink);
    erhe::raytrace::log_embree  .set_sink(&m_tail_log_sink);

    erhe::physics::log_physics      .set_sink(&m_tail_log_sink);
    erhe::physics::log_physics_frame.set_sink(&m_frame_log_sink, false);
#endif

    const auto view = get<View>();
    view->register_command   (&m_toggle_pause_command);
    view->bind_command_to_key(&m_toggle_pause_command, erhe::toolkit::Key_escape);
}

void Log_window::toggle_pause()
{
    m_paused = !m_paused;
}

void Log_window::tail_log_write(
    const erhe::log::Color& color,
    const std::string_view  text
)
{
    if (m_paused)
    {
        return;
    }

    m_tail_entries.emplace_back(
        ImVec4{color.r, color.g, color.b, 1.0},
        std::string{text}
    );
}

void Log_window::frame_log_write(
    const erhe::log::Color& color,
    const std::string_view text
)
{
    if (m_paused)
    {
        return;
    }

    m_frame_entries.emplace_back(
        ImVec4{color.r, color.g, color.b, 1.0},
        std::string{text}
    );
}

void Log_window::imgui()
{
    if (ImGui::TreeNodeEx("Tail", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
    {
        ImGui::SetNextItemWidth(100.0f);

        ImGui::DragInt(
            "Show",
            &m_tail_buffer_show_size,
            1.0f,
            1,
            std::numeric_limits<int>::max(),
            "%d",
            ImGuiSliderFlags_AlwaysClamp
        );

        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);

        ImGui::DragInt(
            "Trim",
            &m_tail_buffer_trim_size,
            1.0f,
            1,
            std::numeric_limits<int>::max(),
            "%d",
            ImGuiSliderFlags_AlwaysClamp
        );

        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::Button("Clear"))
        {
            m_tail_entries.clear();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::Checkbox("Paused", &m_paused);

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
            ImGui::SetNextItemWidth(100.0f);
            ImGui::TextColored(
                ImVec4{0.7f, 0.7f, 0.7f, 1.0f},
                "%s",
                entry.timestamp.c_str()
            );
            ImGui::SameLine();
            ImGui::TextColored(entry.color, "%s", entry.message.c_str());
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
    if (
        ImGui::TreeNodeEx(
            "Frame",
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed
        )
    )
    {
        for (const auto& entry : m_frame_entries)
        {
            ImGui::SetNextItemWidth(100.0f);
            ImGui::TextUnformatted (entry.timestamp.c_str());
            ImGui::SameLine        ();
            ImGui::TextUnformatted (entry.message.c_str());
        }
        ImGui::TreePop();
    }
    m_frame_entries.clear();
}

} // namespace erhe::application
