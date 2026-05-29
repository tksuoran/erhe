#include "erhe_scene_renderer/content_wide_line_interface.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene_renderer {

namespace {

constexpr int c_edge_line_vertex_binding_point        = 0;
constexpr int c_triangle_vertex_binding_point         = 1;
constexpr int c_edge_line_joint_vertex_binding_point  = 2;
constexpr int c_view_binding_point                    = 3;

[[nodiscard]] auto make_block_binding(
    const erhe::graphics::Shader_resource& block
) -> erhe::graphics::Bind_group_layout_binding
{
    return erhe::graphics::Bind_group_layout_binding{
        .binding_point = block.get_binding_point(),
        .type          = (block.get_type() == erhe::graphics::Shader_resource::Type::shader_storage_block)
            ? erhe::graphics::Binding_type::storage_buffer
            : erhe::graphics::Binding_type::uniform_buffer
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
    , triangle_vertex_format{
        {
            0,
            {
                {erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::position},
                {erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::color},
                {erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::custom, 0},
            }
        }
    }
    , view_camera_struct{graphics_device, "ContentLineViewCamera"}
    , view_block{
        graphics_device,
        "view",
        c_view_binding_point,
        erhe::graphics::Shader_resource::Type::uniform_block
    }
    , geometry_bind_group_layout_not_skinned{
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                make_block_binding(view_block)
            },
            .debug_label       = "Content wide line geometry",
            .uses_texture_heap = false
        }
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
    offsets.camera_stride          = view_camera_struct.get_size_bytes();

    // View UBO layout:
    //   ViewCamera cameras[view_count];     // per-eye camera
    //   mat4   world_from_node;             // per-dispatch
    //   vec4   line_color;                  // per-dispatch (.w = line_width)
    //   uint   edge_count;                  // per-dispatch
    //   uint   view_count;                  // mirrors view_count
    //   uint   stride_per_view;             // padded_edge_count * 6 vertices
    //   float  vp_y_sign;
    //   float  clip_depth_direction;
    //   uint   base_joint_index;            // per-dispatch, skinned variant only
    //   float  _padding;                    // pad to 16-byte boundary
    //
    // Per-eye data is grouped into cameras[] so the multiview compute
    // shader can write one triangle set per view in a single dispatch
    // (loop over views) and the multiview vertex shader can index its
    // SSBO read by gl_ViewIndex without a separate push constant.
    view_block.add_struct("cameras", &view_camera_struct, static_cast<std::size_t>(view_count));
    offsets.world_from_node      = view_block.add_mat4 ("world_from_node"     )->get_offset_in_parent();
    offsets.line_color           = view_block.add_vec4 ("line_color"          )->get_offset_in_parent();
    offsets.edge_count           = view_block.add_uint ("edge_count"          )->get_offset_in_parent();
    offsets.view_count           = view_block.add_uint ("view_count"          )->get_offset_in_parent();
    offsets.stride_per_view      = view_block.add_uint ("stride_per_view"     )->get_offset_in_parent();
    offsets.vp_y_sign            = view_block.add_float("vp_y_sign"           )->get_offset_in_parent();
    offsets.clip_depth_direction = view_block.add_float("clip_depth_direction")->get_offset_in_parent();
    // base_joint_index is read only by the skinned variants under
    // ERHE_USE_SKINNING or ERHE_ATTRIBUTE_a_joint_weights_0; the non-
    // skinned variants ignore it. Keeping the field in the shared view
    // block avoids two view layouts.
    offsets.base_joint_index     = view_block.add_uint ("base_joint_index"    )->get_offset_in_parent();
    offsets.padding0             = view_block.add_float("_padding0"           )->get_offset_in_parent();

    // Skinned geometry-shader bind group layout: view UBO + global joint
    // block. The vertex shader reads a_joint_indices_0 + a_joint_weights_0
    // through the input assembler and indexes joint.joints[] with them;
    // no SSBO bindings, so this layout is built whenever joint_block is
    // supplied regardless of whether SSBOs are available.
    if (joint_block != nullptr) {
        geometry_bind_group_layout_skinned = std::make_unique<erhe::graphics::Bind_group_layout>(
            graphics_device,
            erhe::graphics::Bind_group_layout_create_info{
                .bindings = {
                    make_block_binding(view_block),
                    make_block_binding(*joint_block)
                },
                .debug_label       = "Content wide line geometry skinned",
                .uses_texture_heap = false
            }
        );
    }

    // SSBO-backed compute resources are only built on devices that
    // support shader storage buffers. The geometry-shader backend never
    // touches these; it uses only the view UBO + joint UBO above.
    if (!graphics_device.get_info().use_shader_storage_buffers) {
        return;
    }

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

    triangle_vertex_struct = std::make_unique<erhe::graphics::Shader_resource>(graphics_device, "triangle_vertex");
    triangle_vertex_buffer_block = std::make_unique<erhe::graphics::Shader_resource>(
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "triangle_vertex_buffer",
            .binding_point = c_triangle_vertex_binding_point,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .writeonly     = true
        }
    );
    erhe::graphics::add_vertex_stream(
        triangle_vertex_format.streams.front(),
        *triangle_vertex_struct,
        *triangle_vertex_buffer_block
    );
    triangle_vertex_buffer_read_block = std::make_unique<erhe::graphics::Shader_resource>(
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "triangle_vertex_buffer",
            .binding_point = c_triangle_vertex_binding_point,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    );
    triangle_vertex_buffer_read_block->add_struct("vertices", triangle_vertex_struct.get(), erhe::graphics::Shader_resource::unsized_array);

    bind_group_layout = std::make_unique<erhe::graphics::Bind_group_layout>(
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                {c_edge_line_vertex_binding_point, erhe::graphics::Binding_type::storage_buffer},
                {c_triangle_vertex_binding_point,  erhe::graphics::Binding_type::storage_buffer},
                make_block_binding(view_block)
            },
            .debug_label       = "Content wide line",
            .uses_texture_heap = false
        }
    );

    graphics_bind_group_layout = std::make_unique<erhe::graphics::Bind_group_layout>(
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                {c_triangle_vertex_binding_point, erhe::graphics::Binding_type::storage_buffer},
                make_block_binding(view_block)
            },
            .debug_label       = "Content wide line graphics",
            .uses_texture_heap = false
        }
    );

    // Skinned bind group layout: same bindings as bind_group_layout plus
    // the joint side-buffer SSBO and the global `joint` block. Built only
    // when joint_block is supplied; otherwise stays null and the skinned
    // compute pipeline is not constructed.
    if (joint_block != nullptr) {
        skinned_bind_group_layout = std::make_unique<erhe::graphics::Bind_group_layout>(
            graphics_device,
            erhe::graphics::Bind_group_layout_create_info{
                .bindings = {
                    {c_edge_line_vertex_binding_point,       erhe::graphics::Binding_type::storage_buffer},
                    {c_triangle_vertex_binding_point,        erhe::graphics::Binding_type::storage_buffer},
                    {c_edge_line_joint_vertex_binding_point, erhe::graphics::Binding_type::storage_buffer},
                    make_block_binding(view_block),
                    make_block_binding(*joint_block)
                },
                .debug_label       = "Content wide line skinned",
                .uses_texture_heap = false
            }
        );
    }
}

} // namespace erhe::scene_renderer
