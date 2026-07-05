#pragma once

#include "geometry_graph/geometry_payload.hpp"

#include "erhe_graph/node.hpp"

#include <nlohmann/json_fwd.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct ImDrawList;

namespace ax::NodeEditor { class EditorContext; }

namespace editor {

class App_context;
class Geometry_graph;
class Graph_mesh;

// Applies the Geometry::process() flags every generator / operation node
// needs on its output geometry, so downstream operation nodes find
// connectivity and edges present. Final render oriented processing
// (normals, tangents, texture coordinates) happens in the scene output
// node.
void process_for_graph(erhe::geometry::Geometry& geometry);

// JSON helpers for node parameter (de)serialization.
void write_vec3 (nlohmann::json& out, const char* key, const glm::vec3& value);
void write_ivec3(nlohmann::json& out, const char* key, const glm::ivec3& value);
[[nodiscard]] auto read_vec3 (const nlohmann::json& in, const char* key, const glm::vec3& fallback) -> glm::vec3;
[[nodiscard]] auto read_ivec3(const nlohmann::json& in, const char* key, const glm::ivec3& fallback) -> glm::ivec3;

// Base class for all geometry graph nodes.
//
// Follows the Shader_graph_node pattern: payload storage per pin slot,
// accumulate-from-links input pulling, and ax::NodeEditor rendering with
// input pins on the left edge and output pins on the right edge.
//
// Nodes are born dirty; parameter edits in imgui() must call mark_dirty().
// Geometry_graph::evaluate() re-runs dirty nodes and everything downstream
// of them (dirtiness propagates along links in topological order); clean
// nodes keep their cached output payloads.
class Geometry_graph_node : public erhe::graph::Node
{
public:
    explicit Geometry_graph_node(const char* label);

    // Pulls and combines payloads from all links connected to input pin slot i.
    // See Geometry_payload::operator+=() for multi-link semantics.
    [[nodiscard]] auto accumulate_input_from_links(std::size_t i) const -> Geometry_payload;

    // Stores accumulate_input_from_links() into every input payload slot.
    // Call at the start of evaluate() so imgui() can display input values
    // via get_input() without re-pulling (geometry accumulation allocates).
    void pull_inputs();

    [[nodiscard]] auto get_input (std::size_t i) const -> const Geometry_payload&;
    [[nodiscard]] auto get_output(std::size_t i) const -> const Geometry_payload&;
    void set_input      (std::size_t i, const Geometry_payload& payload);
    void set_output     (std::size_t i, const Geometry_payload& payload);
    void make_input_pin (std::size_t key, std::string_view name);
    void make_output_pin(std::size_t key, std::string_view name);

    [[nodiscard]] auto is_dirty() const -> bool;
    void mark_dirty ();
    void clear_dirty();

    void node_editor(App_context& context, ax::NodeEditor::EditorContext& node_editor);

    virtual void evaluate(Geometry_graph& graph);
    virtual void imgui   ();

    // Called when the node leaves the graph (deletion, undo of add,
    // graph clear / load). The node object may stay alive in the undo
    // stack, so side effects outside the graph - like the scene mesh
    // owned by the output node - must be released here rather than in
    // the destructor.
    virtual void on_removed_from_graph();

    // Graph serialization: the factory type name is the
    // Geometry_graph_window factory name that recreates this node on
    // load (set by the factory); parameters are the node's editable
    // values. (Named to avoid clashing with erhe::Item::get_type_name().)
    [[nodiscard]] auto get_factory_type_name() const -> const std::string&;
    void set_factory_type_name(const std::string& type_name);
    virtual void write_parameters(nlohmann::json& out) const;
    virtual void read_parameters (const nlohmann::json& in);

    // Parameter undo support. The committed state is the write_parameters()
    // JSON dump the next Geometry_graph_parameter_operation uses as its
    // "before" side; node_editor() captures widget edit gestures against it
    // (one operation per completed gesture, pushed on widget deactivation).
    [[nodiscard]] auto dump_parameters() const -> std::string;
    [[nodiscard]] auto get_committed_parameters() const -> const std::string&;
    void set_committed_parameters(const std::string& parameters);

    // Shadow clones evaluated in the background log under the id of the
    // live node they mirror, so trace logs stay correlated with the ids
    // the UI and the MCP tools report. Unset (0) means "this node's own
    // id" (the normal case for live nodes).
    [[nodiscard]] auto get_log_id() const -> std::size_t;
    void set_log_source_id(std::size_t id);

    // The Graph_mesh ASSET this node belongs to; null for nodes of the
    // window's scratch graph (set by Geometry_graph_window::insert_node
    // for asset graphs only) and for shadow clones / group subgraph
    // nodes. The output node uses it to publish its evaluated products
    // to the asset instead of creating a self-owned scene node.
    [[nodiscard]] auto get_owning_graph_mesh() const -> std::shared_ptr<Graph_mesh>;
    void set_owning_graph_mesh(const std::shared_ptr<Graph_mesh>& graph_mesh);

protected:
    // Issue #251: node content is authored directly in screen space at the
    // zoomed size (the node editor pushes a zoom-scaled font + style around
    // node content). Canvas-unit pixel metrics baked into node content - table
    // column widths, per-widget widths, pin sizes - must be multiplied by this
    // view scale so the content lays out to match the zoomed node frame. Set
    // from EditorContext::GetCurrentZoom() at the top of node_editor().
    [[nodiscard]] auto content_scale() const -> float { return m_content_scale; }

    void show_pins(
        ax::NodeEditor::EditorContext&                        node_editor,
        ImDrawList&                                           draw_list,
        const etl::vector<erhe::graph::Pin, erhe::graph::max_pin_count>& pins,
        float                                                 edge_x,
        bool                                                  right_edge
    );

    std::vector<Geometry_payload> m_input_payloads;
    std::vector<Geometry_payload> m_output_payloads;
    std::weak_ptr<Graph_mesh>     m_owning_graph_mesh;
    std::string                   m_type_name;
    std::string                   m_committed_parameters;
    std::size_t                   m_log_source_id{0};
    float                         m_content_scale{1.0f};
    bool                          m_dirty{true};
    bool                          m_parameter_edit_in_progress{false};
};

}
