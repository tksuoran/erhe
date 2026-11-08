#pragma once

#include "graph_editor/graph_operations.hpp"

#include "geometry_graph/graph_mesh.hpp"

#include <cstddef>
#include <string_view>

namespace editor {

class Geometry_graph_node;
class Geometry_graph_window;

// Binds the shared graph undo / redo operations to the geometry graph. The
// operation bodies live in graph_editor/graph_operations.hpp; distinct Traits
// give distinct instantiations, so the geometry and texture graphs no longer
// need identically-shaped copies kept apart by renaming.
class Geometry_graph_op_traits
{
public:
    using Window = Geometry_graph_window;
    using Asset  = Graph_mesh;
    using Node   = Geometry_graph_node;
    static constexpr const char* label = "Geometry graph";
};

using Geometry_graph_link_record                  = Graph_link_record<Geometry_graph_node>;
using Geometry_graph_node_insert_remove_operation = Graph_node_insert_remove_operation<Geometry_graph_op_traits>;
using Geometry_graph_parameter_operation          = Graph_parameter_operation<Geometry_graph_op_traits>;
using Geometry_graph_link_insert_remove_operation = Graph_link_insert_remove_operation<Geometry_graph_op_traits>;

// Undoable change of the graph's display / ghost node designation (Houdini
// display / template flags; see Geometry_graph::set_display_node_id). One
// operation captures both ids before / after, so moving a badge from node A
// to node B (which implicitly un-badges A) is a single undo step. The graph's
// setters mark the scene-output nodes dirty, so a background re-bake follows
// each apply automatically.
class Geometry_graph_display_designation_operation : public Operation
{
public:
    Geometry_graph_display_designation_operation(
        const std::shared_ptr<Graph_mesh>& graph_mesh,
        const std::string_view             node_name,
        const std::size_t                  before_display_id,
        const std::size_t                  before_ghost_id,
        const std::size_t                  after_display_id,
        const std::size_t                  after_ghost_id
    )
        : m_graph_mesh       {graph_mesh}
        , m_before_display_id{before_display_id}
        , m_before_ghost_id  {before_ghost_id}
        , m_after_display_id {after_display_id}
        , m_after_ghost_id   {after_ghost_id}
    {
        set_description(fmt::format("Geometry graph set display/ghost flags at '{}'", node_name));
    }

    // Implements Operation
    void execute(App_context&) override { apply(m_after_display_id,  m_after_ghost_id);  }
    void undo   (App_context&) override { apply(m_before_display_id, m_before_ghost_id); }

private:
    void apply(const std::size_t display_id, const std::size_t ghost_id)
    {
        m_graph_mesh->graph().set_display_node_id(display_id);
        m_graph_mesh->graph().set_ghost_node_id(ghost_id);
    }

    std::shared_ptr<Graph_mesh> m_graph_mesh;
    std::size_t                 m_before_display_id;
    std::size_t                 m_before_ghost_id;
    std::size_t                 m_after_display_id;
    std::size_t                 m_after_ghost_id;
};

} // namespace editor
