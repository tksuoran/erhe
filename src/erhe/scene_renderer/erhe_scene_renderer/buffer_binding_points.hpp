#pragma once

namespace erhe::scene_renderer {

#define material_buffer_binding_point      0

#define light_buffer_binding_point         1
#define light_control_buffer_binding_point 2

#define primitive_buffer_binding_point     3

#define camera_buffer_binding_point        4

#define cube_instance_buffer_binding_point 5
#define cube_control_buffer_binding_point  6

#define joint_buffer_binding_point         7

#define glyph_buffer_binding_point         8

// The lights of light_block in a storage block (doc/plans/clustered_lights.md
// C3). Metal maps a GLSL binding to the buffer index; 9-12 are the indices
// below its reserved ones.
#define light_array_buffer_binding_point   9
// The light cluster lists (doc/plans/clustered_lights.md C3).
#define light_cluster_buffer_binding_point 10
// The hot records of the shadow-mapped lights (Light_shadowed_struct) and
// their cold world-space shadow transforms (Light_shadow_world_struct).
#define light_shadowed_array_buffer_binding_point     11
#define light_shadow_world_array_buffer_binding_point 12
// Binding 13 is shared by two fragment-only storage blocks that no program
// declares together (Program_interface::make_prototype() adds one or the
// other, never both): Metal's index 13 is the ray query acceleration
// structure, which only compute shaders bind, 14 and 15 are its argument
// buffers and 16 up are its vertex buffers, so the render pipelines of this
// layout have no other index left.
// The potentially visible set bake's layered buffer (Pvs_layered_interface,
// doc/plans/pvs.md "Bake" 4): read and written by the standard shader's PVS
// variants only.
#define pvs_layered_buffer_binding_point 13
// The per-pixel fragment shader invocation counts of the overdraw variants
// (ERHE_OVERDRAW, res/shaders/erhe_overdraw.glsl).
#define overdraw_count_buffer_binding_point 13

} // namespace erhe::scene_renderer
