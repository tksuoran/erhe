#pragma once

#include "erhe_scene_renderer/draw_list_object.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace erhe::graphics {
    class Buffer;
}

namespace erhe::scene_renderer {

// The edge lines of one level of detail of a mesh primitive
// (Content_wide_line_feed::level_edges): the edge-line vertex pairs and,
// for a skinned mesh, their per-endpoint joint data.
class Content_wide_line_level_edges
{
public:
    erhe::graphics::Buffer* edge_buffer             {nullptr};
    std::size_t             edge_buffer_byte_offset {0};
    std::size_t             edge_count              {0};
    erhe::graphics::Buffer* joint_buffer            {nullptr};
    std::size_t             joint_buffer_byte_offset{0};
    std::size_t             joint_buffer_byte_size  {0};
};

// One mesh primitive's edge lines as Content_wide_line_renderer draws them:
// everything Content_wide_line_renderer::prepare_mesh() read of the mesh, so
// the render phase queues the dispatch without touching a Mesh, Node, Skin or
// Primitive. The buffers are Mesh_memory pool buffers (stable for the pool's
// lifetime).
class Content_wide_line_record
{
public:
    // The primitive's edge-line vertex pairs: those of the level the
    // record was prepared with (Content_wide_line_renderer::prepare_mesh()
    // lod_level; the original variant's for level 0). A record with
    // level_edge_count > 0 replaces them in add_feed() by the level its
    // color entry draws in the view (see below).
    erhe::graphics::Buffer* edge_buffer             {nullptr};
    std::size_t             edge_buffer_byte_offset {0};
    std::size_t             edge_count              {0};
    glm::mat4               world_from_node         {1.0f};
    glm::vec4               color                   {1.0f};
    float                   line_width              {1.0f};
    uint32_t                group                   {0};
    // The mesh has a skin. With joint_buffer non-null (the primitive has
    // per-endpoint joint data) and a renderer with the skinned compute
    // path, the dispatch is skinned with base_joint_index (the skin's joint
    // slot, assigned by the frame's Joint_poses::capture).
    bool                    mesh_is_skinned         {false};
    erhe::graphics::Buffer* joint_buffer            {nullptr};
    std::size_t             joint_buffer_byte_offset{0};
    std::size_t             joint_buffer_byte_size  {0};
    uint32_t                base_joint_index        {0};
    // Level of detail (doc/editor/lod.md "Edge lines and the selection
    // outline"): the edge lines of every level of the drawn variant's chain,
    // level 0 first, at [first_level_edges, first_level_edges +
    // level_edge_count) of the feed's level_edges (0 levels: the primitive
    // has no chain), and the primitive's color entry in the frame's
    // published draw lists (invalid without one). add_feed() draws the
    // level that entry draws in the view (Draw_list_scene::
    // get_published_entry_lod_level()).
    uint32_t                  first_level_edges{0};
    uint32_t                  level_edge_count {0};
    Draw_list_published_entry color_entry      {};
};

// The edge lines of one view and one frame: the records of every mesh the
// edge-line passes feed (Content_wide_line_renderer::prepare_mesh), in feed
// order. Storage a caller keeps per view and frame in flight; clear() keeps
// the capacity.
class Content_wide_line_feed
{
public:
    void clear()
    {
        records.clear();
        level_edges.clear();
        mesh_count = 0;
    }

    std::vector<Content_wide_line_record>      records;
    // The records' per-level edge lines (Content_wide_line_record::
    // first_level_edges).
    std::vector<Content_wide_line_level_edges> level_edges;
    // Meshes fed (whether or not a primitive produced a record): the render
    // phase skips the joint buffer update and the compute pass at 0.
    std::size_t                           mesh_count{0};
};

} // namespace erhe::scene_renderer
