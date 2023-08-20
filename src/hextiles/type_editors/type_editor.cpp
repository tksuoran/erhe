#include "type_editors/type_editor.hpp"

#include "map_window.hpp"
#include "menu_window.hpp"
#include "tiles.hpp"
#include "tile_renderer.hpp"
#include "tile_shape.hpp"

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_graphics/texture.hpp"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace hextiles
{

namespace
{

constexpr float drag_speed = 0.2f;

}

Type_editor::Type_editor(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Menu_window&                 menu_window,
    Tile_renderer&               tile_renderer,
    Tiles&                       tiles
)
    : m_imgui_renderer{imgui_renderer}
    , m_menu_window   {menu_window}
    , m_tile_renderer {tile_renderer}
    , m_tiles         {tiles}

    , terrain_editor_window                 {imgui_renderer, imgui_windows, menu_window, tiles, *this}
    , terrain_group_editor_window           {imgui_renderer, imgui_windows, menu_window, tiles, *this}
    , terrain_replacement_rule_editor_window{imgui_renderer, imgui_windows, menu_window, tiles, *this}
    , unit_editor_window                    {imgui_renderer, imgui_windows, menu_window, tiles, *this}
{
}

void Type_editor::hide_windows()
{
    terrain_editor_window                 .hide();
    terrain_group_editor_window           .hide();
    terrain_replacement_rule_editor_window.hide();
    unit_editor_window                    .hide();
}

void Type_editor::make_def(const char* tooltip_text, bool& value)
{
    if (ImGui::TableNextColumn()) {
        const auto label   = fmt::format("##{}-{}", m_current_column, m_current_row);
        const auto tooltip = fmt::format("{} for {}: {}", tooltip_text, m_current_element_name.c_str(), value);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (m_current_column < m_value_colors.size()) {
            ImGui::PushStyleColor(ImGuiCol_Text, m_value_colors[m_current_column]);
        }
        ImGui::Checkbox(label.c_str(), &value);
        if (m_current_column < m_value_colors.size()) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip.c_str());
        }
    }

    ++m_current_column;
}

void Type_editor::make_def(const char* tooltip_text, int& value)
{
    if (ImGui::TableNextColumn()) {
        const auto label   = fmt::format("##{}-{}", m_current_column, m_current_row);
        const auto tooltip = fmt::format("{} for {}: {}", tooltip_text, m_current_element_name.c_str(), value);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (m_current_column < m_value_colors.size()) {
            ImGui::PushStyleColor(ImGuiCol_Text, m_value_colors[m_current_column]);
        }
        ImGui::DragInt(label.c_str(), &value, drag_speed, 0, 999);
        if (m_current_column < m_value_colors.size()) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip.c_str());
        }
    }

    ++m_current_column;
}

void Type_editor::make_def(
    const char* tooltip_text,
    float&      value,
    const float min_value,
    const float max_value
)
{
    if (ImGui::TableNextColumn()) {
        const auto label   = fmt::format("##{}-{}", m_current_column, m_current_row);
        const auto tooltip = fmt::format("{} for {}: {}", tooltip_text, m_current_element_name.c_str(), value);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (m_current_column < m_value_colors.size())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, m_value_colors[m_current_column]);
        }
        ImGui::DragFloat(label.c_str(), &value, drag_speed, min_value, max_value);
        if (m_current_column < m_value_colors.size())
        {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", tooltip.c_str());
        }
    }

    ++m_current_column;
}

void Type_editor::make_terrain_type_def(
    const char* tooltip_text,
    terrain_t&  value
)
{
    if (ImGui::TableNextColumn()) {
        terrain_tile_t terrain_tile = m_tiles.get_terrain_tile_from_terrain(value);
        const auto     label        = fmt::format("##{}-{}", m_current_column, m_current_row);
        const auto     tooltip      = fmt::format("{} for {}: {}", tooltip_text, m_current_element_name.c_str(), value);
        m_tile_renderer.terrain_image(terrain_tile, 2);
        ImGui::SameLine();
        if (m_current_column < m_value_colors.size()) {
            ImGui::PushStyleColor(ImGuiCol_Text, m_value_colors[m_current_column]);
        }
        m_tile_renderer.make_terrain_type_combo(label.c_str(), value);
        if (m_current_column < m_value_colors.size()) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip.c_str());
        }
    }

    ++m_current_column;
}

void Type_editor::make_unit_type_def(
    const char* tooltip_text,
    unit_t&     value,
    const int   player
)
{
    if (ImGui::TableNextColumn()) {
        unit_tile_t unit_tile = m_tile_renderer.get_single_unit_tile(player, value);
        const auto  label     = fmt::format("##{}-{}", m_current_column, m_current_row);
        const auto  tooltip   = fmt::format("{} for {}: {}", tooltip_text, m_current_element_name.c_str(), value);
        m_tile_renderer.unit_image(unit_tile, 2);
        ImGui::SameLine();
        if (m_current_column < m_value_colors.size())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, m_value_colors[m_current_column]);
        }
        m_tile_renderer.make_unit_type_combo(label.c_str(), value, player);
        if (m_current_column < m_value_colors.size()) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip.c_str());
        }
    }

    ++m_current_column;
}

auto Type_editor::begin_table(const int column_count) -> bool
{
    m_header_colors.clear();
    m_value_colors.clear();

    const auto outer_size = ImGui::GetContentRegionAvail();
    return ImGui::BeginTable(
        "Terrain Types",
        column_count,
        ImGuiTableFlags_Resizable   |
        ImGuiTableFlags_Reorderable |
        ImGuiTableFlags_Hideable    |
        ImGuiTableFlags_RowBg       |
        ImGuiTableFlags_NoBordersInBodyUntilResize |
        ImGuiTableFlags_ScrollX     |
        ImGuiTableFlags_ScrollY,
        outer_size
    );
}

void Type_editor::make_column(
    const char*  label,
    const float  width,
    const ImVec4 color
)
{
    ImGui::TableSetupColumn(label, ImGuiTableColumnFlags_WidthFixed, width);
    m_header_colors.push_back(color);
    m_value_colors.push_back(
        ImVec4{
            0.5f + color.x * 0.5f,
            0.5f + color.y * 0.5f,
            0.5f + color.z * 0.5f,
            1.0f
        }
    );
};

void Type_editor::table_headers_row()
{
    const int column_count = ImGui::TableGetColumnCount();
    ImGui::TableSetupScrollFreeze(column_count, 1);

    //ImGui::TableHeadersRow();
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    for (int column = 0; column < column_count; column++) {
        if (!ImGui::TableSetColumnIndex(column)) {
            continue;
        }
        const char* column_name = ImGui::TableGetColumnName(column); // Retrieve name passed to TableSetupColumn()
        ImGui::PushID(column);
        ImGui::PushStyleColor(ImGuiCol_Text, m_header_colors[column]);
        ImGui::TableHeader(column_name);
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
}

namespace
{
    ImVec4 attack_color  {1.0f, 0.2f, 0.2f, 1.0f};
    ImVec4 defense_color {0.2f, 1.0f, 0.2f, 1.0f};
    ImVec4 health_color  {0.4f, 1.0f, 0.4f, 1.0f};
    ImVec4 move_color    {0.6f, 0.8f, 1.0f, 1.0f};
    ImVec4 cargo_color   {1.0f, 0.7f, 0.5f, 1.0f};
    ImVec4 generate_color{1.0f, 0.9f, 0.5f, 1.0f};
}

void Type_editor::terrain_editor_imgui()
{
    constexpr ImVec2 button_size{110.0f, 0.0f};

    static int show_tile = 0;

    m_tile_renderer.terrain_image(static_cast<terrain_tile_t>(show_tile), 4);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputInt("Show tile", &show_tile);

    m_tile_renderer.terrain_image(m_tiles.get_terrain_tile_from_terrain(m_simulate_terrain_type), 2);
    ImGui::SameLine();
    m_tile_renderer.make_terrain_type_combo("##Terrain Type", m_simulate_terrain_type);
    ImGui::SameLine();
    ImGui::TextUnformatted("vs.");
    ImGui::SameLine();
    m_tile_renderer.unit_image(m_tile_renderer.get_single_unit_tile(0, m_simulate_unit_type), 2);
    ImGui::SameLine();
    m_tile_renderer.make_unit_type_combo("##Unit Type", m_simulate_unit_type);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(50.0f);
    ImGui::DragInt("Move Count", &m_simulate_move_count, drag_speed, 1, 99);
    ImGui::SameLine();

    if (ImGui::Button("Simulate", button_size)) {
        //for (int i = 0; i < 1000; ++i) {
        //}
    }

    //ImGui::SameLine();
    //if (ImGui::Button("Update", button_size)) {
    //    m_map_window->update_elevation_terrains();
    //}

    constexpr int column_count = 18;

    if (!begin_table(column_count)) {
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{2.0f, 2.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,  ImVec2{1.0f, 0.0f});

    constexpr float narrow_column_width = 65.0f;
    constexpr float medium_column_width = 75.0f;
    constexpr float wide_column_width   = 120.0f;

    make_column("Terrain",      wide_column_width);
    make_column("Flags",        medium_column_width);
    make_column("City Size",    narrow_column_width);
    make_column("Move Types",   medium_column_width, move_color);
    make_column("Move Cost",    narrow_column_width, move_color);
    make_column("Threat",       narrow_column_width, attack_color);
    make_column("Strength",     narrow_column_width, health_color);
    make_column("After Damage", wide_column_width,   attack_color);
    make_column("Defense+",     narrow_column_width, defense_color);
    make_column("Level Types",  medium_column_width);
    make_column("Elevation",    narrow_column_width, generate_color);
    make_column("Priority",     wide_column_width,   generate_color);
    make_column("Base",         wide_column_width,   generate_color);
    make_column("Min Temp",     narrow_column_width, generate_color);
    make_column("Max Temp",     narrow_column_width, generate_color);
    make_column("Min Humi",     narrow_column_width, generate_color);
    make_column("Max Humi",     narrow_column_width, generate_color);
    make_column("Ratio",        narrow_column_width, generate_color);
    table_headers_row();

    const terrain_t end = static_cast<unit_t>(m_tiles.get_terrain_type_count());
    for (m_current_terrain_id = 0; m_current_terrain_id < end; ++m_current_terrain_id) {
        m_current_row = m_current_terrain_id;
        ImGui::TableNextRow();
        {
            auto& terrain = m_tiles.get_terrain_type(m_current_terrain_id);

            m_current_element_name = terrain.name;

            // Icon and name
            if (ImGui::TableNextColumn()) {
                ImGui::SetNextItemWidth(20.0f);
                ImGui::PushFont        (m_imgui_renderer.mono_font());
                const auto row_label = fmt::format("{:>2}", m_current_row);
                ImGui::TextUnformatted (row_label.c_str());
                ImGui::PopFont         ();
                ImGui::SameLine        ();
                const auto name_label = fmt::format("##name-{}", m_current_terrain_id);
                m_tile_renderer.terrain_image(m_tiles.get_terrain_tile_from_terrain(m_current_terrain_id), 2);
                ImGui::SameLine        ();
                ImGui::SetNextItemWidth(80.0f);
                ImGui::InputText       (name_label.c_str(), terrain.name.data(), terrain.name.max_size() - 1);
            }
            m_current_column = 1;

            make_bit_mask_def<Terrain_flags>("Flags",           terrain.flags);
            make_def                        ("City Size",       terrain.city_size);
            make_bit_mask_def<Movement_type>("Move Types",      terrain.move_type_allow_mask);
            make_def                        ("Move Cost",       terrain.move_cost);
            make_def                        ("Threat to Units", terrain.threat);
            make_def                        ("Strength",        terrain.strength);
            make_terrain_type_def           ("Damaged Version", terrain.damaged_version);
            make_def                        ("Defense Bonus",   terrain.defence_bonus);
            make_bit_mask_def<Movement_type>("Level Types",     terrain.move_type_level_mask);
            make_def                        ("Generate Elevation",           terrain.generate_elevation);
            make_def                        ("Generate Priority",            terrain.generate_priority);
            make_terrain_type_def           ("Generate Base",                terrain.generate_base);
            make_def                        ("Generate Minimum Temperature", terrain.generate_min_temperature);
            make_def                        ("Generate Maximum Temperature", terrain.generate_max_temperature);
            make_def                        ("Generate Minimum Humidity",    terrain.generate_min_humidity);
            make_def                        ("Generate Maximum Humidity",    terrain.generate_max_humidity);
            make_def                        ("Generate Ratio",               terrain.generate_ratio, 0.0f, 1.0f);
        }
    }
    ImGui::PopStyleVar(2);
    ImGui::EndTable();
}

void Type_editor::terrain_group_editor_imgui()
{
    constexpr int column_count = 13;

    if (!begin_table(column_count)) {
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{2.0f, 2.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,  ImVec2{1.0f, 0.0f});

    constexpr float narrow_column_width = 60.0f;
    constexpr float wide_column_width   = 120.0f;

    make_column("Group ID",     narrow_column_width); //  1
    make_column("Enabled",      narrow_column_width); //  2
    make_column("Shape Group",  narrow_column_width); //  3
    make_column("Base Terrain", wide_column_width);   //  4
    make_column("First",        wide_column_width);   //  5
    make_column("Last ",        wide_column_width);   //  6
    make_column("First",        wide_column_width);   //  7
    make_column("Last ",        wide_column_width);   //  8
    make_column("Group",        narrow_column_width); //  9
    make_column("Group",        narrow_column_width); // 10
    make_column("Promoted",     narrow_column_width); // 11
    make_column("Demoted",      narrow_column_width); // 12
    make_column("Remove",       narrow_column_width); // 13
    table_headers_row();

    const int end = static_cast<int>(m_tiles.get_terrain_group_count());
    std::optional<int> delete_row;
    for (m_current_row = 0; m_current_row < end; ++m_current_row) {
        ImGui::TableNextRow();
        {
            auto& group = m_tiles.get_terrain_group(m_current_row);

            fmt::format_to_n(
                m_current_element_name.data(),
                m_current_element_name.max_size() - 1,
                "Group {}",
                m_current_row
            );
            //m_current_element_name = fmt::format("Group {}", m_current_row);

            // Icon and name
            if (ImGui::TableNextColumn()) {
                ImGui::SetNextItemWidth(20.0f);
                ImGui::PushFont        (m_imgui_renderer.mono_font());
                const auto row_label = fmt::format("{:>2}", m_current_row);
                ImGui::TextUnformatted (row_label.c_str());
                ImGui::PopFont         ();
            }
            m_current_column = 1;

            make_def             ("Enabled",            group.enabled);
            make_def             ("Shape Group",        group.shape_group);
            make_terrain_type_def("Base Terrain",       group.base_terrain_type);
            make_terrain_type_def("Link Range 1 First", group.link_first[0]);
            make_terrain_type_def("Link Range 1 Last",  group.link_last[0]);
            make_terrain_type_def("Link Range 2 First", group.link_first[1]);
            make_terrain_type_def("Link Range 2 Last",  group.link_last[1]);
            make_def             ("Link Group 1",       group.link_group[0]);
            make_def             ("Link Group 2",       group.link_group[1]);
            make_def             ("Promote to Group",   group.promoted);
            make_def             ("Demote to Group",    group.demoted);

            if (ImGui::TableNextColumn()) {
                const auto label = fmt::format("Remove##{}-{}", m_current_column, m_current_row);
                if (ImGui::Button(label.c_str())) {
                    delete_row = m_current_row;
                }
            }
        }
    }

    ImGui::TableNextRow();
    if (ImGui::TableNextColumn() && ImGui::Button("Add")) {
        m_tiles.add_terrain_group();
    }
    ImGui::PopStyleVar(2);
    ImGui::EndTable();

    if (delete_row.has_value()) {
        m_tiles.remove_terrain_group(delete_row.value());
    }
}

void Type_editor::terrain_replacement_rule_editor_imgui()
{
    constexpr int column_count = 9;

    if (!begin_table(column_count)) {
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{2.0f, 2.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,  ImVec2{1.0f, 0.0f});

    constexpr float narrow_column_width = 60.0f;
    constexpr float wide_column_width   = 120.0f;

    make_column("Rule ID",     narrow_column_width); // 1
    make_column("Enabled",     narrow_column_width); // 2
    make_column("Equal",       narrow_column_width); // 3
    make_column("Primary",     wide_column_width);   // 4
    make_column("Secondary",   wide_column_width);   // 5
    make_column("Secondary",   wide_column_width);   // 6
    make_column("Secondary",   wide_column_width);   // 7
    make_column("Replacement", wide_column_width);   // 8
    make_column("Remove",      narrow_column_width); // 9
    table_headers_row();

    const int end = static_cast<int>(m_tiles.get_terrain_replacement_rule_count());
    std::optional<int> delete_row;
    for (m_current_row = 0; m_current_row < end; ++m_current_row) {
        ImGui::TableNextRow();
        {
            auto& rule = m_tiles.get_terrain_replacement_rule(m_current_row);

            //m_current_element_name = 
            fmt::format_to_n(
                m_current_element_name.data(),
                m_current_element_name.max_size() - 1,
                "Rule {}",
                m_current_row
            );

            // Icon and name
            if (ImGui::TableNextColumn()) {
                ImGui::SetNextItemWidth(20.0f);
                ImGui::PushFont        (m_imgui_renderer.mono_font());
                const auto row_label = fmt::format("{:>2}", m_current_row);
                ImGui::TextUnformatted (row_label.c_str());
                ImGui::PopFont         ();
            }
            m_current_column = 1;

            make_def             ("Enabled",     rule.enabled);
            make_def             ("Equal",       rule.equal);
            make_terrain_type_def("Primary",     rule.primary);
            make_terrain_type_def("Secondary",   rule.secondary[0]);
            make_terrain_type_def("Secondary",   rule.secondary[1]);
            make_terrain_type_def("Secondary",   rule.secondary[2]);
            make_terrain_type_def("Replacement", rule.replacement);

            if (ImGui::TableNextColumn()) {
                const auto label = fmt::format("Remove##{}-{}", m_current_column, m_current_row);
                if (ImGui::Button(label.c_str())) {
                    delete_row = m_current_row;
                }
            }
        }
    }

    ImGui::TableNextRow();
    if (ImGui::TableNextColumn() && ImGui::Button("Add")) {
        m_tiles.add_terrain_replacement_rule();
    }

    ImGui::PopStyleVar(2);
    ImGui::EndTable();

    if (delete_row.has_value()) {
        m_tiles.remove_terrain_replacement_rule(delete_row.value());
    }
}

void Type_editor::unit_editor_imgui()
{
    static int show_tile = 0;
    m_tile_renderer.unit_image(static_cast<unit_tile_t>(show_tile), 4);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputInt("Show tile", &show_tile);

    static int player_base_one = 1;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::SliderInt("Player", &player_base_one, 1, max_player_count);
    const int player = player_base_one - 1;

    //m_rendering->show_texture();

    m_tile_renderer.unit_image(m_tile_renderer.get_single_unit_tile(0, m_simulate_unit_type_a), 2);
    ImGui::SameLine();
    m_tile_renderer.make_unit_type_combo("##Type A", m_simulate_unit_type_a);
    ImGui::SameLine();
    ImGui::TextUnformatted("vs.");
    ImGui::SameLine();
    m_tile_renderer.unit_image(m_tile_renderer.get_single_unit_tile(1, m_simulate_unit_type_b), 2);
    ImGui::SameLine();
    m_tile_renderer.make_unit_type_combo("##Type B", m_simulate_unit_type_b);
    ImGui::SameLine();

    if (ImGui::Button("Simulate")) {
        //for (int i = 0; i < 1000; ++i) {
        //
        //}
    }

    constexpr int column_count = 38;

    if (!begin_table(column_count)) {
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{2.0f, 2.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,  ImVec2{1.0f, 0.0f});

    constexpr float narrow_column_width = 35.0f;
    constexpr float column_width        = 60.0f;
    constexpr float wide_column_width   = 80.0f;
    constexpr float combo_column_width  = 80.0f;

    make_column("Unit", 120.0f);                  // 1

    make_column("Tech",    narrow_column_width);  // 2
    make_column("PTime",   narrow_column_width);  // 3
    make_column("City",    narrow_column_width);  // 4
    make_column("Flags",   wide_column_width);    // 5

    make_column("HP",      narrow_column_width, health_color); // 6
    make_column("Repair",  narrow_column_width, health_color); // 7

    make_column("BType",   combo_column_width);                 // 8
    make_column("Air",     narrow_column_width, attack_color ); // 9
    make_column("Air",     narrow_column_width, defense_color); // 10
    make_column("Gnd",     narrow_column_width, attack_color ); // 11
    make_column("Gnd",     narrow_column_width, defense_color); // 12
    make_column("Sea",     narrow_column_width, attack_color ); // 13
    make_column("Sea",     narrow_column_width, defense_color); // 14
    make_column("Sub",     narrow_column_width, attack_color ); // 15
    make_column("Sub",     narrow_column_width, defense_color); // 16
    make_column("St.",     combo_column_width);                 // 17
    make_column("St.",     narrow_column_width, attack_color ); // 18
    make_column("St.",     narrow_column_width, defense_color); // 19

    make_column("MType",   combo_column_width , move_color); // 20
    make_column("MP",      narrow_column_width, move_color); // 21
    make_column("MP'",     narrow_column_width, move_color); // 22
    make_column("Fuel",    narrow_column_width, move_color); // 23
    make_column("Refuel",  narrow_column_width, move_color); // 24

    make_column("CargoT'", wide_column_width, cargo_color); // 25
    make_column("CSpace",  column_width,      cargo_color); // 26
    make_column("CSpeed",  column_width,      cargo_color); // 27

    make_column("Vis",     narrow_column_width, move_color); // 28
    make_column("SVis",    narrow_column_width, move_color); // 29
    make_column("VFrom",   narrow_column_width, move_color); // 30
    make_column("SVFrom",  narrow_column_width, move_color); // 31

    make_column("Range",   narrow_column_width, attack_color); // 32
    make_column("SRange",  narrow_column_width, attack_color); // 33
    make_column("RAtt",    narrow_column_width, attack_color); // 34
    make_column("Ammo",    narrow_column_width, attack_color); // 35
    make_column("Shots",   narrow_column_width, attack_color); // 36

    make_column("Sfx",     narrow_column_width); // 37
    make_column("Freq",    narrow_column_width); // 38

    table_headers_row();

    const unit_t end = static_cast<unit_t>(m_tiles.get_unit_type_count());
    for (m_current_unit_id = 0; m_current_unit_id < end; ++m_current_unit_id) {
        m_current_row = m_current_unit_id;
        ImGui::TableNextRow();
        {
            auto& unit = m_tiles.get_unit_type(m_current_unit_id);

            m_current_element_name = unit.name;

            // 0 Icon and name
            if (ImGui::TableNextColumn()) {
                ImGui::SetNextItemWidth(20.0f);
                ImGui::PushFont        (m_imgui_renderer.mono_font());
                const auto row_label = fmt::format("{:>2}", m_current_row);
                ImGui::TextUnformatted (row_label.c_str());
                ImGui::PopFont         ();
                ImGui::SameLine        ();
                const auto name_label = fmt::format("##name-{}", m_current_row);
                m_tile_renderer.unit_image(m_tile_renderer.get_single_unit_tile(player, m_current_unit_id), 2);
                ImGui::SameLine        ();
                ImGui::SetNextItemWidth(80.0f);
                ImGui::InputText       (name_label.c_str(), unit.name.data(), unit.name.max_size() - 1);
            }
            m_current_column = 1;

            make_def                     ("Required Tech Level",        unit.tech_level);      // 2
            make_def                     ("Production Time (in turns)", unit.production_time); // 3
            make_def                     ("City Size",                  unit.city_size);       // 4
            make_bit_mask_def<Unit_flags>("Flags",                      unit.flags);           // 5

            make_def("Hit Points",                                  unit.hit_points);      // 6
            make_def("Repair hit-points per Turn (when in City)",   unit.repair_per_turn); // 7

            make_combo_def<Battle_type>("Battle Type",              unit.battle_type);     //  8
            make_def                   ("Attack Air Units",         unit.attack [0]);      //  9
            make_def                   ("Defense Air Units",        unit.defense[0]);      // 10
            make_def                   ("Attack Ground Units",      unit.attack [1]);      // 11
            make_def                   ("Defense Ground Units",     unit.defense[1]);      // 12
            make_def                   ("Attack Sea Units",         unit.attack [2]);      // 13
            make_def                   ("Defense Sea units",        unit.defense[2]);      // 14
            make_def                   ("Attack Underwater units",  unit.attack [3]);      // 15
            make_def                   ("Defense Underwater units", unit.defense[3]);      // 16
            make_unit_type_def         ("Stealth Version",          unit.stealth_version, player); // 17
            make_def                   ("Stealth Attack",           unit.stealth_attack);  // 18
            make_def                   ("Stealth Defense",          unit.stealth_defense); // 19

            make_bit_combo_def<Movement_type>("Movement Type",                              unit.move_type_bits);  // 20
            make_def                     ("Movement Points per Turn (in normal mode)",  unit.move_points[0]);  // 21
            make_def                     ("Movement points per Turn (in stealth mode)", unit.move_points[1]);  // 22
            make_def                     ("Fuel Capacity",                              unit.fuel);            // 23
            make_def                     ("Refuel per Turn (when in City)",             unit.refuel_per_turn); // 24

            make_bit_mask_def<Movement_type>("Cargo Types",                           unit.cargo_types);               // 25
            make_def                        ("Cargo Space",                           unit.cargo_space);               // 26
            make_def                        ("Cargo Load Count per Turn",             unit.cargo_load_count_per_turn); // 27

            make_def                        ("Vision Range (in normal mode)",         unit.vision_range[0]);           // 28
            make_def                        ("Vision Range (in stealth mode)",        unit.vision_range[1]);           // 29
            make_def                        ("Visible From (in normal mode)",         unit.visible_from_range[0]);     // 30
            make_def                        ("Visible From (in stealth mode)",        unit.visible_from_range[1]);     // 31

            make_def                        ("Ranged Attack Range (in normal mode)",  unit.range[0]);                     // 32
            make_def                        ("Ranged Attack Range (in stealth mode)", unit.range[1]);                     // 33
            make_def                        ("Ranged Attack Modifier",                unit.ranged_attack_modifier);       // 34
            make_def                        ("Ranged Attack Ammo",                    unit.ranged_attack_ammo);           // 35
            make_def                        ("Ranged Attacks per Turn",               unit.ranged_attack_count_per_turn); // 36

            make_def                        ("Audio Loop",                            unit.audio_loop);      // 37
            make_def                        ("Audio Frequency",                       unit.audio_frequency); // 38
        }
    }
    ImGui::PopStyleVar(2);
    ImGui::EndTable();
}

} // namespace hextiles
