#pragma once

#include "erhe_graph/link.hpp"
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

// ---------------------------------------------------------------------------
// Model-side routing (erhe::graph::Link stores the mid points and curve
// params - the authority every editor canvas syncs against and the form the
// graph JSON serializes).

// The link's stored mid points as the same JSON array form as above.
[[nodiscard]] inline auto link_routing_to_json(const erhe::graph::Link& link) -> nlohmann::json
{
    nlohmann::json mid_points_json = nlohmann::json::array();
    for (const erhe::graph::Link_mid_point& mid_point : link.get_mid_points()) {
        if (mid_point.mode == 0) {
            mid_points_json.push_back({mid_point.position_x, mid_point.position_y});
        } else {
            mid_points_json.push_back(
                nlohmann::json{
                    {"pos",  {mid_point.position_x, mid_point.position_y}},
                    {"mode", mid_point.mode},
                    {"in",   {mid_point.tangent_in_x,  mid_point.tangent_in_y}},
                    {"out",  {mid_point.tangent_out_x, mid_point.tangent_out_y}}
                }
            );
        }
    }
    return mid_points_json;
}

// Parses a mid point array (either entry form) into the link's stored
// routing, translating positions by 'translation'. A malformed array is
// refused whole: returns false and leaves the link untouched.
inline auto link_routing_from_json(
    erhe::graph::Link&    link,
    const nlohmann::json& mid_points_json,
    const ImVec2&         translation = ImVec2{0.0f, 0.0f}
) -> bool
{
    if (!mid_points_json.is_array()) {
        return false;
    }
    std::vector<erhe::graph::Link_mid_point> mid_points;
    for (const nlohmann::json& entry : mid_points_json) {
        erhe::graph::Link_mid_point mid_point;
        if (is_xy_pair(entry)) {
            mid_point.position_x = entry.at(0).get<float>() + translation.x;
            mid_point.position_y = entry.at(1).get<float>() + translation.y;
            mid_points.push_back(mid_point);
            continue;
        }
        if (!entry.is_object() || !entry.contains("pos") || !is_xy_pair(entry["pos"])) {
            return false;
        }
        const nlohmann::json tan_in_json  = entry.value("in",  nlohmann::json::array());
        const nlohmann::json tan_out_json = entry.value("out", nlohmann::json::array());
        mid_point.mode = entry.value("mode", 0);
        if ((mid_point.mode != 0) && (!is_xy_pair(tan_in_json) || !is_xy_pair(tan_out_json))) {
            return false;
        }
        mid_point.position_x = entry["pos"].at(0).get<float>() + translation.x;
        mid_point.position_y = entry["pos"].at(1).get<float>() + translation.y;
        if (mid_point.mode != 0) {
            mid_point.tangent_in_x  = tan_in_json .at(0).get<float>();
            mid_point.tangent_in_y  = tan_in_json .at(1).get<float>();
            mid_point.tangent_out_x = tan_out_json.at(0).get<float>();
            mid_point.tangent_out_y = tan_out_json.at(1).get<float>();
        }
        mid_points.push_back(mid_point);
    }
    link.set_mid_points(std::move(mid_points));
    return true;
}

// Pushes the link's stored routing onto a window's canvas.
inline void apply_link_routing_to_canvas(ax::NodeEditor::EditorContext& node_editor, erhe::graph::Link& link)
{
    const ax::NodeEditor::LinkId link_id{&link};
    std::vector<ImVec2> positions;
    for (const erhe::graph::Link_mid_point& mid_point : link.get_mid_points()) {
        positions.push_back(ImVec2{mid_point.position_x, mid_point.position_y});
    }
    node_editor.SetLinkMidPoints(link_id, positions.data(), static_cast<int>(positions.size()));
    int index = 0;
    for (const erhe::graph::Link_mid_point& mid_point : link.get_mid_points()) {
        if (mid_point.mode != 0) {
            node_editor.SetLinkMidPointTangents(
                link_id, index, mid_point.mode,
                ImVec2{mid_point.tangent_in_x,  mid_point.tangent_in_y},
                ImVec2{mid_point.tangent_out_x, mid_point.tangent_out_y}
            );
        }
        ++index;
    }
    const erhe::graph::Link_curve_params& curve = link.get_curve_params();
    node_editor.SetLinkCurveParams(link_id, curve.tension, curve.continuity, curve.bias);
}

// Reads a window's canvas routing of the link into the same model form
// (without storing it) - used to compare and to adopt local edits.
inline void read_link_routing_from_canvas(
    ax::NodeEditor::EditorContext& node_editor,
    erhe::graph::Link&             link,
    std::vector<erhe::graph::Link_mid_point>& out_mid_points,
    erhe::graph::Link_curve_params&           out_curve_params
)
{
    const ax::NodeEditor::LinkId link_id{&link};
    out_mid_points.clear();
    const int mid_point_count = node_editor.GetLinkMidPointCount(link_id);
    for (int i = 0; i < mid_point_count; ++i) {
        erhe::graph::Link_mid_point mid_point;
        const ImVec2 position = node_editor.GetLinkMidPoint(link_id, i);
        mid_point.position_x = position.x;
        mid_point.position_y = position.y;
        mid_point.mode       = node_editor.GetLinkMidPointMode(link_id, i);
        if (mid_point.mode != 0) {
            ImVec2 tan_in {0.0f, 0.0f};
            ImVec2 tan_out{0.0f, 0.0f};
            node_editor.GetLinkMidPointTangents(link_id, i, &tan_in, &tan_out);
            mid_point.tangent_in_x  = tan_in.x;
            mid_point.tangent_in_y  = tan_in.y;
            mid_point.tangent_out_x = tan_out.x;
            mid_point.tangent_out_y = tan_out.y;
        }
        out_mid_points.push_back(mid_point);
    }
    out_curve_params = erhe::graph::Link_curve_params{};
    node_editor.GetLinkCurveParams(link_id, &out_curve_params.tension, &out_curve_params.continuity, &out_curve_params.bias);
}

} // namespace editor
