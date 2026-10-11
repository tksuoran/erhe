#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace erhe::graphics {
    class Buffer;
}

namespace erhe::scene_renderer {

// One mesh primitive's edge lines as Content_wide_line_renderer draws them:
// everything Content_wide_line_renderer::prepare_mesh() read of the mesh, so
// the render phase queues the dispatch without touching a Mesh, Node, Skin or
// Primitive. The buffers are Mesh_memory pool buffers (stable for the pool's
// lifetime).
class Content_wide_line_record
{
public:
    // The primitive's edge-line vertex pairs (original variant).
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
        mesh_count = 0;
    }

    std::vector<Content_wide_line_record> records;
    // Meshes fed (whether or not a primitive produced a record): the render
    // phase skips the joint buffer update and the compute pass at 0.
    std::size_t                           mesh_count{0};
};

} // namespace erhe::scene_renderer
