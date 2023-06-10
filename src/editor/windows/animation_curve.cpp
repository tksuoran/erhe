#include "windows/animation_curve.hpp"

#include "erhe/scene/animation.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include <functional>

namespace editor {

[[nodiscard]] auto map(
    const float input_value,
    const float input_low,
    const float input_high,
    const float output_low,
    const float output_high
) -> float
{
    float input_range  = input_high  - input_low;
    float output_range = output_high - output_low;
    float ratio        = output_range / input_range;
    float mapped_value = ((input_value - input_low) * ratio) + output_low;
    return mapped_value;
}

[[nodiscard]] auto scale(
    const float input_value,
    const float input_low,
    const float input_high,
    const float output_low,
    const float output_high
) -> float
{
    float input_range  = input_high  - input_low;
    float output_range = output_high - output_low;
    float ratio        = output_range / input_range;
    float mapped_value = input_value * ratio;
    return mapped_value;
}

[[nodiscard]] auto map(
    const ImVec2 input_value,
    const ImRect input_box,
    const ImRect output_box
) -> ImVec2
{
    return ImVec2{
        map(input_value.x, input_box.Min.x, input_box.Max.x, output_box.Min.x, output_box.Max.x),
        map(input_value.y, input_box.Min.y, input_box.Max.y, output_box.Min.y, output_box.Max.y)
    };
}

[[nodiscard]] auto scale(
    const ImVec2 input_value,
    const ImRect input_box,
    const ImRect output_box
) -> ImVec2
{
    return ImVec2{
        scale(input_value.x, input_box.Min.x, input_box.Max.x, output_box.Min.x, output_box.Max.x),
        scale(input_value.y, input_box.Min.y, input_box.Max.y, output_box.Min.y, output_box.Max.y)
    };
}

void animation_curve(erhe::scene::Animation& animation)
{
    ImGuiWindow* Window = ImGui::GetCurrentWindow();
    if (Window->SkipItems) {
        return;
    }

    static int channel_index{0};
    static int component_index{0};
    static std::string channel_description;
    if (ImGui::BeginCombo("Channel", channel_description.c_str(), ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_HeightLarge)) {
        for (std::size_t i = 0; i < animation.channels.size(); ++i) {
            auto& channel = animation.channels.at(i);
            std::string text = fmt::format(
                "{} {}##channel-{}",
                channel.target->get_name(),
                erhe::scene::c_str(channel.path),
                i
            );
            if (ImGui::Selectable(text.c_str(), channel_index == i)) {
                channel_description = text;
                channel_index = static_cast<int>(i);
                component_index = 0;
            }
        }
        ImGui::EndCombo();
    }
    auto& channel = animation.channels.at(channel_index);
    const auto& sampler = animation.samplers.at(channel.sampler_index);

    static const char* translation_components[] = { "Tx", "Ty", "Tz"       };
    static const char* rotation_components   [] = { "Qx", "Qy", "Qz", "Qw" };
    static const char* scale_components      [] = { "Sx", "Sy", "Sz"       };
    switch (channel.path)
    {
        case erhe::scene::Animation_path::TRANSLATION: ImGui::Combo("Component", &component_index, translation_components, 3, 3); break;
        case erhe::scene::Animation_path::ROTATION:    ImGui::Combo("Component", &component_index, rotation_components,    4, 4); break;
        case erhe::scene::Animation_path::SCALE:       ImGui::Combo("Component", &component_index, scale_components,       3, 3); break;
        default: break;
    }

    {
        static float time{0.0f};
        ImGui::SliderFloat("Time", &time, 0.01f, 10.0f, "%.3f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);
        float value = animation.evaluate(time, channel_index, component_index);
        ImGui::Text("%f", value);
    }

    const auto& io = ImGui::GetIO();

    static float zoom_scale{1.0f};
    static ImVec2 pan{0.0f, 0.0f};
    ImGui::SliderFloat("Zoom Scale", &zoom_scale, 0.0001f, 10000.0f, "%.5f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_Logarithmic);

    if (zoom_scale < 0.0001f) {
        zoom_scale = 0.0001f;
    }
    if (zoom_scale > 10000.0f) {
        zoom_scale = 10000.0f;
    }

    ImVec2 canvas = ImGui::GetContentRegionAvail();
    ImRect bb{Window->DC.CursorPos, Window->DC.CursorPos + canvas};
    float aspect = bb.GetWidth() / bb.GetHeight();
    ImRect view{-1.0f * aspect, 1.0f, 1.0f * aspect, -1.0}; 
    view.Min.x *= zoom_scale;
    view.Min.y *= zoom_scale;
    view.Max.x *= zoom_scale;
    view.Max.y *= zoom_scale;
    view.Translate(pan);

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        ImVec2 delta_in_view = scale(ImVec2{io.MouseDelta.x, io.MouseDelta.y}, bb, view); 
        pan -= delta_in_view;
    }

    if (io.MouseWheel != 0.0f) {
        zoom_scale -= io.MouseWheel * 0.05f * zoom_scale;
    }

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        float distance = io.MouseDelta.y;
        zoom_scale += 0.005f * distance * zoom_scale;
    }

    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, 0)) {
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::RenderFrame(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_FrameBg, 1), true, style.FrameRounding);

    auto view_to_pixel = [&](float x, float y) -> ImVec2 {
        return map(ImVec2{x, y}, view, bb); 
    };

    auto pixel_to_view = [&](float x, float y) -> ImVec2 {
        return map(ImVec2{x, y}, bb, view); 
    };

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(bb.Min, bb.Max, true);

    // Draw grid
    float grid_spacing = 0.1f;
    {
        float grid_interval = grid_spacing;
        float view_size = std::max(view.GetWidth(), view.GetHeight());
        while (view_size / grid_interval > 100.0f) {
            grid_interval *= 10.0f;
        }
        while (view_size / grid_interval < 10.0f) {
            grid_interval *= 0.1f;
        }
        const ImU32 color = ImGui::GetColorU32(ImGuiCol_Separator);
        const auto& f = view_to_pixel;
        for (float x = grid_interval; x <= view.Max.x; x += grid_interval) {
            if (x < view.Min.x) continue;
            draw_list->AddLine(f(x, view.Min.y), f(x, view.Max.y), color);
        }
        for (float x = -grid_interval; x >= view.Min.x; x -= grid_interval) {
            if (x > view.Max.x) continue;
            draw_list->AddLine(f(x, view.Min.y), f(x, view.Max.y), color);
        }
        for (float y = grid_interval; y <= view.Min.y; y += grid_interval) {
            if (y < view.Max.y) continue;
            draw_list->AddLine(f(view.Min.x, y), f(view.Max.x, y), color);
        }
        for (float y = -grid_interval; y >= view.Max.y; y -= grid_interval) {
            if (y > view.Min.y) continue;
            draw_list->AddLine(f(view.Min.x, y), f(view.Max.x, y), color);
        }
        draw_list->AddLine(f(0.0f, view.Min.y), f(0.0f, view.Max.y), ImGui::GetColorU32(ImGuiCol_SeparatorActive));
        draw_list->AddLine(f(view.Min.x, 0.0f), f(view.Max.x, 0.0f), ImGui::GetColorU32(ImGuiCol_SeparatorActive));
    }

    // Draw curve
    int step_count = 100;
    draw_list->PathClear();
    float start_time  = std::max(sampler.timestamps.front(), view.Min.x);
    float end_time    = std::min(sampler.timestamps.back(), view.Max.x);
    float time_length = end_time - start_time;
    for (int i = 0; i <= step_count; ++i) {
        float rel   = static_cast<float>(i) / static_cast<float>(step_count);
        float time  = start_time + rel * time_length;
        float value = animation.evaluate(time, channel_index, component_index);
        draw_list->PathLineTo(view_to_pixel(time, value));
    }
    draw_list->PathStroke(ImGui::GetColorU32(ImGuiCol_PlotLines), 0, 1.0f);

    // Draw Keyframes
    {
        ImU32 color = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
        for (std::size_t i = 0; i < sampler.timestamps.size(); ++i) {
            float  time  = sampler.timestamps.at(i);
            float  value = animation.evaluate(time, channel_index, component_index);
            ImVec2 p     = view_to_pixel(time, value);
            draw_list->AddRect(
                p - ImVec2(4.0f, 4.0f),
                p + ImVec2(4.0f, 4.0f),
                color,
                0.0f,
                ImDrawFlags_None,
                2.0f
            );
        }
    }
    draw_list->PopClipRect();

    if (ImGui::IsMouseHoveringRect(bb.Min, bb.Max)) {
        ImVec2 mousePos = pixel_to_view(io.MousePos.x, io.MousePos.y);
        ImGui::SetTooltip("X = %.4f Y = %.4f", mousePos.x, mousePos.y);
    }
}

}
