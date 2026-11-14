#include "erhe_scene_renderer/content_wide_line_interface.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene_renderer {

namespace {

constexpr int c_edge_line_vertex_binding_point        = 0;
constexpr int c_edge_line_joint_vertex_binding_point  = 2;
constexpr int c_view_binding_point                    = 3;

[[nodiscard]] auto make_block_binding(
    const erhe::graphics::Shader_resource& block,
    const uint32_t                         stage_flags // Shader_stage_flags bitmask
) -> erhe::graphics::Bind_group_layout_binding
{
    return erhe::graphics::Bind_group_layout_binding{
        .binding_point = block.get_binding_point(),
        .type          = (block.get_type() == erhe::graphics::Shader_resource::Type::shader_storage_block)
            ? erhe::graphics::Binding_type::storage_buffer
            : erhe::graphics::Binding_type::uniform_buffer,
        .stage_flags   = stage_flags
    };
}

} // anonymous namespace

Content_wide_line_interface::Content_wide_line_interface(
    erhe::graphics::Device&          graphics_device,
    erhe::graphics::Shader_resource* joint_block_,
    const int                        view_count_
)
    : fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = erhe::graphics::Glsl_type::float_vec4,
            .location = 0
        }
    }
    , view_camera_struct{graphics_device, "ContentLineViewCamera"}
    , view_block{
        graphics_device,
        "view",
        c_view_binding_point,
        erhe::graphics::Shader_resource::Type::uniform_block
    }
    , view_count{std::max(1, view_count_)}
    , joint_block{joint_block_}
{
    ERHE_VERIFY(view_count_ >= 1);

    // ViewCamera struct (one entry per element of cameras[view_count]).
    offsets.clip_from_world        = view_camera_struct.add_mat4("clip_from_world"       )->get_offset_in_parent();
    offsets.viewport               = view_camera_struct.add_vec4("viewport"              )->get_offset_in_parent();
    offsets.fov                    = view_camera_struct.add_vec4("fov"                   )->get_offset_in_parent();
    offsets.view_position_in_world = view_camera_struct.add_vec4("view_position_in_world")->get_offset_in_parent();
    // Camera_view_input::pixel_scale; the three pad words keep the struct a
    // whole number of 16-byte slots so the cameras[] array stride matches std140.
    offsets.pixel_scale            = view_camera_struct.add_float("pixel_scale"          )->get_offset_in_parent();
    view_camera_struct.add_float("_padding0");
    view_camera_struct.add_float("_padding1");
    view_camera_struct.add_float("_padding2");
    offsets.camera_stride          = view_camera_struct.get_size_bytes();

    // View UBO layout:
    //   ViewCamera cameras[view_count];     // per-eye camera
    //   mat4   world_from_node;             // per-dispatch
    //   vec4   line_color;                  // per-dispatch (.w = line_width)
    //   uint   edge_count;                  // per-dispatch
    //   uint   view_count;                  // mirrors view_count
    //   float  vp_y_sign;
    //   float  clip_depth_direction;
    //   uint   base_joint_index;            // per-dispatch, skinned variant only
    //   vec4   position_scale;              // per-dispatch vertex position dequantization
    //   vec4   position_offset;             //   affine (xyz used)
    //
    // Per-eye data is grouped into cameras[]; the multiview vertex shader
    // indexes it by gl_ViewIndex.
    view_block.add_struct("cameras", &view_camera_struct, static_cast<std::size_t>(view_count));
    offsets.world_from_node      = view_block.add_mat4 ("world_from_node"     )->get_offset_in_parent();
    offsets.line_color           = view_block.add_vec4 ("line_color"          )->get_offset_in_parent();
    offsets.edge_count           = view_block.add_uint ("edge_count"          )->get_offset_in_parent();
    offsets.view_count           = view_block.add_uint ("view_count"          )->get_offset_in_parent();
    offsets.vp_y_sign            = view_block.add_float("vp_y_sign"           )->get_offset_in_parent();
    offsets.clip_depth_direction = view_block.add_float("clip_depth_direction")->get_offset_in_parent();
    // Read only by the skinned variant; one view layout serves both.
    offsets.base_joint_index     = view_block.add_uint ("base_joint_index"    )->get_offset_in_parent();
    // Vertex position dequantization affine (see Content_wide_line_view_offsets).
    // vec4s, so they carry 16-byte alignment and land after the scalars above.
    offsets.position_scale       = view_block.add_vec4 ("position_scale"      )->get_offset_in_parent();
    offsets.position_offset      = view_block.add_vec4 ("position_offset"     )->get_offset_in_parent();

    edge_line_vertex_struct = std::make_unique<erhe::graphics::Shader_resource>(graphics_device, "edge_line_vertex");
    edge_line_vertex_buffer_block = std::make_unique<erhe::graphics::Shader_resource>(
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "edge_line_vertex_buffer",
            .binding_point = c_edge_line_vertex_binding_point,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    );
    edge_line_vertex_struct->add_vec4("position");
    edge_line_vertex_struct->add_vec4("normal");
    edge_line_vertex_buffer_block->add_struct("vertices", edge_line_vertex_struct.get(), erhe::graphics::Shader_resource::unsized_array);

    edge_line_joint_vertex_struct = std::make_unique<erhe::graphics::Shader_resource>(graphics_device, "edge_line_joint_vertex");
    edge_line_joint_vertex_buffer_block = std::make_unique<erhe::graphics::Shader_resource>(
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "edge_line_joint_buffer",
            .binding_point = c_edge_line_joint_vertex_binding_point,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    );
    edge_line_joint_vertex_struct->add_uvec4("joint_indices");
    edge_line_joint_vertex_struct->add_vec4 ("joint_weights");
    edge_line_joint_vertex_buffer_block->add_struct("vertices", edge_line_joint_vertex_struct.get(), erhe::graphics::Shader_resource::unsized_array);

    constexpr uint32_t vertex          = erhe::graphics::Shader_stage_flags::vertex;
    constexpr uint32_t vertex_fragment = erhe::graphics::Shader_stage_flags::vertex | erhe::graphics::Shader_stage_flags::fragment;

    // content_line_ribbon.vert reads the edge-line vertices and the view;
    // content_line_after_compute.frag reads the view.
    bind_group_layout = std::make_unique<erhe::graphics::Bind_group_layout>(
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                make_block_binding(*edge_line_vertex_buffer_block, vertex),
                make_block_binding(view_block, vertex_fragment)
            },
            .debug_label       = "Content wide line",
            .uses_texture_heap = false
        }
    );

    if (joint_block != nullptr) {
        skinned_bind_group_layout = std::make_unique<erhe::graphics::Bind_group_layout>(
            graphics_device,
            erhe::graphics::Bind_group_layout_create_info{
                .bindings = {
                    make_block_binding(*edge_line_vertex_buffer_block, vertex),
                    make_block_binding(*edge_line_joint_vertex_buffer_block, vertex),
                    make_block_binding(view_block, vertex_fragment),
                    make_block_binding(*joint_block, vertex)
                },
                .debug_label       = "Content wide line skinned",
                .uses_texture_heap = false
            }
        );
    }
}

} // namespace erhe::scene_renderer
