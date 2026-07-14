#pragma once

#include "geometry_graph/geometry_payload.hpp"
#include "graph_editor/graph_editor_node.hpp"

#include <nlohmann/json_fwd.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ax::NodeEditor { class EditorContext; }
namespace erhe::graphics { class Texture; }
namespace erhe::primitive { class Primitive; }
namespace erhe::scene_renderer { class Mesh_memory; }

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
class Geometry_graph_node : public Graph_editor_node
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

    virtual void evaluate(Geometry_graph& graph);

    // True for terminal nodes that publish scene products (the output
    // node). Geometry_graph::evaluate() runs these in a final phase so a
    // display / ghost designation can read any other node's payload
    // regardless of topological order, and Geometry_graph's designation
    // setters mark them dirty. Group_output_node is a subgraph pin, not a
    // scene output.
    [[nodiscard]] virtual auto is_scene_output() const -> bool;

    // Called when the node leaves the graph (deletion, undo of add,
    // graph clear / load). The node object may stay alive in the undo
    // stack, so side effects outside the graph - like the scene mesh
    // owned by the output node - must be released here rather than in
    // the destructor.
    virtual void on_removed_from_graph();

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

    // Per-node mesh preview thumbnails (texture-graph-style; enabled per
    // asset via Graph_mesh::set_node_previews_enabled). Split like the
    // output node's bake: build_preview_primitive() runs on the evaluation
    // worker (shadow nodes) right after evaluate(); take_preview() moves
    // the built primitive onto the live node on the main thread and arms
    // m_preview_needs_render; Geometry_graph_window::update_node_previews()
    // then renders it into m_preview_texture with a per-frame budget, and
    // after_node_content() draws the texture on the canvas.
    void build_preview_primitive(erhe::scene_renderer::Mesh_memory& mesh_memory);
    void take_preview(Geometry_graph_node& from);
    [[nodiscard]] auto get_preview_primitive() const -> const std::shared_ptr<erhe::primitive::Primitive>&;
    [[nodiscard]] auto preview_needs_render() const -> bool;
    void clear_preview_needs_render();
    [[nodiscard]] auto get_preview_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&;
    void set_preview_texture(const std::shared_ptr<erhe::graphics::Texture>& texture);

protected:
    // Implements Graph_editor_node
    [[nodiscard]] auto pin_key_color(std::size_t key) const -> ImU32 override;
    void commit_parameter_operation(App_context& context, std::string&& before_parameters, std::string&& after_parameters) override;
    // Draws the display ("D") / ghost ("G") designation badges under the
    // node content (Houdini display / template flags). Runs after the
    // parameter-undo dirty-delta bracket in Graph_editor_node::node_editor,
    // so the toggle cannot fabricate a spurious parameter operation; the
    // click executes an undoable Geometry_graph_display_designation_operation
    // instead.
    void after_node_content(App_context& context) override;

    // Draws the preview thumbnail (see the preview block above); called
    // from after_node_content() when the owning asset has previews on.
    void draw_preview(App_context& context);

    std::vector<Geometry_payload> m_input_payloads;
    std::vector<Geometry_payload> m_output_payloads;
    std::weak_ptr<Graph_mesh>     m_owning_graph_mesh;
    std::size_t                   m_log_source_id{0};
    std::shared_ptr<erhe::primitive::Primitive> m_preview_primitive;
    std::shared_ptr<erhe::graphics::Texture>    m_preview_texture;
    bool                                        m_preview_needs_render{false};
};

}
