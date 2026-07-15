#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

#include <array>

namespace editor {

// One node type per Conway operator ("conway_ambo" ... "conway_gyro", see
// c_operation_infos), so each operator is its own palette entry - draggable
// to the canvas and into inventory / hotbar slots individually. The legacy
// "conway" type (one node with an operation combo) still constructs: its
// saved "operation" parameter is adopted by read_parameters(), which also
// renames the node and migrates its factory type name, so old graph assets
// load correctly and re-save as the specific per-operator type.
class Conway_node : public Geometry_graph_node
{
public:
    enum class Conway_operation : int {
        ambo     = 0,
        dual     = 1,
        join     = 2,
        kis      = 3,
        meta     = 4,
        subdivide= 5,
        truncate = 6,
        chamfer  = 7,
        gyro     = 8
    };

    // Operator table: the single source of truth for the per-operator factory
    // type names and the palette / node labels (the palette "Conway" category
    // is built from it). "Conway Join" / "Conway Subdivide" carry the prefix
    // because plain Join (combine) and Subdivide nodes already exist.
    class Operation_info
    {
    public:
        Conway_operation operation;
        const char*      type_name;
        const char*      label;
    };
    static constexpr std::array<Operation_info, 9> c_operation_infos{
        Operation_info{Conway_operation::ambo,      "conway_ambo",      "Ambo"},
        Operation_info{Conway_operation::dual,      "conway_dual",      "Dual"},
        Operation_info{Conway_operation::join,      "conway_join",      "Conway Join"},
        Operation_info{Conway_operation::kis,       "conway_kis",       "Kis"},
        Operation_info{Conway_operation::meta,      "conway_meta",      "Meta"},
        Operation_info{Conway_operation::subdivide, "conway_subdivide", "Conway Subdivide"},
        Operation_info{Conway_operation::truncate,  "conway_truncate",  "Truncate"},
        Operation_info{Conway_operation::chamfer,   "conway_chamfer",   "Chamfer"},
        Operation_info{Conway_operation::gyro,      "conway_gyro",      "Gyro"}
    };
    // The table entry for the operation, or nullptr when out of range (a
    // malformed graph file or the MCP set_parameter abuse path).
    [[nodiscard]] static auto find_operation_info(Conway_operation operation) -> const Operation_info*;

    explicit Conway_node(Conway_operation operation = Conway_operation::dual);

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

private:
    // Adopts an operation arriving through parameters (legacy "conway" files,
    // MCP parameter writes): renames the node and migrates its factory type
    // name when the value is in range; out-of-range values are kept (evaluate
    // then yields empty geometry) so a later in-range write recovers.
    void set_operation(Conway_operation operation);

    Conway_operation m_operation     {Conway_operation::dual};
    float            m_kis_height    {0.0f};
    float            m_truncate_ratio{1.0f / 3.0f};
    float            m_chamfer_ratio {0.25f};
    float            m_gyro_ratio    {1.0f / 3.0f};
};

}
