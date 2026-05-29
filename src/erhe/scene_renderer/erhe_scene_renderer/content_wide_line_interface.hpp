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
    std::size_t padding0              {0};
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

    erhe::graphics::Shader_resource  edge_line_vertex_struct;
    erhe::graphics::Shader_resource  edge_line_vertex_buffer_block;
    erhe::graphics::Shader_resource  edge_line_joint_vertex_struct;
    erhe::graphics::Shader_resource  edge_line_joint_vertex_buffer_block;
    erhe::graphics::Shader_resource  triangle_vertex_struct;
    erhe::graphics::Shader_resource  triangle_vertex_buffer_block;
    // Same descriptor set binding as triangle_vertex_buffer_block but
    // declared readonly. The compute shader uses the writeonly block
    // (cannot be read); the multiview vertex shader uses this read-only
    // block to fetch the per-view triangle it must emit. Different
    // declarations of the same descriptor set binding are legal in Vulkan.
    erhe::graphics::Shader_resource  triangle_vertex_buffer_read_block;
    erhe::graphics::Shader_resource  view_camera_struct;
    erhe::graphics::Shader_resource  view_block;

    erhe::graphics::Bind_group_layout                  bind_group_layout;
    // Skinned-variant bind group layout: adds the edge-line joint side
    // buffer SSBO + the `joint` block at binding 7. nullptr when
    // joint_block was nullptr at construction (skinned compute path disabled).
    std::unique_ptr<erhe::graphics::Bind_group_layout> skinned_bind_group_layout;
    // Graphics pipeline layout used by both single-view and multiview
    // render paths: triangle SSBO (binding 1, read by vertex stage), view
    // UBO (binding 3, read by vertex stage for stride_per_view and by
    // fragment stage for cameras[c_view_index].viewport.xy). Distinct
    // from the compute layout because the compute side also needs the
    // line-vertex SSBO at binding 0 and writes the triangle SSBO; this
    // graphics-side layout omits the line input and binds the triangle
    // buffer read-only.
    erhe::graphics::Bind_group_layout                  graphics_bind_group_layout;

    Content_wide_line_view_offsets   offsets;
    int                              view_count{1};
    // Not owned; lifetime managed by Joint_interface. nullptr when the
    // skinned compute variant is disabled.
    erhe::graphics::Shader_resource* joint_block{nullptr};
};

} // namespace erhe::scene_renderer
