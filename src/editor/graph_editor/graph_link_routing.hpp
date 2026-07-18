#pragma once

#include "erhe_imgui/imgui_node_editor.h"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

namespace editor {

// Shared JSON form of a canvas link's routing mid points, used by the node
// clipboard (graph_clipboard.hpp / graph_editor_window_base.cpp) and the MCP
// graph tools. Each entry is either a bare canvas-space [x, y] pair (an Auto
// point - tangents computed) or an object with pen-tool tangent handles:
//   { "pos": [x, y], "mode": 1|2|3, "in": [x, y], "out": [x, y] }
// (mode 1 = mirrored, 2 = aligned, 3 = free; "in" / "out" are tangent offsets
// from "pos", so a paste translation moves only the positions).

// The mid points of the given link as a JSON array (empty when it has none).
[[nodiscard]] inline auto write_link_mid_points_json(
    ax::NodeEditor::EditorContext& node_editor,
    const ax::NodeEditor::LinkId   link_id
) -> nlohmann::json
{
    nlohmann::json mid_points_json = nlohmann::json::array();
    const int mid_point_count = node_editor.GetLinkMidPointCount(link_id);
    for (int i = 0; i < mid_point_count; ++i) {
        const ImVec2 mid_point = node_editor.GetLinkMidPoint(link_id, i);
        const int    mode      = node_editor.GetLinkMidPointMode(link_id, i);
        if (mode == 0) {
            mid_points_json.push_back({mid_point.x, mid_point.y});
        } else {
            ImVec2 tan_in {0.0f, 0.0f};
            ImVec2 tan_out{0.0f, 0.0f};
            node_editor.GetLinkMidPointTangents(link_id, i, &tan_in, &tan_out);
            mid_points_json.push_back(
                nlohmann::json{
                    {"pos",  {mid_point.x, mid_point.y}},
                    {"mode", mode},
                    {"in",   {tan_in.x,  tan_in.y}},
                    {"out",  {tan_out.x, tan_out.y}}
                }
            );
        }
    }
    return mid_points_json;
}

[[nodiscard]] inline auto is_xy_pair(const nlohmann::json& value) -> bool
{
    return value.is_array() && (value.size() == 2) && value.at(0).is_number() && value.at(1).is_number();
}

// Parses a mid point array (either entry form) and applies it to the link,
// translating positions by 'translation' (tangent offsets are translation
// invariant). A malformed array is refused whole: returns false and leaves
// the link's routing untouched. An empty array clears the routing.
inline auto read_link_mid_points_json(
    ax::NodeEditor::EditorContext& node_editor,
    const ax::NodeEditor::LinkId   link_id,
    const nlohmann::json&          mid_points_json,
    const ImVec2&                  translation = ImVec2{0.0f, 0.0f}
) -> bool
{
    if (!mid_points_json.is_array()) {
        return false;
    }
    std::vector<ImVec2> positions;
    std::vector<int>    modes;
    std::vector<ImVec2> tangents_in;
    std::vector<ImVec2> tangents_out;
    for (const nlohmann::json& entry : mid_points_json) {
        if (is_xy_pair(entry)) {
            positions.push_back(ImVec2{entry.at(0).get<float>() + translation.x, entry.at(1).get<float>() + translation.y});
            modes       .push_back(0);
            tangents_in .push_back(ImVec2{0.0f, 0.0f});
            tangents_out.push_back(ImVec2{0.0f, 0.0f});
            continue;
        }
        if (!entry.is_object() || !entry.contains("pos") || !is_xy_pair(entry["pos"])) {
            return false;
        }
        const nlohmann::json tan_in_json  = entry.value("in",  nlohmann::json::array());
        const nlohmann::json tan_out_json = entry.value("out", nlohmann::json::array());
        const int            mode         = entry.value("mode", 0);
        if ((mode != 0) && (!is_xy_pair(tan_in_json) || !is_xy_pair(tan_out_json))) {
            return false;
        }
        positions.push_back(ImVec2{entry["pos"].at(0).get<float>() + translation.x, entry["pos"].at(1).get<float>() + translation.y});
        modes.push_back(mode);
        if (mode != 0) {
            tangents_in .push_back(ImVec2{tan_in_json .at(0).get<float>(), tan_in_json .at(1).get<float>()});
            tangents_out.push_back(ImVec2{tan_out_json.at(0).get<float>(), tan_out_json.at(1).get<float>()});
        } else {
            tangents_in .push_back(ImVec2{0.0f, 0.0f});
            tangents_out.push_back(ImVec2{0.0f, 0.0f});
        }
    }
    node_editor.SetLinkMidPoints(link_id, positions.data(), static_cast<int>(positions.size()));
    for (std::size_t i = 0, end = positions.size(); i < end; ++i) {
        if (modes[i] != 0) {
            node_editor.SetLinkMidPointTangents(link_id, static_cast<int>(i), modes[i], tangents_in[i], tangents_out[i]);
        }
    }
    return true;
}

} // namespace editor
