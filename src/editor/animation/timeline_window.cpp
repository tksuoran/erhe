#include "animation/timeline_window.hpp"

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/imgui_renderer.hpp"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

// #include "IconsMaterialDesignIcons.h"
#define ICON_MDI_PAUSE                                    "\xf3\xb0\x8f\xa4" // U+F03E4
#define ICON_MDI_PLAY                                     "\xf3\xb0\x90\x8a" // U+F040A
#define ICON_MDI_STOP                                     "\xf3\xb0\x93\x9b" // U+F04DB
#define ICON_MDI_SKIP_PREVIOUS                            "\xf3\xb0\x92\xae" // U+F04AE
#define ICON_MDI_SKIP_NEXT                                "\xf3\xb0\x92\xad" // U+F04AD
#define ICON_MDI_SKIP_BACKWARD                            "\xf3\xb0\x92\xab" // U+F04AB
#define ICON_MDI_SKIP_FORWARD                             "\xf3\xb0\x92\xac" // U+F04AC
#define ICON_MDI_STEP_BACKWARD                            "\xf3\xb0\x93\x95" // U+F04D5
#define ICON_MDI_STEP_BACKWARD_2                          "\xf3\xb0\x93\x96" // U+F04D6
#define ICON_MDI_STEP_FORWARD                             "\xf3\xb0\x93\x97" // U+F04D7
#define ICON_MDI_STEP_FORWARD_2                           "\xf3\xb0\x93\x98" // U+F04D8
#define ICON_MDI_REPEAT                                   "\xf3\xb0\x91\x96" // U+F0456
#define ICON_MDI_REPEAT_OFF                               "\xf3\xb0\x91\x97" // U+F0457
#define ICON_MDI_ARROW_LEFT                               "\xf3\xb0\x81\x8d" // U+F004D
#define ICON_MDI_ARROW_RIGHT                              "\xf3\xb0\x81\x94" // U+F0054
#define ICON_MDI_CHEVRON_LEFT                             "\xf3\xb0\x85\x81" // U+F0141
#define ICON_MDI_CHEVRON_RIGHT                            "\xf3\xb0\x85\x82" // U+F0142

namespace editor {

Timeline_window::Timeline_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context
)
    : Imgui_window{imgui_renderer, imgui_windows, "Timeline", "timeline"}
    , m_context   {app_context}
{
}


void Timeline_window::update()
{
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (!m_last_time.has_value()) {
        m_last_time = now;
        return;
    }

    if (m_length == 0.0f) {
        m_play_position = 0.0f;
        return;
    }

    const std::chrono::duration<float> duration = now - m_last_time.value();
    m_last_time = now;

    if (m_playing) {
        const float dt_wall = duration.count();
        const float dt_play = dt_wall * m_play_speed;
        m_play_position += dt_play;

        if (m_looping) {
            while (m_play_position < 0.0f) {
                m_play_position += m_length;
            }
            while (m_play_position > m_length) {
                m_play_position -= m_length;
            }
        } else {
            if (m_play_position < 0.0f) {
                m_playing = false;
                m_play_position = 0.0f;
            }
            if (m_play_position > m_length) {
                m_playing = false;
                m_play_position = m_length;
            }
        }
    }
}

void Timeline_window::set_timeline_length(float length)
{
    m_length = length;
}

void Timeline_window::set_play_position(float position)
{
    m_play_position = position;
}

void Timeline_window::set_play_speed(float speed)
{
    m_play_speed = speed;
    m_play_speed_abs = std::abs(speed);
    m_backwards = speed < 0.0f;
}

auto Timeline_window::get_timeline_length() const -> float
{
    return m_length;
}

auto Timeline_window::get_play_position() const -> float
{
    return m_play_position;
}

auto Timeline_window::get_play_speed() const -> float
{
    return m_play_speed;
}

void Timeline_window::imgui()
{
    update();
    ImFont* icon_font = m_imgui_renderer.material_design_font();
    if (icon_font == nullptr) {
        return;
    }
    const float font_size = m_imgui_renderer.get_imgui_settings().material_design_font_size;

    {
        ImGui::PushID("##seek_start");
        ImGui::PushFont(icon_font, font_size);
        const bool seek_start = ImGui::Button(ICON_MDI_SKIP_PREVIOUS);
        ImGui::PopFont();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Seek to start");
        }
        if (seek_start) {
            m_play_position = 0.0f;
        }
        ImGui::PopID();
    }

    ImGui::SameLine();
    {
        ImGui::PushID("##play_pause");
        if (m_playing) {
            ImGui::PushFont(icon_font, font_size);
            const bool pause = ImGui::Button(ICON_MDI_PAUSE);
            ImGui::PopFont();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Playing - Press to pause");
            }
            if (pause) {
                m_playing = false;
            }
        } else {
            ImGui::PushFont(icon_font, font_size);
            const bool play = ImGui::Button(ICON_MDI_PLAY);
            ImGui::PopFont();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Paused - Press to play");
            }
            if (play) {
                if (m_play_position == m_length) {
                    m_play_position = 0.0f;
                }
                m_playing = true;
            }
        }
        ImGui::PopID();
    }

    ImGui::SameLine();
    {
        ImGui::PushID("##seek_end");
        ImGui::PushFont(icon_font, font_size);
        const bool seek_end = ImGui::Button(ICON_MDI_SKIP_NEXT);
        ImGui::PopFont();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Seek to end");
        }
        if (seek_end) {
            m_play_position = m_length;
        }
        ImGui::PopID();
    }

    ImGui::PushID("##loop");
    {
        ImGui::SameLine();
        if (m_looping) {
            ImGui::PushFont(icon_font, font_size);
            const bool repeat_off = ImGui::Button(ICON_MDI_REPEAT);
            ImGui::PopFont();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Repeat in on - Press to turn repeat off");
            }
            if (repeat_off) {
                m_looping = false;
            }
        } else {
            ImGui::PushFont(icon_font, font_size);
            const bool repeat = ImGui::Button(ICON_MDI_REPEAT_OFF);
            ImGui::PopFont();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Repeat in off - Press to turn repeat on");
            }
            if (repeat) {
                m_looping = true;
            }
        }
        ImGui::PopID();
    }

    {
        ImGui::PushID("##speed");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(240.0f);
        ImGui::SliderFloat("##Speed", &m_play_speed_abs, 0.01f, 100.f, "Play speed %.3f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);

        ImGui::SameLine();
        if (m_backwards) {
            ImGui::PushFont(icon_font, font_size);
            const bool pressed = ImGui::Button(ICON_MDI_CHEVRON_LEFT);
            ImGui::PopFont();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Playing backwards - Press to play forwards");
            }
            if (pressed) {
                m_backwards = false;
            }
        } else {
            ImGui::PushFont(icon_font, font_size);
            const bool pressed = ImGui::Button(ICON_MDI_CHEVRON_RIGHT);
            ImGui::PopFont();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Playing forwards - Press to play backwards");
            }
            if (pressed) {
                m_backwards = true;
            }
        }
        m_play_speed = m_backwards ? -m_play_speed_abs : m_play_speed_abs;
        ImGui::PopID();
    }

    ImGui::PushID("##positions");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderFloat("##TimeLine", &m_play_position, 0.0f, m_length);
    ImGui::PopID();
}

}
