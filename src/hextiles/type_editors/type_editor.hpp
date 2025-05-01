#pragma once

#include <fmt/format.h>

#include "types.hpp"
#include "type_editors/terrain_editor_window.hpp"
#include "type_editors/terrain_group_editor_window.hpp"
#include "type_editors/terrain_replacement_rule_editor_window.hpp"
#include "type_editors/unit_editor_window.hpp"
#include "map_editor/terrain_palette_window.hpp"

#include "erhe_imgui/imgui_renderer.hpp"

#include <etl/vector.h>

#include <cstdint>
#include <imgui/imgui.h>
#include <string>

namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}

namespace hextiles {

class Tile_renderer;
class Tiles;

class Type_editor
{
public:
    Type_editor(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Menu_window&                 menu_window,
        Tile_renderer&               tile_renderer,
        Tiles&                       tiles
    );

    // Public API
    void terrain_editor_imgui();
    void terrain_group_editor_imgui();
    void terrain_replacement_rule_editor_imgui();
    void unit_editor_imgui();
    void hide_windows();

private:
    erhe::imgui::Imgui_renderer& m_imgui_renderer;
    Menu_window&                 m_menu_window;
    Tile_renderer&               m_tile_renderer;
    Tiles&                       m_tiles;

public:
    Terrain_editor_window                  terrain_editor_window;
    Terrain_group_editor_window            terrain_group_editor_window;
    Terrain_replacement_rule_editor_window terrain_replacement_rule_editor_window;
    Unit_editor_window                     unit_editor_window;

private:
    void make_def             (const char* tooltip_text, bool& value);
    void make_def             (const char* tooltip_text, int& value);
    void make_def             (const char* tooltip_text, float& value, float min_value, float max_value);
    void make_terrain_type_def(const char* tooltip_text, terrain_t& value);
    void make_unit_type_def   (const char* label, unit_t& value, int player = 0);

    template<typename T>
    void make_combo_def(const char* tooltip_text, uint32_t& value);

    template<typename T>
    void make_bit_combo_def(const char* tooltip_text, uint32_t& value);

    template<typename T>
    void make_bit_mask_def(const char* tooltip_text, uint32_t& value);

    auto begin_table(int column_count) -> bool;
    void make_column(
        const char* label,
        float       width,
        ImVec4      color = ImVec4{0.9f, 0.9f, 0.9f, 1.0f}
    );
    void table_headers_row();

    // Simulate
    unit_t      m_simulate_terrain_type{1};
    unit_t      m_simulate_unit_type   {1};
    int         m_simulate_move_count  {1};

    unit_t      m_simulate_unit_type_a {1};
    unit_t      m_simulate_unit_type_b {1};

    // Table
    int         m_current_column;
    int         m_current_row;
    std::string m_current_element_name;
    terrain_t   m_current_terrain_id;
    unit_t      m_current_unit_id;

    constexpr static int max_column_count = 40;

    etl::vector<ImVec4, max_column_count> m_header_colors;
    etl::vector<ImVec4, max_column_count> m_value_colors;
};

} // namespace hextiles


namespace hextiles
{

template<typename T>
void Type_editor::make_combo_def(
    const char* tooltip_text,
    uint32_t&   value
)
{
    if (ImGui::TableNextColumn()) {
        //char label_buffer[200];
        //char tooltip_buffer[200];
        //snprintf(&label_buffer, sizeof(label_buffer), "##%d-%d", m_current_column, m_current_row);
        //snprintf(&tooltip_buffer, sizeof(tooltip_buffer), "%s for %s: %d", tooltip_text, m_current_element_name.c_str(), value);
        const auto label   = fmt::format("##{}-{}", m_current_column, m_current_row);
        const auto tooltip = fmt::format("{} for {}: {}", tooltip_text, m_current_element_name.c_str(), value);
        ImGui::SetNextItemWidth(-FLT_MIN);
        const char* combo_preview_value = T::c_str(value);//(value == 0)
            //? "None"
            //: T::c_str(value);

        if (m_current_column < m_value_colors.size()) {
            ImGui::PushStyleColor(ImGuiCol_Text, m_value_colors[m_current_column]);
        }
        const auto combo_pressed = ImGui::BeginCombo(
            label.c_str(),
            combo_preview_value,
            ImGuiComboFlags_NoArrowButton |
            ImGuiComboFlags_HeightLarge
        );
        if (m_current_column < m_value_colors.size()) {
            ImGui::PopStyleColor();
        }

        if (combo_pressed) {
            //{
            //    const auto id = fmt::format("##{}-{}-none", m_current_column, m_current_row);
            //    ImGui::PushID(id.c_str());
            //
            //    bool is_selected = (value == 0);
            //    if (ImGui::Selectable("None", is_selected))
            //    {
            //        value = 0;
            //    }
            //
            //    if (is_selected)
            //    {
            //        ImGui::SetItemDefaultFocus();
            //    }
            //    ImGui::PopID();
            //}

            for (uint32_t bit_position = 0; bit_position < T::bit_count; ++bit_position) {
                //char id_buffer[40];
                //snprintf(&id_buffer, sizeof(id_buffer), "##%d-%d-%u", m_current_column, m_current_row, bit_position);
                const auto id = fmt::format("##{}-{}-{}", m_current_column, m_current_row, bit_position);
                ImGui::PushID(id.c_str());

                //const uint32_t bit_value   = (1u << bit_position);
                bool is_selected = (value == bit_position);
                if (ImGui::Selectable(T::c_str(bit_position), is_selected)) {
                    value = bit_position;
                }

                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tooltip.c_str());
            }
        }
    }

    ++m_current_column;
}

template<typename T>
void Type_editor::make_bit_combo_def(const char* tooltip_text, uint32_t& value)
{
    if (ImGui::TableNextColumn()) {
        //char label_buffer[200];
        //char tooltip_buffer[200];
        //snprintf(&label_buffer, sizeof(label_buffer), "##%d-%d", m_current_column, m_current_row);
        //snprintf(&tooltip_buffer, sizeof(tooltip_buffer), "%s for %s: %d", tooltip_text, m_current_element_name.c_str(), value);
        const auto label   = fmt::format("##{}-{}", m_current_column, m_current_row);
        const auto tooltip = fmt::format("{} for {}: {}", tooltip_text, m_current_element_name.c_str(), value);
        ImGui::SetNextItemWidth(-FLT_MIN);
        uint32_t preview_bit_position = 0;
        for (uint32_t bit_position = 0; bit_position < T::bit_count; bit_position++) {
            const uint32_t bit_value = (1u << bit_position);
            if (value == bit_value) {
                preview_bit_position = bit_position;
            }
        }
        const char* combo_preview_value = (value == 0)
            ? "None"
            : T::c_str(preview_bit_position);

        if (m_current_column < m_value_colors.size()) {
            ImGui::PushStyleColor(ImGuiCol_Text, m_value_colors[m_current_column]);
        }
        const auto combo_pressed = ImGui::BeginCombo(
            label.c_str(),
            combo_preview_value,
            ImGuiComboFlags_NoArrowButton |
            ImGuiComboFlags_HeightLarge
        );
        if (m_current_column < m_value_colors.size()) {
            ImGui::PopStyleColor();
        }

        if (combo_pressed) {
            {
                //char id_buffer[40];
                //snprintf(&id_buffer, sizeof(id_buffer), "##%d-%d-none", m_current_column, m_current_row);
                //ImGui::PushID(id_buffer);
                const auto id = fmt::format("##{}-{}-none", m_current_column, m_current_row);
                ImGui::PushID(id.c_str());

                bool is_selected = (value == 0);
                if (ImGui::Selectable("None", is_selected)) {
                    value = 0;
                }

                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
            }

            for (uint32_t bit_position = 0; bit_position < T::bit_count; ++bit_position) {
                const auto     id = fmt::format("##{}-{}-{}", m_current_column, m_current_row, bit_position);
                ImGui::PushID(id.c_str());

                const uint32_t bit_value   = (1u << bit_position);
                bool           is_selected = (value == bit_value);
                if (ImGui::Selectable(T::c_str(bit_position), is_selected)) {
                    value = bit_value;
                }

                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tooltip.c_str());
            }
        }
    }

    ++m_current_column;
}

template<typename T>
void Type_editor::make_bit_mask_def(const char* tooltip_text, uint32_t& value)
{
    static_cast<void>(tooltip_text);
    static_cast<void>(value);

    std::array<bool, T::bit_count> type_boolean;
    char preview_chars[T::bit_count + 1];
    for (uint32_t bit_position = 0; bit_position < T::bit_count; ++bit_position) {
        uint32_t bit_value = (1u << bit_position);
        type_boolean[bit_position] = (value & bit_value) == bit_value;
        preview_chars[bit_position] = type_boolean[bit_position] ? T::c_char(bit_position) : ' ';
    }
    preview_chars[T::bit_count] = '\0';

    const auto preview = fmt::format(
        "{}##{}-{}",
        preview_chars,
        m_current_column,
        m_current_row
    );
    const auto popup_label = fmt::format("popup-{}-{}", m_current_column, m_current_row);

    if (ImGui::TableNextColumn()) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::PushFont(m_imgui_renderer.mono_font());
        if (m_current_column < m_value_colors.size()) {
            ImGui::PushStyleColor(ImGuiCol_Text, m_value_colors[m_current_column]);
        }
        if (ImGui::Button(preview.c_str())) {
            ImGui::OpenPopup(popup_label.c_str());
        }
        if (m_current_column < m_value_colors.size()) {
            ImGui::PopStyleColor();
        }
        ImGui::PopFont();
        if (ImGui::BeginPopup(popup_label.c_str())) {
            const auto title = fmt::format("{} for {}", tooltip_text, m_current_element_name.c_str());
            ImGui::LabelText(title.c_str(), "%s", "");
            ImGui::Separator();
            for (uint32_t bit_position = 0; bit_position < T::bit_count; ++bit_position) {
                ImGui::MenuItem(
                    T::c_str(bit_position),
                    "",
                    &type_boolean[bit_position]
                );
                uint32_t bit_value = (1u << bit_position);
                if (type_boolean[bit_position]) {
                    value |= bit_value;
                } else {
                    value &= ~bit_value;
                }
            }
            ImGui::EndPopup();
        }
    }

    ++m_current_column;
}

} // namespace hextiles
