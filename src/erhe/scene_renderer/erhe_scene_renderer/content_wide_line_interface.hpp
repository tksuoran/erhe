#pragma once

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/shader_resource.hpp"

#include <cstddef>
#include <memory>

namespace erhe::graphics {
    class Device;
}

namespace erhe::scene_renderer {

// Field offsets inside the view UBO. The ViewCamera entries form the
// cameras[view_count] array prefix; the remaining fields follow.
class Content_wide_line_view_offsets
{
public:
    // ViewCamera entry layout (repeated view_count times at the start of the block).
    std::size_t clip_from_world       {0};
    std::size_t viewport              {0};
    std::size_t fov                   {0};
    std::size_t view_position_in_world{0};
    std::size_t pixel_scale           {0};
    std::size_t camera_stride         {0};

    // Outer view block fields, after cameras[].
    std::size_t world_from_node       {0};
    std::size_t line_color            {0};
    std::size_t edge_count            {0};
    std::size_t view_count            {0};
    std::size_t vp_y_sign             {0};
    std::size_t clip_depth_direction  {0};
    std::size_t base_joint_index      {0};
    // Vertex position dequantization affine for the dispatched primitive, as
    // vec4s (xyz used); written per dispatch by write_view_block. The view
    // block has no primitive block in scope, so the affine travels here.
    std::size_t position_scale        {0};
    std::size_t position_offset       {0};
};

// Shader interface of the content wide-line renderer: the edge-line
// vertex (and joint) storage blocks the ribbon vertex shader reads, the view
// UBO, the fragment outputs and the bind group layouts. Built once at app
// initialization (doc/erhe/scene_renderer.md "Content wide lines").
class Content_wide_line_interface
{
public:
    // joint_block: the global `joint` block (Joint_interface's); nullptr
    // disables the skinned path (skinned_bind_group_layout stays null).
    // view_count: size of the view UBO's cameras[] array; >= 2 for multiview.
    Content_wide_line_interface(
        erhe::graphics::Device&          graphics_device,
        erhe::graphics::Shader_resource* joint_block,
        int                              view_count
    );

    erhe::graphics::Fragment_outputs fragment_outputs;

    std::unique_ptr<erhe::graphics::Shader_resource> edge_line_vertex_struct;
    std::unique_ptr<erhe::graphics::Shader_resource> edge_line_vertex_buffer_block;
    std::unique_ptr<erhe::graphics::Shader_resource> edge_line_joint_vertex_struct;
    std::unique_ptr<erhe::graphics::Shader_resource> edge_line_joint_vertex_buffer_block;
    erhe::graphics::Shader_resource  view_camera_struct;
    erhe::graphics::Shader_resource  view_block;

    // Edge-line vertex SSBO + view UBO.
    std::unique_ptr<erhe::graphics::Bind_group_layout> bind_group_layout;
    // Adds the edge-line joint SSBO and the `joint` block; null without joint_block.
    std::unique_ptr<erhe::graphics::Bind_group_layout> skinned_bind_group_layout;

    Content_wide_line_view_offsets   offsets;
    int                              view_count{1};
    erhe::graphics::Shader_resource* joint_block{nullptr}; // owned by Joint_interface
};

} // namespace erhe::scene_renderer
