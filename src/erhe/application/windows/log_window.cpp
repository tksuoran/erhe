#include "erhe/application/windows/log_window.hpp"

#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/view.hpp"
#include "erhe/application/log.hpp"

#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/timestamp.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

#include <algorithm>

namespace erhe::application
{

auto Log_window_toggle_pause_command::try_call(Command_context& context) -> bool
{
    static_cast<void>(context);

    m_log_window.toggle_pause();
    return true;
}

Log_window::Log_window()
    : erhe::components::Component{c_label}
    , Imgui_window               {c_title, c_label}
    , m_toggle_pause_command     {*this}
{
}

Log_window::~Log_window() noexcept
{
}

void Log_window::declare_required_components()
{
    require<Imgui_windows>();
    require<View         >();
}

void Log_window::initialize_component()
{
    get<Imgui_windows>()->register_imgui_window(this);
    m_min_size = glm::vec2{220.0f, 120.0f};

    const auto view = get<View>();
    view->register_command   (&m_toggle_pause_command);
    view->bind_command_to_key(&m_toggle_pause_command, erhe::toolkit::Key_escape);
}

void Log_window::toggle_pause()
{
    m_paused = !m_paused;
}

void Log_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    auto& tail  = erhe::log::get_tail_store_log();
    auto& frame = erhe::log::get_frame_store_log();

    if (ImGui::TreeNodeEx("Tail", /*ImGuiTreeNodeFlags_DefaultOpen |*/ ImGuiTreeNodeFlags_Framed))
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
            tail->trim(0);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::Checkbox("Paused", &m_paused))
        {
            tail->set_paused(m_paused);
        }

        const auto trim_size = static_cast<size_t>(m_tail_buffer_trim_size);
        tail->trim(trim_size);

        auto& tail_entries = tail->get_log();

        const auto visible_count = (std::min)(
            static_cast<size_t>(m_tail_buffer_show_size),
            tail_entries.size()
        );
        for (
            auto i = tail_entries.rbegin(),
            end = tail_entries.rbegin() + visible_count;
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
            //ImGui::TextColored(entry.color, "%s", entry.message.c_str());
            ImGui::TextUnformatted(entry.message.c_str());
            //if (entry.repeat_count > 0)
            //{
            //    ImGui::TextColored(
            //        ImVec4{0.55f, 0.55f, 0.55f, 1.0f},
            //        "Message repeated %u times",
            //        entry.repeat_count
            //    );
            //}
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
        auto& frame_entries = frame->get_log();
        for (const auto& entry : frame_entries)
        {
            ImGui::SetNextItemWidth(100.0f);
            ImGui::TextUnformatted (entry.timestamp.c_str());
            ImGui::SameLine        ();
            ImGui::TextUnformatted (entry.message.c_str());
        }
        ImGui::TreePop();
    }
    frame->trim(0);
#endif
}

} // namespace erhe::application
