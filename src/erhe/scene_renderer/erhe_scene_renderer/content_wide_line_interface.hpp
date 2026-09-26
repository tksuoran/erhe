#pragma once

#include "erhe_dataformat/vertex_format.hpp"
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
    std::size_t stride_per_view       {0};
    std::size_t vp_y_sign             {0};
    std::size_t clip_depth_direction  {0};
    std::size_t base_joint_index      {0};
    // Tent wide-line method fields (see compute_before_content_line.comp).
    // line_bias_margin: surface-line depth-bias headroom in depth-buffer
    //   resolvable units (ULPs); window_to_ndc_scale: d(window)/d(ndc) for the
    //   depth-range convention (1.0 zero_to_one, 2.0 minus_one_to_one);
    //   use_tent: 0 = simple quad, 1 = two-face tent; line_bias_clamp: max
    //   toward-camera face-plane extrapolation per corner (ULPs), anti
    //   show-through.
    std::size_t line_bias_margin      {0};
    std::size_t window_to_ndc_scale   {0};
    std::size_t use_tent              {0};
    std::size_t line_bias_clamp       {0};
    // Vertex position dequantization affine for the dispatched primitive, as
    // vec4s (xyz used); written per dispatch by write_view_block. The view
    // block has no primitive block in scope, so the affine travels here.
    std::size_t position_scale        {0};
    std::size_t position_offset       {0};
};

// Shader-interface description for the content wide-line renderer. Owns
// the Shader_resource objects, bind group layouts, vertex format, fragment
// outputs, and view UBO offsets POD. Built once at app initialization;
// consumed by the app to compile the compute + graphics shader stages and
// by Content_wide_line_renderer at runtime.
//
// Mirrors the Camera_interface / Joint_interface pattern in this same
// library: shader-interface owner is separate from the per-frame ring
// buffer client.
class Content_wide_line_interface
{
public:
    // joint_block: shader resource describing the global `joint` interface
    //   block that the skinned compute variant reads to apply linear-blend
    //   skinning. Pass the same Shader_resource that Joint_interface owns;
    //   nullptr disables the skinned compute path entirely (skinned_bind_group_layout
    //   stays null; non-skinned dispatches still work).
    // view_count: size N of the cameras[N] array inside the view UBO
    //   block (and the per-view stride of the triangle SSBO). 1 =
    //   single-view (default). >= 2 = stereo / OpenXR multiview. Mirrors
    //   Camera_interface::view_count.
    Content_wide_line_interface(
        erhe::graphics::Device&          graphics_device,
        erhe::graphics::Shader_resource* joint_block,
        int                              view_count
    );

    erhe::graphics::Fragment_outputs fragment_outputs;
    erhe::dataformat::Vertex_format  triangle_vertex_format;

    // SSBO resources consumed by the compute pre-pass and the SSBO-read
    // graphics path.
    std::unique_ptr<erhe::graphics::Shader_resource> edge_line_vertex_struct;
    std::unique_ptr<erhe::graphics::Shader_resource> edge_line_vertex_buffer_block;
    std::unique_ptr<erhe::graphics::Shader_resource> edge_line_joint_vertex_struct;
    std::unique_ptr<erhe::graphics::Shader_resource> edge_line_joint_vertex_buffer_block;
    std::unique_ptr<erhe::graphics::Shader_resource> triangle_vertex_struct;
    std::unique_ptr<erhe::graphics::Shader_resource> triangle_vertex_buffer_block;
    // Same descriptor set binding as triangle_vertex_buffer_block but
    // declared readonly. The compute shader uses the writeonly block
    // (cannot be read); the multiview vertex shader uses this read-only
    // block to fetch the per-view triangle it must emit. Different
    // declarations of the same descriptor set binding are legal in Vulkan.
    std::unique_ptr<erhe::graphics::Shader_resource>  triangle_vertex_buffer_read_block;
    erhe::graphics::Shader_resource  view_camera_struct;
    erhe::graphics::Shader_resource  view_block;

    // Compute bind group layout: contains the line SSBO, the triangle
    // SSBO, and the view UBO.
    std::unique_ptr<erhe::graphics::Bind_group_layout> bind_group_layout;
    // Skinned-variant bind group layout: adds the edge-line joint side
    // buffer SSBO + the `joint` block. nullptr when joint_block was
    // nullptr at construction (skinned compute path disabled).
    std::unique_ptr<erhe::graphics::Bind_group_layout> skinned_bind_group_layout;
    // Graphics pipeline layout used by both single-view and multiview
    // SSBO-read render paths: triangle SSBO + view UBO.
    std::unique_ptr<erhe::graphics::Bind_group_layout> graphics_bind_group_layout;

    Content_wide_line_view_offsets   offsets;
    int                              view_count{1};
    // Not owned; lifetime managed by Joint_interface. nullptr when the
    // skinned compute variant is disabled.
    erhe::graphics::Shader_resource* joint_block{nullptr};
};

} // namespace erhe::scene_renderer
