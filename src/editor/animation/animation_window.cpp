#include "animation/animation_window.hpp"

#include "animation/animation_keying.hpp"
#include "animation/animation_player.hpp"
#include "app_context.hpp"
#include "app_scenes.hpp"
#include "content_library/content_library.hpp"
#include "operations/animation_edit_operation.hpp"
#include "operations/compound_operation.hpp"
#include "operations/content_library_attach_operation.hpp"
#include "operations/operation_stack.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

// From IconsMaterialDesignIcons.h - that header also defines the (non-inline)
// icon name/code arrays for the icon browser, so it must only be included in
// one translation unit (icon_browser.cpp). Transport icons are re-declared
// here instead.
#define ICON_MDI_PAUSE               "\xf3\xb0\x8f\xa4" // U+F03E4
#define ICON_MDI_PLAY                "\xf3\xb0\x90\x8a" // U+F040A
#define ICON_MDI_REPEAT              "\xf3\xb0\x91\x96" // U+F0456
#define ICON_MDI_REPEAT_OFF          "\xf3\xb0\x91\x97" // U+F0457
#define ICON_MDI_SKIP_NEXT           "\xf3\xb0\x92\xad" // U+F04AD
#define ICON_MDI_SKIP_PREVIOUS       "\xf3\xb0\x92\xae" // U+F04AE
#define ICON_MDI_STOP                "\xf3\xb0\x93\x9b" // U+F04DB
#define ICON_MDI_FIT_TO_PAGE_OUTLINE "\xf3\xb0\xbb\xb6" // U+F0EF6

#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <fmt/format.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h> // ImGuiItemFlags_MixedValue (tri-state channel checkbox)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace editor {

namespace {

constexpr float c_key_half_size_px    = 4.0f;
constexpr float c_key_hit_radius_px   = 6.0f;
constexpr float c_curve_hit_radius_px = 8.0f;
constexpr float c_drag_threshold_px   = 3.0f;
constexpr int   c_cubic_segment_steps = 16;

const char* const c_component_names[4] = { "X", "Y", "Z", "W" };

// Rounds a raw step to a "nice" 1-2-5 progression value.
[[nodiscard]] auto nice_step(const float raw_step) -> float
{
    if (!(raw_step > 0.0f) || !std::isfinite(raw_step)) {
        return 1.0f;
    }
    const float power    = std::pow(10.0f, std::floor(std::log10(raw_step)));
    const float mantissa = raw_step / power;
    if (mantissa < 1.5f) return 1.0f * power;
    if (mantissa < 3.5f) return 2.0f * power;
    if (mantissa < 7.5f) return 5.0f * power;
    return 10.0f * power;
}

} // anonymous namespace

Animation_window::Animation_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context
)
    : Imgui_window{imgui_renderer, imgui_windows, "Animation", "animation"}
    , m_context   {app_context}
{
    set_min_size(480.0f, 200.0f);
}

void Animation_window::set_animation(const std::shared_ptr<erhe::scene::Animation>& animation)
{
    if (m_animation == animation) {
        return;
    }
    m_animation = animation;
    m_selection.clear();
    m_drag_mode = Drag_mode::none;
    m_channel_visibility.clear();
    ensure_visibility_size();
    if (m_animation) {
        // Small animations: show everything. Large (rigged) animations: show
        // only the first channel to keep the view readable.
        if (m_animation->channels.size() > 4) {
            std::fill(m_channel_visibility.begin(), m_channel_visibility.end(), 0u);
            if (!m_channel_visibility.empty()) {
                m_channel_visibility.front() = 0xffffffffu;
            }
        }
    }
    m_frame_all_queued = true;
    if (m_context.animation_player != nullptr) {
        m_context.animation_player->set_animation(animation);
    }
}

auto Animation_window::get_animation() const -> const std::shared_ptr<erhe::scene::Animation>&
{
    return m_animation;
}

void Animation_window::frame_all()
{
    m_frame_all_queued = true;
}

void Animation_window::set_channel_visibility(const std::size_t channel_index, const uint32_t component_mask)
{
    ensure_visibility_size();
    if (channel_index < m_channel_visibility.size()) {
        m_channel_visibility[channel_index] = component_mask;
    }
}

void Animation_window::ensure_visibility_size()
{
    const std::size_t channel_count = m_animation ? m_animation->channels.size() : 0;
    if (m_channel_visibility.size() != channel_count) {
        m_channel_visibility.assign(channel_count, 0xffffffffu);
    }
}

void Animation_window::prune_stale_selection()
{
    if (!m_animation) {
        m_selection.clear();
        return;
    }
    const erhe::scene::Animation& animation = *m_animation.get();
    m_selection.erase(
        std::remove_if(
            m_selection.begin(),
            m_selection.end(),
            [&animation](const Curve_point& point) -> bool {
                if (point.channel_index >= animation.channels.size()) {
                    return true;
                }
                const erhe::scene::Animation_channel& channel = animation.channels[point.channel_index];
                const erhe::scene::Animation_sampler& sampler = animation.samplers.at(channel.sampler_index);
                if (point.key_index >= sampler.timestamps.size()) {
                    return true;
                }
                return point.component >= erhe::scene::get_component_count(channel.path);
            }
        ),
        m_selection.end()
    );
}

auto Animation_window::is_point_selected(const Curve_point& point) const -> bool
{
    return std::find(m_selection.begin(), m_selection.end(), point) != m_selection.end();
}

auto Animation_window::is_component_visible(const std::size_t channel_index, const std::size_t component) const -> bool
{
    if (channel_index >= m_channel_visibility.size()) {
        return false;
    }
    return (m_channel_visibility[channel_index] & (1u << component)) != 0;
}

auto Animation_window::get_component_color(const std::size_t component) const -> ImU32
{
    switch (component) {
        case 0:  return IM_COL32(235,  90,  90, 255); // X - red
        case 1:  return IM_COL32(110, 210, 100, 255); // Y - green
        case 2:  return IM_COL32( 90, 140, 240, 255); // Z - blue
        default: return IM_COL32(230, 205,  90, 255); // W - yellow
    }
}

auto Animation_window::time_to_x(const float time) const -> float
{
    const float span = std::max(m_view_time_max - m_view_time_min, 1.0e-6f);
    return m_canvas_min.x + ((time - m_view_time_min) / span) * (m_canvas_max.x - m_canvas_min.x);
}

auto Animation_window::value_to_y(const float value) const -> float
{
    const float span = std::max(m_view_value_max - m_view_value_min, 1.0e-9f);
    return m_canvas_max.y - ((value - m_view_value_min) / span) * (m_canvas_max.y - (m_canvas_min.y + m_ruler_height));
}

auto Animation_window::x_to_time(const float x) const -> float
{
    const float width = std::max(m_canvas_max.x - m_canvas_min.x, 1.0f);
    return m_view_time_min + ((x - m_canvas_min.x) / width) * (m_view_time_max - m_view_time_min);
}

auto Animation_window::y_to_value(const float y) const -> float
{
    const float height = std::max(m_canvas_max.y - (m_canvas_min.y + m_ruler_height), 1.0f);
    return m_view_value_min + ((m_canvas_max.y - y) / height) * (m_view_value_max - m_view_value_min);
}

void Animation_window::compute_frame_all_view(const float canvas_height_px)
{
    static_cast<void>(canvas_height_px);
    if (!m_animation || m_animation->channels.empty()) {
        m_view_time_min  = 0.0f;
        m_view_time_max  = 1.0f;
        m_view_value_min = -1.0f;
        m_view_value_max = 1.0f;
        return;
    }

    const erhe::scene::Animation& animation = *m_animation.get();
    float time_min  = std::numeric_limits<float>::max();
    float time_max  = std::numeric_limits<float>::lowest();
    float value_min = std::numeric_limits<float>::max();
    float value_max = std::numeric_limits<float>::lowest();
    bool  any       = false;

    for (std::size_t channel_index = 0; channel_index < animation.channels.size(); ++channel_index) {
        const erhe::scene::Animation_channel& channel = animation.channels[channel_index];
        const erhe::scene::Animation_sampler& sampler = animation.samplers.at(channel.sampler_index);
        const std::size_t component_count = erhe::scene::get_component_count(channel.path);
        if (sampler.timestamps.empty() || (component_count == 0)) {
            continue;
        }
        bool channel_any = false;
        for (std::size_t component = 0; component < component_count; ++component) {
            if (!is_component_visible(channel_index, component)) {
                continue;
            }
            channel_any = true;
            for (std::size_t key = 0; key < sampler.timestamps.size(); ++key) {
                const float value = get_keyframe_value(animation, channel_index, key, component);
                value_min = std::min(value_min, value);
                value_max = std::max(value_max, value);
            }
        }
        if (channel_any) {
            any = true;
            time_min = std::min(time_min, sampler.timestamps.front());
            time_max = std::max(time_max, sampler.timestamps.back());
        }
    }

    if (!any) {
        // Nothing visible: fall back to the whole animation time range.
        time_min  = animation.get_first_time();
        time_max  = animation.get_last_time();
        value_min = -1.0f;
        value_max = 1.0f;
    }

    float time_margin = (time_max - time_min) * 0.05f;
    if (time_margin <= 0.0f) {
        time_margin = 0.5f;
    }
    float value_margin = (value_max - value_min) * 0.1f;
    if (value_margin <= 0.0f) {
        value_margin = 1.0f;
    }
    m_view_time_min  = time_min  - time_margin;
    m_view_time_max  = time_max  + time_margin;
    m_view_value_min = value_min - value_margin;
    m_view_value_max = value_max + value_margin;
}

void Animation_window::animation_combo()
{
    const char* preview = m_animation ? m_animation->get_name().c_str() : "(no animation)";
    ImGui::SetNextItemWidth(220.0f);
    if (!ImGui::BeginCombo("##animation", preview, ImGuiComboFlags_HeightLarge)) {
        return;
    }
    if (m_context.app_scenes != nullptr) {
        for (const std::shared_ptr<Scene_root>& scene_root : m_context.app_scenes->get_scene_roots()) {
            const std::shared_ptr<Content_library>& content_library = scene_root->get_content_library();
            if (!content_library || !content_library->animations) {
                continue;
            }
            for (const std::shared_ptr<erhe::scene::Animation>& animation : content_library->animations->get_all<erhe::scene::Animation>()) {
                ImGui::PushID(static_cast<int>(animation->get_id()));
                const bool is_selected = animation == m_animation;
                if (ImGui::Selectable(animation->get_name().c_str(), is_selected)) {
                    set_animation(animation);
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::EndCombo();
}

void Animation_window::transport_toolbar()
{
    Animation_player* player = m_context.animation_player;
    if (player == nullptr) {
        return;
    }

    ImFont* icon_font = m_imgui_renderer.material_design_font();
    const float icon_font_size =
        m_imgui_renderer.get_imgui_settings().scale_factor *
        m_imgui_renderer.get_imgui_settings().material_design_font_size;

    const auto icon_button = [icon_font, icon_font_size](const char* icon, const char* tooltip) -> bool {
        bool pressed = false;
        if (icon_font != nullptr) {
            ImGui::PushFont(icon_font, icon_font_size);
            pressed = ImGui::Button(icon);
            ImGui::PopFont();
        } else {
            pressed = ImGui::Button(tooltip);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip);
        }
        return pressed;
    };

    ImGui::SameLine();
    if (icon_button(ICON_MDI_SKIP_PREVIOUS, "Seek to start")) {
        player->seek(player->get_start_time());
    }

    ImGui::SameLine();
    if (player->is_playing()) {
        if (icon_button(ICON_MDI_PAUSE, "Pause")) {
            player->pause();
        }
    } else {
        if (icon_button(ICON_MDI_PLAY, "Play")) {
            player->play();
        }
    }

    ImGui::SameLine();
    if (icon_button(ICON_MDI_STOP, "Stop (seek to start)")) {
        player->stop();
    }

    ImGui::SameLine();
    if (icon_button(ICON_MDI_SKIP_NEXT, "Seek to end")) {
        player->seek(player->get_end_time());
    }

    ImGui::SameLine();
    {
        const bool looping = player->is_looping();
        if (looping) {
            if (icon_button(ICON_MDI_REPEAT, "Looping - press to play once")) {
                player->set_looping(false);
            }
        } else {
            if (icon_button(ICON_MDI_REPEAT_OFF, "Play once - press to loop")) {
                player->set_looping(true);
            }
        }
    }

    ImGui::SameLine();
    {
        float speed = player->get_speed();
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::DragFloat("##speed", &speed, 0.01f, -100.0f, 100.0f, "speed %.2fx", ImGuiSliderFlags_NoRoundToFormat)) {
            player->set_speed(speed);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Playback speed; negative plays backwards");
        }
    }

    ImGui::SameLine();
    {
        float time = player->get_time();
        ImGui::SetNextItemWidth(130.0f);
        if (ImGui::DragFloat("##time", &time, 0.01f, player->get_start_time(), player->get_end_time(), "t = %.3f s", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_AlwaysClamp)) {
            player->seek(time);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Current time (drag to scrub)");
        }
    }

    ImGui::SameLine();
    if (icon_button(ICON_MDI_FIT_TO_PAGE_OUTLINE, "Frame visible curves (Home)")) {
        m_frame_all_queued = true;
    }
}

void Animation_window::keying_toolbar()
{
    Animation_player* player = m_context.animation_player;
    if (player == nullptr) {
        return;
    }

    // Autokey mode (3ds-Max-style red tint while armed)
    ImGui::TextUnformatted("Autokey");
    ImGui::SameLine();
    {
        const Autokey_mode mode = player->get_autokey_mode();
        const bool armed = mode != Autokey_mode::off;
        if (armed) {
            ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4{0.55f, 0.15f, 0.15f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4{0.65f, 0.20f, 0.20f, 1.0f});
        }
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::BeginCombo("##autokey", c_str(mode))) {
            const Autokey_mode modes[3] = {Autokey_mode::off, Autokey_mode::modified_paths, Autokey_mode::all_paths};
            for (const Autokey_mode candidate : modes) {
                if (ImGui::Selectable(c_str(candidate), candidate == mode)) {
                    player->set_autokey_mode(candidate);
                }
            }
            ImGui::EndCombo();
        }
        if (armed) {
            ImGui::PopStyleColor(2);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Autokey: key edited nodes at the current time when a transform edit ends.\n"
                "Modified keys only the changed paths (move -> translation); All keys T, R and S.\n"
                "Requires a targeted animation."
            );
        }
    }

    // Manual Create / Delete Key with LightWave-dialog-style path toggles
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::Checkbox("T##key_t", &m_key_translation);
    ImGui::SameLine();
    ImGui::Checkbox("R##key_r", &m_key_rotation);
    ImGui::SameLine();
    ImGui::Checkbox("S##key_s", &m_key_scale);
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Key")) {
        create_key_for_selection();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Create keys for the selected objects at the current time on the checked paths (T / R / S).\n"
            "Missing channels are created; with no animation targeted, a new animation is created."
        );
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("- Key")) {
        delete_key_for_selection();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Delete the selected objects' keys at the current time");
    }

    // Key marker source for the timeline strip
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::TextUnformatted("Marks");
    ImGui::SameLine();
    {
        const char* source_names[3] = {"Selected Objects", "Active Animation", "Shown Channels"};
        int source = static_cast<int>(m_key_marker_source);
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::Combo("##marker_source", &source, source_names, 3)) {
            m_key_marker_source = static_cast<Key_marker_source>(source);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Which keys the timeline strip marks");
        }
    }
}

void Animation_window::collect_key_marker_times()
{
    m_marker_times.clear();
    if (!m_animation) {
        return;
    }
    const erhe::scene::Animation& animation = *m_animation.get();

    // Scratch for the selected-objects source
    static thread_local std::vector<const erhe::scene::Node*> selected_nodes;
    selected_nodes.clear();
    if ((m_key_marker_source == Key_marker_source::selected_objects) && (m_context.selection != nullptr)) {
        for (const std::shared_ptr<erhe::Item_base>& item : m_context.selection->get_selected_items()) {
            const erhe::scene::Node* node = dynamic_cast<const erhe::scene::Node*>(item.get());
            if (node != nullptr) {
                selected_nodes.push_back(node);
            }
        }
    }

    for (std::size_t channel_index = 0; channel_index < animation.channels.size(); ++channel_index) {
        const erhe::scene::Animation_channel& channel = animation.channels[channel_index];
        const std::size_t component_count = erhe::scene::get_component_count(channel.path);
        bool include = false;
        switch (m_key_marker_source) {
            case Key_marker_source::selected_objects: {
                include = std::find(selected_nodes.begin(), selected_nodes.end(), channel.target.get()) != selected_nodes.end();
                break;
            }
            case Key_marker_source::active_animation: {
                include = true;
                break;
            }
            case Key_marker_source::shown_channels: {
                const uint32_t full_mask = (component_count > 0) ? ((1u << component_count) - 1u) : 0u;
                include = (channel_index < m_channel_visibility.size()) && ((m_channel_visibility[channel_index] & full_mask) != 0u);
                break;
            }
            default: {
                break;
            }
        }
        if (!include) {
            continue;
        }
        const erhe::scene::Animation_sampler& sampler = animation.samplers.at(channel.sampler_index);
        m_marker_times.insert(m_marker_times.end(), sampler.timestamps.begin(), sampler.timestamps.end());
    }
    std::sort(m_marker_times.begin(), m_marker_times.end());
}

void Animation_window::timeline_strip()
{
    Animation_player* player = m_context.animation_player;
    if (player == nullptr) {
        return;
    }

    const ImGuiIO&    io        = ImGui::GetIO();
    ImDrawList* const draw_list = ImGui::GetWindowDrawList();

    const float height      = ImGui::GetFrameHeight() * 1.4f;
    const float field_width = ImGui::GetFontSize() * 3.5f;

    // Editable timeline range (LightWave-style): start field, strip, end field.
    {
        float start_time = player->get_start_time();
        ImGui::SetNextItemWidth(field_width);
        if (ImGui::DragFloat("##timeline_start", &start_time, 0.01f, 0.0f, 0.0f, "%.2f", ImGuiSliderFlags_NoRoundToFormat)) {
            player->set_start_time(start_time);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Timeline start time");
        }
        ImGui::SameLine();
    }

    const ImVec2 pos  = ImGui::GetCursorScreenPos();
    ImVec2       size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x - (field_width + ImGui::GetStyle().ItemSpacing.x), 50.0f);
    size.y = height;
    const ImVec2 max{pos.x + size.x, pos.y + size.y};

    float range_min = player->get_start_time();
    float range_max = player->get_end_time();
    if (!(range_max > range_min)) {
        range_max = range_min + 1.0f;
    }
    const float margin = (range_max - range_min) * 0.02f;
    range_min -= margin;
    range_max += margin;

    const auto strip_time_to_x = [&](const float time) -> float {
        return pos.x + ((time - range_min) / (range_max - range_min)) * size.x;
    };
    const auto strip_x_to_time = [&](const float x) -> float {
        return range_min + ((x - pos.x) / std::max(size.x, 1.0f)) * (range_max - range_min);
    };

    ImGui::InvisibleButton("timeline_strip", size, ImGuiButtonFlags_MouseButtonLeft);
    const bool active  = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();

    // Moving the timeline: click / drag anywhere on the strip scrubs.
    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        player->seek(strip_x_to_time(io.MousePos.x));
        m_strip_scrubbing = true;
    } else {
        m_strip_scrubbing = false;
    }
    if (hovered || active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    draw_list->PushClipRect(pos, max, true);

    const ImGuiStyle& style = ImGui::GetStyle();
    draw_list->AddRectFilled(pos, max, ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);

    // Time ticks + labels
    {
        const ImU32 grid_color  = ImGui::GetColorU32(ImGuiCol_Separator, 0.6f);
        const ImU32 label_color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const float step        = nice_step((range_max - range_min) * 80.0f / size.x);
        const float first_tick  = std::ceil(range_min / step) * step;
        for (float t = first_tick; t <= range_max; t += step) {
            const float x = strip_time_to_x(t);
            draw_list->AddLine(ImVec2{x, pos.y}, ImVec2{x, max.y}, grid_color);
            char label[32];
            snprintf(label, sizeof(label), "%g", static_cast<double>(t));
            draw_list->AddText(ImVec2{x + 3.0f, pos.y + 1.0f}, label_color, label);
        }
    }

    // Key markers (LightWave frame-slider style pale yellow lines)
    collect_key_marker_times();
    {
        const ImU32 marker_color = IM_COL32(235, 220, 120, 210);
        const float marker_top   = pos.y + (size.y * 0.35f);
        for (const float time : m_marker_times) {
            const float x = strip_time_to_x(time);
            draw_list->AddLine(ImVec2{x, marker_top}, ImVec2{x, max.y - 1.0f}, marker_color, 1.5f);
        }
    }

    // Playhead with a grab-handle knob (LightWave frame-slider style: the
    // knob shows the current time and is the drag affordance; clicking /
    // dragging anywhere on the strip scrubs too). Drawn regardless of whether
    // an animation is active: the current time matters even without one
    // (Create Key / autokey key at the current time).
    {
        const float time = player->get_time();
        const float x    = strip_time_to_x(time);
        const ImU32 playhead_color = IM_COL32(90, 200, 220, 255);
        draw_list->AddLine(ImVec2{x, pos.y}, ImVec2{x, max.y}, playhead_color, 2.0f);

        char label[32];
        snprintf(label, sizeof(label), "%.3f", static_cast<double>(time));
        const ImVec2 text_size  = ImGui::CalcTextSize(label);
        const float  knob_width = text_size.x + 12.0f;
        float knob_left = x - (knob_width * 0.5f);
        knob_left = std::clamp(knob_left, pos.x, max.x - knob_width);
        const ImVec2 knob_min{knob_left, pos.y + 1.0f};
        const ImVec2 knob_max{knob_left + knob_width, max.y - 1.0f};
        const ImU32 knob_color = ImGui::GetColorU32(
            (active || m_strip_scrubbing) ? ImGuiCol_SliderGrabActive : (hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button)
        );
        draw_list->AddRectFilled(knob_min, knob_max, knob_color, style.FrameRounding);
        draw_list->AddRect      (knob_min, knob_max, playhead_color, style.FrameRounding);
        draw_list->AddText(
            ImVec2{
                knob_left + ((knob_width - text_size.x) * 0.5f),
                pos.y + ((size.y - text_size.y) * 0.5f)
            },
            ImGui::GetColorU32(ImGuiCol_Text),
            label
        );
    }

    draw_list->PopClipRect();

    if ((hovered || m_strip_scrubbing) && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        ImGui::SetTooltip("t = %.3f", static_cast<double>(strip_x_to_time(io.MousePos.x)));
    }

    ImGui::SameLine();
    {
        float end_time = player->get_end_time();
        ImGui::SetNextItemWidth(field_width);
        if (ImGui::DragFloat("##timeline_end", &end_time, 0.01f, 0.0f, 0.0f, "%.2f", ImGuiSliderFlags_NoRoundToFormat)) {
            player->set_end_time(end_time);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Timeline end time");
        }
    }
}

void Animation_window::collect_selected_nodes(std::vector<std::shared_ptr<erhe::scene::Node>>& out_nodes) const
{
    out_nodes.clear();
    if (m_context.selection == nullptr) {
        return;
    }
    for (const std::shared_ptr<erhe::Item_base>& item : m_context.selection->get_selected_items()) {
        std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        if (node) {
            out_nodes.push_back(std::move(node));
        }
    }
}

void Animation_window::create_key_for_selection()
{
    Animation_player* player = m_context.animation_player;
    if ((player == nullptr) || (m_context.operation_stack == nullptr)) {
        return;
    }
    std::vector<std::shared_ptr<erhe::scene::Node>> nodes;
    collect_selected_nodes(nodes);
    if (nodes.empty()) {
        return;
    }

    Compound_operation::Parameters compound_parameters;

    // No animation targeted yet: create one in the scene's content library
    // (Unity-record-button style), undoably, and target it.
    std::shared_ptr<erhe::scene::Animation> animation = m_animation;
    if (!animation) {
        erhe::scene::Scene* scene = nodes.front()->get_scene();
        Scene_root* scene_root = (scene != nullptr) ? static_cast<Scene_root*>(scene->get_item_host()) : nullptr;
        const std::shared_ptr<Content_library> library = (scene_root != nullptr) ? scene_root->get_content_library() : std::shared_ptr<Content_library>{};
        if (!library || !library->animations) {
            return;
        }
        animation = std::make_shared<erhe::scene::Animation>("Animation");
        animation->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
        compound_parameters.operations.push_back(
            std::make_shared<Content_library_attach_operation<erhe::scene::Animation>>(
                library,
                library->animations,
                animation,
                Gltf_source_reference{
                    .item_name = animation->get_name(),
                    .item_type = "animation"
                }
            )
        );
    }

    std::vector<Keying_request> requests;
    requests.reserve(nodes.size());
    for (const std::shared_ptr<erhe::scene::Node>& node : nodes) {
        requests.push_back(
            Keying_request{
                .node            = node,
                .key_translation = m_key_translation,
                .key_rotation    = m_key_rotation,
                .key_scale       = m_key_scale
            }
        );
    }
    std::shared_ptr<Operation> keying_operation = key_nodes(animation, requests, player->get_time());
    if (!keying_operation) {
        return;
    }
    compound_parameters.operations.push_back(std::move(keying_operation));

    if (compound_parameters.operations.size() == 1) {
        m_context.operation_stack->queue(compound_parameters.operations.front());
    } else {
        m_context.operation_stack->queue(std::make_shared<Compound_operation>(std::move(compound_parameters)));
    }

    if (animation != m_animation) {
        set_animation(animation);
    }
    player->on_animation_edited(animation);
}

void Animation_window::delete_key_for_selection()
{
    Animation_player* player = m_context.animation_player;
    if ((player == nullptr) || (m_context.operation_stack == nullptr) || !m_animation) {
        return;
    }
    std::vector<std::shared_ptr<erhe::scene::Node>> nodes;
    collect_selected_nodes(nodes);
    if (nodes.empty()) {
        return;
    }
    std::shared_ptr<Operation> operation = delete_keys(m_animation, nodes, player->get_time());
    if (!operation) {
        return;
    }
    m_context.operation_stack->queue(std::move(operation));
    player->on_animation_edited(m_animation);
}

auto Animation_window::channel_label(const std::size_t channel_index) const -> std::string
{
    const erhe::scene::Animation_channel& channel = m_animation->channels.at(channel_index);
    return fmt::format(
        "{} {}",
        channel.target ? channel.target->get_name() : "(no target)",
        erhe::scene::c_str(channel.path)
    );
}

void Animation_window::channel_row(const std::size_t channel_index)
{
    erhe::scene::Animation& animation = *m_animation.get();
    const erhe::scene::Animation_channel& channel = animation.channels[channel_index];
    const std::size_t component_count = erhe::scene::get_component_count(channel.path);

    ImGui::PushID(static_cast<int>(channel_index));

    // Tri-state channel checkbox: checked = all components visible,
    // unchecked = none, mixed (dash) = some. Clicking a mixed checkbox
    // turns all components on.
    const uint32_t full_mask   = (1u << component_count) - 1u;
    const uint32_t masked      = m_channel_visibility[channel_index] & full_mask;
    const bool     any_visible = masked != 0u;
    const bool     all_visible = masked == full_mask;
    const bool     mixed       = any_visible && !all_visible;
    bool checked = any_visible;
    if (mixed) {
        ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
    }
    const bool checkbox_changed = ImGui::Checkbox("##visible", &checked);
    if (mixed) {
        ImGui::PopItemFlag();
    }
    if (checkbox_changed) {
        m_channel_visibility[channel_index] = (mixed || checked) ? 0xffffffffu : 0u;
    }
    ImGui::SameLine();

    const std::string label = channel_label(channel_index);
    const bool open    = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::BeginPopupContextItem("channel_context")) {
        const bool can_select = channel.target && (m_context.selection != nullptr);
        if (ImGui::MenuItem("Select Target Node", nullptr, false, can_select)) {
            m_context.selection->set_selection({channel.target});
        }
        if (ImGui::MenuItem("Add Target Node to Selection", nullptr, false, can_select)) {
            m_context.selection->add_to_selection(channel.target);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Show", nullptr, false, !all_visible)) {
            m_channel_visibility[channel_index] = 0xffffffffu;
        }
        if (ImGui::MenuItem("Hide", nullptr, false, any_visible)) {
            m_channel_visibility[channel_index] = 0u;
        }
        ImGui::EndPopup();
    } else if (hovered) {
        const erhe::scene::Animation_sampler& sampler = animation.samplers.at(channel.sampler_index);
        ImGui::SetTooltip(
            "%s interpolation, %d keys",
            erhe::scene::c_str(sampler.interpolation_mode),
            static_cast<int>(sampler.timestamps.size())
        );
    }
    if (open) {
        for (std::size_t component = 0; component < component_count; ++component) {
            ImGui::PushID(static_cast<int>(component));
            bool visible = is_component_visible(channel_index, component);
            if (ImGui::Checkbox("##component_visible", &visible)) {
                if (visible) {
                    m_channel_visibility[channel_index] |= (1u << component);
                } else {
                    m_channel_visibility[channel_index] &= ~(1u << component);
                }
            }
            ImGui::SameLine();
            const ImU32  color = get_component_color(component);
            const ImVec4 color_v = ImGui::ColorConvertU32ToFloat4(color);
            ImGui::TextColored(color_v, "%s", c_component_names[component]);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

auto Animation_window::channel_passes_filter(const std::size_t channel_index) const -> bool
{
    if (!m_channel_filter.IsActive()) {
        return true;
    }
    return m_channel_filter.PassFilter(channel_label(channel_index).c_str());
}

void Animation_window::channel_list_pane()
{
    if (!m_animation) {
        ImGui::TextUnformatted("No animation selected");
        return;
    }
    erhe::scene::Animation& animation = *m_animation.get();

    ImGui::PushID("all_channels");
    if (ImGui::TreeNodeEx("Channels", ImGuiTreeNodeFlags_DefaultOpen)) {
        m_channel_filter.Draw("##channel_filter", -FLT_MIN);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Filter channels by name (e.g. 'arm', 'Rotation'); All / None / Animated apply to the filtered channels");
        }

        // All / None / Animated operate on the channels that pass the filter.
        if (ImGui::SmallButton("All")) {
            for (std::size_t channel_index = 0; channel_index < animation.channels.size(); ++channel_index) {
                if (channel_passes_filter(channel_index)) {
                    m_channel_visibility[channel_index] = 0xffffffffu;
                }
            }
            m_frame_all_queued = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("None")) {
            for (std::size_t channel_index = 0; channel_index < animation.channels.size(); ++channel_index) {
                if (channel_passes_filter(channel_index)) {
                    m_channel_visibility[channel_index] = 0u;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Animated")) {
            for (std::size_t channel_index = 0; channel_index < animation.channels.size(); ++channel_index) {
                if (!channel_passes_filter(channel_index)) {
                    continue;
                }
                const std::size_t component_count = erhe::scene::get_component_count(animation.channels[channel_index].path);
                uint32_t mask = 0u;
                for (std::size_t component = 0; component < component_count; ++component) {
                    if (is_component_animated(animation, channel_index, component)) {
                        mask |= (1u << component);
                    }
                }
                m_channel_visibility[channel_index] = mask;
            }
            m_frame_all_queued = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Show only curves that actually change over time; constant curves are hidden");
        }

        for (std::size_t channel_index = 0; channel_index < animation.channels.size(); ++channel_index) {
            if (erhe::scene::get_component_count(animation.channels[channel_index].path) == 0) {
                continue; // WEIGHTS - not shown (no curve support yet)
            }
            if (!channel_passes_filter(channel_index)) {
                continue;
            }
            channel_row(channel_index);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();

    ImGui::Separator();

    ImGui::PushID("shown_channels");
    if (ImGui::TreeNodeEx("Shown Channels", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool any_shown = false;
        for (std::size_t channel_index = 0; channel_index < animation.channels.size(); ++channel_index) {
            const std::size_t component_count = erhe::scene::get_component_count(animation.channels[channel_index].path);
            if (component_count == 0) {
                continue;
            }
            const uint32_t full_mask = (1u << component_count) - 1u;
            if ((m_channel_visibility[channel_index] & full_mask) == 0u) {
                continue;
            }
            any_shown = true;
            channel_row(channel_index);
        }
        if (!any_shown) {
            ImGui::TextDisabled("(no channels shown)");
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void Animation_window::apply_key_drag(const float time_delta, const float value_delta)
{
    if (!m_animation) {
        return;
    }
    erhe::scene::Animation& animation = *m_animation.get();

    // Values: each selected point moves its own component.
    for (std::size_t i = 0; i < m_selection.size(); ++i) {
        const Curve_point& point = m_selection[i];
        set_keyframe_value(animation, point.channel_index, point.key_index, point.component, m_drag_initial[i].y + value_delta);
    }

    // Times: each underlying sampler key moves once, even when several of its
    // component points are selected.
    for (std::size_t i = 0; i < m_selection.size(); ++i) {
        const Curve_point& point   = m_selection[i];
        const std::size_t  sampler = animation.channels.at(point.channel_index).sampler_index;
        bool first_for_key = true;
        for (std::size_t j = 0; j < i; ++j) {
            const Curve_point& other = m_selection[j];
            if ((animation.channels.at(other.channel_index).sampler_index == sampler) && (other.key_index == point.key_index)) {
                first_for_key = false;
                break;
            }
        }
        if (first_for_key) {
            static_cast<void>(set_keyframe_time(animation, sampler, point.key_index, m_drag_initial[i].x + time_delta));
        }
    }

    if (m_context.animation_player != nullptr) {
        m_context.animation_player->on_animation_edited(m_animation);
    }
}

void Animation_window::finish_key_drag(const bool moved)
{
    if (moved && m_animation && !m_edit_before.empty() && (m_context.operation_stack != nullptr)) {
        std::vector<Animation_sampler_state> after;
        after.reserve(m_edit_before.size());
        for (const Animation_sampler_state& state : m_edit_before) {
            after.push_back(capture_sampler_state(*m_animation.get(), state.sampler_index));
        }
        m_context.operation_stack->queue(
            std::make_shared<Animation_edit_operation>(
                "Move keyframes",
                m_animation,
                std::move(m_edit_before),
                std::move(after)
            )
        );
    }
    m_edit_before.clear();
}

void Animation_window::delete_selected_keys()
{
    if (!m_animation || m_selection.empty() || (m_context.operation_stack == nullptr)) {
        return;
    }
    erhe::scene::Animation& animation = *m_animation.get();

    // Unique (sampler, key) pairs, sorted by key descending so deletion does
    // not shift the not-yet-deleted indices.
    std::vector<std::pair<std::size_t, std::size_t>> keys;
    keys.reserve(m_selection.size());
    for (const Curve_point& point : m_selection) {
        const std::size_t sampler = animation.channels.at(point.channel_index).sampler_index;
        const std::pair<std::size_t, std::size_t> entry{sampler, point.key_index};
        if (std::find(keys.begin(), keys.end(), entry) == keys.end()) {
            keys.push_back(entry);
        }
    }
    std::sort(
        keys.begin(),
        keys.end(),
        [](const std::pair<std::size_t, std::size_t>& lhs, const std::pair<std::size_t, std::size_t>& rhs) {
            return (lhs.first != rhs.first) ? (lhs.first < rhs.first) : (lhs.second > rhs.second);
        }
    );

    std::vector<Animation_sampler_state> before;
    for (const std::pair<std::size_t, std::size_t>& entry : keys) {
        const bool captured = std::find_if(
            before.begin(),
            before.end(),
            [&entry](const Animation_sampler_state& state) { return state.sampler_index == entry.first; }
        ) != before.end();
        if (!captured) {
            before.push_back(capture_sampler_state(animation, entry.first));
        }
    }

    bool any_deleted = false;
    for (const std::pair<std::size_t, std::size_t>& entry : keys) {
        any_deleted = delete_keyframe(animation, entry.first, entry.second) || any_deleted;
    }

    m_selection.clear();
    if (!any_deleted) {
        return;
    }

    std::vector<Animation_sampler_state> after;
    after.reserve(before.size());
    for (const Animation_sampler_state& state : before) {
        after.push_back(capture_sampler_state(animation, state.sampler_index));
    }
    m_context.operation_stack->queue(
        std::make_shared<Animation_edit_operation>(
            "Delete keyframes",
            m_animation,
            std::move(before),
            std::move(after)
        )
    );
    if (m_context.animation_player != nullptr) {
        m_context.animation_player->on_animation_edited(m_animation);
    }
}

void Animation_window::insert_key_at(const float time, const ImVec2 mouse_px)
{
    if (!m_animation || (m_context.operation_stack == nullptr)) {
        return;
    }
    std::size_t channel_index = 0;
    std::size_t component     = 0;
    if (!find_curve_near(mouse_px, c_curve_hit_radius_px, channel_index, component)) {
        return;
    }
    erhe::scene::Animation& animation = *m_animation.get();
    const std::size_t sampler_index = animation.channels.at(channel_index).sampler_index;

    std::vector<Animation_sampler_state> before;
    before.push_back(capture_sampler_state(animation, sampler_index));
    const std::size_t key_index = insert_keyframe(animation, sampler_index, time);
    std::vector<Animation_sampler_state> after;
    after.push_back(capture_sampler_state(animation, sampler_index));

    m_context.operation_stack->queue(
        std::make_shared<Animation_edit_operation>(
            "Insert keyframe",
            m_animation,
            std::move(before),
            std::move(after)
        )
    );

    m_selection.clear();
    m_selection.push_back(Curve_point{.channel_index = channel_index, .component = component, .key_index = key_index});
    if (m_context.animation_player != nullptr) {
        m_context.animation_player->on_animation_edited(m_animation);
    }
}

auto Animation_window::find_curve_near(
    const ImVec2 mouse_px,
    const float  max_distance_px,
    std::size_t& out_channel_index,
    std::size_t& out_component
) const -> bool
{
    if (!m_animation) {
        return false;
    }
    erhe::scene::Animation& animation = *m_animation.get();
    const float time = x_to_time(mouse_px.x);

    float best_distance = max_distance_px;
    bool  found         = false;
    for (std::size_t channel_index = 0; channel_index < animation.channels.size(); ++channel_index) {
        const erhe::scene::Animation_channel& channel = animation.channels[channel_index];
        const erhe::scene::Animation_sampler& sampler = animation.samplers.at(channel.sampler_index);
        const std::size_t component_count = erhe::scene::get_component_count(channel.path);
        if (sampler.timestamps.empty() || (component_count == 0)) {
            continue;
        }
        if ((time < sampler.timestamps.front()) || (time > sampler.timestamps.back())) {
            continue;
        }
        for (std::size_t component = 0; component < component_count; ++component) {
            if (!is_component_visible(channel_index, component)) {
                continue;
            }
            const float value    = animation.evaluate(time, channel_index, component);
            const float y        = value_to_y(value);
            const float distance = std::abs(y - mouse_px.y);
            if (distance < best_distance) {
                best_distance     = distance;
                out_channel_index = channel_index;
                out_component     = component;
                found             = true;
            }
        }
    }
    return found;
}

void Animation_window::curve_canvas()
{
    const ImGuiIO&    io        = ImGui::GetIO();
    ImDrawList* const draw_list = ImGui::GetWindowDrawList();

    const ImVec2 canvas_pos  = ImGui::GetCursorScreenPos();
    ImVec2       canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.x = std::max(canvas_size.x, 50.0f);
    canvas_size.y = std::max(canvas_size.y, 50.0f);
    m_canvas_min = canvas_pos;
    m_canvas_max = ImVec2{canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y};

    if (m_frame_all_queued) {
        compute_frame_all_view(canvas_size.y);
        m_frame_all_queued = false;
    }

    ImGui::InvisibleButton(
        "curve_canvas",
        canvas_size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight
    );
    const bool canvas_hovered = ImGui::IsItemHovered();
    const bool canvas_active  = ImGui::IsItemActive();

    // -----------------------------------------------------------------
    // Input: pan / zoom
    // -----------------------------------------------------------------
    if (canvas_active && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right))) {
        const float time_per_px  = (m_view_time_max - m_view_time_min) / std::max(canvas_size.x, 1.0f);
        const float value_per_px = (m_view_value_max - m_view_value_min) / std::max(canvas_size.y - m_ruler_height, 1.0f);
        m_view_time_min  -= io.MouseDelta.x * time_per_px;
        m_view_time_max  -= io.MouseDelta.x * time_per_px;
        m_view_value_min += io.MouseDelta.y * value_per_px;
        m_view_value_max += io.MouseDelta.y * value_per_px;
    }

    if (canvas_hovered && (io.MouseWheel != 0.0f)) {
        const float zoom = std::pow(1.15f, -io.MouseWheel);
        const bool  zoom_time  = !io.KeyAlt;
        const bool  zoom_value = !io.KeyCtrl;
        if (zoom_time) {
            const float pivot = x_to_time(io.MousePos.x);
            m_view_time_min = pivot + (m_view_time_min - pivot) * zoom;
            m_view_time_max = pivot + (m_view_time_max - pivot) * zoom;
        }
        if (zoom_value) {
            const float pivot = y_to_value(io.MousePos.y);
            m_view_value_min = pivot + (m_view_value_min - pivot) * zoom;
            m_view_value_max = pivot + (m_view_value_max - pivot) * zoom;
        }
    }

    if ((canvas_hovered || ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) && ImGui::IsKeyPressed(ImGuiKey_Home)) {
        compute_frame_all_view(canvas_size.y);
    }

    // -----------------------------------------------------------------
    // Build visible key points (pixel positions reflect this frame's view)
    // -----------------------------------------------------------------
    m_visible_points.clear();
    m_visible_point_px.clear();
    if (m_animation) {
        erhe::scene::Animation& animation = *m_animation.get();
        for (std::size_t channel_index = 0; channel_index < animation.channels.size(); ++channel_index) {
            const erhe::scene::Animation_channel& channel = animation.channels[channel_index];
            const erhe::scene::Animation_sampler& sampler = animation.samplers.at(channel.sampler_index);
            const std::size_t component_count = erhe::scene::get_component_count(channel.path);
            for (std::size_t component = 0; component < component_count; ++component) {
                if (!is_component_visible(channel_index, component)) {
                    continue;
                }
                for (std::size_t key = 0; key < sampler.timestamps.size(); ++key) {
                    const float time  = sampler.timestamps[key];
                    const float value = get_keyframe_value(animation, channel_index, key, component);
                    m_visible_points.push_back(Curve_point{.channel_index = channel_index, .component = component, .key_index = key});
                    m_visible_point_px.push_back(ImVec2{time_to_x(time), value_to_y(value)});
                }
            }
        }
    }

    // Hovered key
    m_hovered_point = -1;
    if (canvas_hovered && (m_drag_mode == Drag_mode::none)) {
        float best = c_key_hit_radius_px;
        for (std::size_t i = 0; i < m_visible_point_px.size(); ++i) {
            const ImVec2 p = m_visible_point_px[i];
            const float distance = std::max(std::abs(p.x - io.MousePos.x), std::abs(p.y - io.MousePos.y));
            if (distance <= best) {
                best = distance;
                m_hovered_point = static_cast<int>(i);
            }
        }
    }

    // -----------------------------------------------------------------
    // Input: left button state machine (scrub / select / drag / insert)
    // -----------------------------------------------------------------
    const bool in_ruler = canvas_hovered && (io.MousePos.y <= m_canvas_min.y + m_ruler_height);
    if (canvas_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (in_ruler) {
            m_drag_mode = Drag_mode::scrub;
            if (m_context.animation_player != nullptr) {
                m_context.animation_player->seek(x_to_time(io.MousePos.x));
            }
        } else if (m_hovered_point >= 0) {
            const Curve_point pressed = m_visible_points[static_cast<std::size_t>(m_hovered_point)];
            m_pressed_point              = pressed;
            m_pressed_point_was_selected = is_point_selected(pressed);
            if (!m_pressed_point_was_selected) {
                if (!io.KeyCtrl) {
                    m_selection.clear();
                }
                m_selection.push_back(pressed);
            }
            // Record initial (time, value) per selected point and snapshot
            // the affected samplers for undo.
            m_drag_initial.clear();
            m_edit_before.clear();
            if (m_animation) {
                erhe::scene::Animation& animation = *m_animation.get();
                for (const Curve_point& point : m_selection) {
                    const erhe::scene::Animation_channel& channel = animation.channels.at(point.channel_index);
                    const erhe::scene::Animation_sampler& sampler = animation.samplers.at(channel.sampler_index);
                    m_drag_initial.push_back(
                        ImVec2{
                            sampler.timestamps.at(point.key_index),
                            get_keyframe_value(animation, point.channel_index, point.key_index, point.component)
                        }
                    );
                    const bool captured = std::find_if(
                        m_edit_before.begin(),
                        m_edit_before.end(),
                        [&channel](const Animation_sampler_state& state) { return state.sampler_index == channel.sampler_index; }
                    ) != m_edit_before.end();
                    if (!captured) {
                        m_edit_before.push_back(capture_sampler_state(animation, channel.sampler_index));
                    }
                }
            }
            m_drag_mode     = Drag_mode::move_keys;
            m_drag_start_px = io.MousePos;
            m_drag_moved    = false;
        } else if (io.KeyCtrl) {
            insert_key_at(x_to_time(io.MousePos.x), io.MousePos);
        } else {
            m_drag_mode     = Drag_mode::box_select;
            m_drag_start_px = io.MousePos;
        }
    }

    if (m_drag_mode == Drag_mode::scrub) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (m_context.animation_player != nullptr) {
                m_context.animation_player->seek(x_to_time(io.MousePos.x));
            }
        } else {
            m_drag_mode = Drag_mode::none;
        }
    }

    if (m_drag_mode == Drag_mode::move_keys) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ImVec2 delta_px{io.MousePos.x - m_drag_start_px.x, io.MousePos.y - m_drag_start_px.y};
            if (!m_drag_moved && ((std::abs(delta_px.x) > c_drag_threshold_px) || (std::abs(delta_px.y) > c_drag_threshold_px))) {
                m_drag_moved = true;
            }
            if (m_drag_moved) {
                const float time_per_px  = (m_view_time_max - m_view_time_min) / std::max(canvas_size.x, 1.0f);
                const float value_per_px = (m_view_value_max - m_view_value_min) / std::max(canvas_size.y - m_ruler_height, 1.0f);
                apply_key_drag(delta_px.x * time_per_px, -delta_px.y * value_per_px);
            }
        } else {
            if (!m_drag_moved) {
                // Plain click on a key: resolve selection.
                if (io.KeyCtrl && m_pressed_point_was_selected) {
                    m_selection.erase(std::remove(m_selection.begin(), m_selection.end(), m_pressed_point), m_selection.end());
                } else if (!io.KeyCtrl) {
                    m_selection.clear();
                    m_selection.push_back(m_pressed_point);
                }
            }
            finish_key_drag(m_drag_moved);
            m_drag_mode = Drag_mode::none;
        }
    }

    if (m_drag_mode == Drag_mode::box_select) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ImVec2 box_min{std::min(m_drag_start_px.x, io.MousePos.x), std::min(m_drag_start_px.y, io.MousePos.y)};
            const ImVec2 box_max{std::max(m_drag_start_px.x, io.MousePos.x), std::max(m_drag_start_px.y, io.MousePos.y)};
            if (!io.KeyCtrl) {
                m_selection.clear();
            }
            for (std::size_t i = 0; i < m_visible_points.size(); ++i) {
                const ImVec2 p = m_visible_point_px[i];
                const bool inside =
                    (p.x >= box_min.x) && (p.x <= box_max.x) &&
                    (p.y >= box_min.y) && (p.y <= box_max.y);
                if (inside && !is_point_selected(m_visible_points[i])) {
                    m_selection.push_back(m_visible_points[i]);
                }
            }
            m_drag_mode = Drag_mode::none;
        }
    }

    // Delete selected keyframes
    if (
        (canvas_hovered || ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) &&
        !m_selection.empty() &&
        (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_X))
    ) {
        delete_selected_keys();
        // Selection / key indices changed; visible points are rebuilt next frame.
        m_visible_points.clear();
        m_visible_point_px.clear();
        m_hovered_point = -1;
    }

    // -----------------------------------------------------------------
    // Draw
    // -----------------------------------------------------------------
    draw_list->PushClipRect(m_canvas_min, m_canvas_max, true);

    const ImGuiStyle& style = ImGui::GetStyle();
    draw_list->AddRectFilled(m_canvas_min, m_canvas_max, ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);

    const float curve_top = m_canvas_min.y + m_ruler_height;

    // Grid + labels
    {
        const ImU32 grid_color  = ImGui::GetColorU32(ImGuiCol_Separator, 0.5f);
        const ImU32 axis_color  = ImGui::GetColorU32(ImGuiCol_SeparatorActive, 0.75f);
        const ImU32 label_color = ImGui::GetColorU32(ImGuiCol_TextDisabled);

        const float time_step = nice_step((m_view_time_max - m_view_time_min) * 80.0f / std::max(canvas_size.x, 1.0f));
        const float first_time_tick = std::ceil(m_view_time_min / time_step) * time_step;
        for (float t = first_time_tick; t <= m_view_time_max; t += time_step) {
            const float x = time_to_x(t);
            draw_list->AddLine(ImVec2{x, curve_top}, ImVec2{x, m_canvas_max.y}, grid_color);
            char label[32];
            snprintf(label, sizeof(label), "%g", static_cast<double>(t));
            draw_list->AddText(ImVec2{x + 3.0f, m_canvas_min.y + 3.0f}, label_color, label);
        }

        const float value_step = nice_step((m_view_value_max - m_view_value_min) * 40.0f / std::max(canvas_size.y - m_ruler_height, 1.0f));
        const float first_value_tick = std::ceil(m_view_value_min / value_step) * value_step;
        for (float v = first_value_tick; v <= m_view_value_max; v += value_step) {
            const float y = value_to_y(v);
            if (y < curve_top) {
                continue;
            }
            draw_list->AddLine(ImVec2{m_canvas_min.x, y}, ImVec2{m_canvas_max.x, y}, grid_color);
            char label[32];
            snprintf(label, sizeof(label), "%g", static_cast<double>(v));
            draw_list->AddText(ImVec2{m_canvas_min.x + 3.0f, y + 2.0f}, label_color, label);
        }

        // Zero axes
        if ((0.0f >= m_view_time_min) && (0.0f <= m_view_time_max)) {
            const float x = time_to_x(0.0f);
            draw_list->AddLine(ImVec2{x, curve_top}, ImVec2{x, m_canvas_max.y}, axis_color);
        }
        if ((0.0f >= m_view_value_min) && (0.0f <= m_view_value_max)) {
            const float y = value_to_y(0.0f);
            if (y >= curve_top) {
                draw_list->AddLine(ImVec2{m_canvas_min.x, y}, ImVec2{m_canvas_max.x, y}, axis_color);
            }
        }

        // Ruler strip separator
        draw_list->AddLine(ImVec2{m_canvas_min.x, curve_top}, ImVec2{m_canvas_max.x, curve_top}, axis_color);
    }

    // Curves
    if (m_animation) {
        erhe::scene::Animation& animation = *m_animation.get();
        for (std::size_t channel_index = 0; channel_index < animation.channels.size(); ++channel_index) {
            const erhe::scene::Animation_channel& channel = animation.channels[channel_index];
            const erhe::scene::Animation_sampler& sampler = animation.samplers.at(channel.sampler_index);
            const std::size_t component_count = erhe::scene::get_component_count(channel.path);
            if (sampler.timestamps.empty() || (component_count == 0)) {
                continue;
            }
            const std::size_t key_count = sampler.timestamps.size();
            for (std::size_t component = 0; component < component_count; ++component) {
                if (!is_component_visible(channel_index, component)) {
                    continue;
                }
                const ImU32 color     = get_component_color(component);
                const ImU32 dim_color = (color & 0x00ffffffu) | 0x60000000u;

                // Constant extensions beyond the first / last key
                const float first_value = get_keyframe_value(animation, channel_index, 0, component);
                const float last_value  = get_keyframe_value(animation, channel_index, key_count - 1, component);
                if (sampler.timestamps.front() > m_view_time_min) {
                    const float y = value_to_y(first_value);
                    draw_list->AddLine(ImVec2{m_canvas_min.x, y}, ImVec2{time_to_x(sampler.timestamps.front()), y}, dim_color);
                }
                if (sampler.timestamps.back() < m_view_time_max) {
                    const float y = value_to_y(last_value);
                    draw_list->AddLine(ImVec2{time_to_x(sampler.timestamps.back()), y}, ImVec2{m_canvas_max.x, y}, dim_color);
                }

                draw_list->PathClear();
                switch (sampler.interpolation_mode) {
                    case erhe::scene::Animation_interpolation_mode::STEP: {
                        for (std::size_t key = 0; key < key_count; ++key) {
                            const float x = time_to_x(sampler.timestamps[key]);
                            const float y = value_to_y(get_keyframe_value(animation, channel_index, key, component));
                            if (key > 0) {
                                const float previous_y = value_to_y(get_keyframe_value(animation, channel_index, key - 1, component));
                                draw_list->PathLineTo(ImVec2{x, previous_y});
                            }
                            draw_list->PathLineTo(ImVec2{x, y});
                        }
                        break;
                    }
                    case erhe::scene::Animation_interpolation_mode::LINEAR: {
                        for (std::size_t key = 0; key < key_count; ++key) {
                            draw_list->PathLineTo(
                                ImVec2{
                                    time_to_x(sampler.timestamps[key]),
                                    value_to_y(get_keyframe_value(animation, channel_index, key, component))
                                }
                            );
                        }
                        break;
                    }
                    case erhe::scene::Animation_interpolation_mode::CUBICSPLINE:
                    default: {
                        for (std::size_t key = 0; (key + 1) < key_count; ++key) {
                            const float t0 = sampler.timestamps[key];
                            const float t1 = sampler.timestamps[key + 1];
                            const int steps = c_cubic_segment_steps;
                            const int start = (key == 0) ? 0 : 1;
                            for (int step = start; step <= steps; ++step) {
                                const float rel  = static_cast<float>(step) / static_cast<float>(steps);
                                const float time = t0 + rel * (t1 - t0);
                                const float value = animation.evaluate(time, channel_index, component);
                                draw_list->PathLineTo(ImVec2{time_to_x(time), value_to_y(value)});
                            }
                        }
                        if (key_count == 1) {
                            draw_list->PathLineTo(
                                ImVec2{
                                    time_to_x(sampler.timestamps[0]),
                                    value_to_y(get_keyframe_value(animation, channel_index, 0, component))
                                }
                            );
                        }
                        break;
                    }
                }
                draw_list->PathStroke(color, ImDrawFlags_None, 1.25f);
            }
        }
    }

    // Keyframe points
    {
        const ImU32 selected_color = IM_COL32(255, 255, 255, 255);
        for (std::size_t i = 0; i < m_visible_points.size(); ++i) {
            const Curve_point& point = m_visible_points[i];
            const ImVec2       p     = m_visible_point_px[i];
            const bool  selected = is_point_selected(point);
            const bool  hovered  = (static_cast<int>(i) == m_hovered_point);
            const float r        = c_key_half_size_px + (hovered ? 1.5f : 0.0f);
            const ImU32 color    = get_component_color(point.component);
            draw_list->AddQuadFilled(
                ImVec2{p.x, p.y - r},
                ImVec2{p.x + r, p.y},
                ImVec2{p.x, p.y + r},
                ImVec2{p.x - r, p.y},
                color
            );
            if (selected) {
                draw_list->AddQuad(
                    ImVec2{p.x, p.y - r - 1.0f},
                    ImVec2{p.x + r + 1.0f, p.y},
                    ImVec2{p.x, p.y + r + 1.0f},
                    ImVec2{p.x - r - 1.0f, p.y},
                    selected_color,
                    1.5f
                );
            }
        }
    }

    // Box select rectangle
    if (m_drag_mode == Drag_mode::box_select) {
        const ImVec2 box_min{std::min(m_drag_start_px.x, io.MousePos.x), std::min(m_drag_start_px.y, io.MousePos.y)};
        const ImVec2 box_max{std::max(m_drag_start_px.x, io.MousePos.x), std::max(m_drag_start_px.y, io.MousePos.y)};
        draw_list->AddRectFilled(box_min, box_max, IM_COL32(120, 160, 240, 40));
        draw_list->AddRect      (box_min, box_max, IM_COL32(120, 160, 240, 180));
    }

    // Playhead
    if ((m_context.animation_player != nullptr) && m_animation && (m_context.animation_player->get_animation() == m_animation)) {
        const float time = m_context.animation_player->get_time();
        if ((time >= m_view_time_min) && (time <= m_view_time_max)) {
            const float x = time_to_x(time);
            const ImU32 playhead_color = IM_COL32(90, 200, 220, 255);
            draw_list->AddLine(ImVec2{x, m_canvas_min.y}, ImVec2{x, m_canvas_max.y}, playhead_color, 1.5f);
            draw_list->AddTriangleFilled(
                ImVec2{x - 5.0f, m_canvas_min.y},
                ImVec2{x + 5.0f, m_canvas_min.y},
                ImVec2{x, m_canvas_min.y + 8.0f},
                playhead_color
            );
            char label[32];
            snprintf(label, sizeof(label), "%.3f", static_cast<double>(time));
            draw_list->AddText(ImVec2{x + 7.0f, m_canvas_min.y + 6.0f}, playhead_color, label);
        }
    }

    draw_list->PopClipRect();

    // Tooltip for hovered key
    if ((m_hovered_point >= 0) && (m_drag_mode == Drag_mode::none) && m_animation) {
        const Curve_point& point = m_visible_points[static_cast<std::size_t>(m_hovered_point)];
        const erhe::scene::Animation_channel& channel = m_animation->channels.at(point.channel_index);
        const erhe::scene::Animation_sampler& sampler = m_animation->samplers.at(channel.sampler_index);
        ImGui::SetTooltip(
            "%s %s.%s\nkey %d  t = %.4f  v = %.4f",
            channel.target ? channel.target->get_name().c_str() : "(no target)",
            erhe::scene::c_str(channel.path),
            c_component_names[point.component],
            static_cast<int>(point.key_index),
            static_cast<double>(sampler.timestamps.at(point.key_index)),
            static_cast<double>(get_keyframe_value(*m_animation.get(), point.channel_index, point.key_index, point.component))
        );
    }

    if (!m_animation) {
        const char* message = "Select an animation to edit (import a glTF asset with animations)";
        const ImVec2 text_size = ImGui::CalcTextSize(message);
        draw_list->AddText(
            ImVec2{
                m_canvas_min.x + ((canvas_size.x - text_size.x) * 0.5f),
                m_canvas_min.y + ((canvas_size.y - text_size.y) * 0.5f)
            },
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            message
        );
    }
}

void Animation_window::imgui()
{
    ensure_visibility_size();
    prune_stale_selection();

    animation_combo();
    transport_toolbar();
    keying_toolbar();
    timeline_strip();
    ImGui::Separator();

    if (ImGui::BeginChild("channel_pane", ImVec2{m_channel_pane_width, 0.0f}, ImGuiChildFlags_ResizeX)) {
        channel_list_pane();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    if (ImGui::BeginChild("curve_pane", ImVec2{0.0f, 0.0f}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        curve_canvas();
    }
    ImGui::EndChild();
}

}
