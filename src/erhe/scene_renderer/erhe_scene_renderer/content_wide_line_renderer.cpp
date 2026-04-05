#include "erhe_scene_renderer/content_wide_line_renderer.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/gtc/type_ptr.hpp>

namespace erhe::scene_renderer {

Content_wide_line_renderer::Content_wide_line_renderer(
    erhe::graphics::Device&        graphics_device,
    erhe::graphics::Buffer&        edge_line_vertex_buffer,
    erhe::graphics::Shader_stages* compute_shader_stages,
    erhe::graphics::Shader_stages* graphics_shader_stages
)
    : m_graphics_device        {graphics_device}
    , m_edge_line_vertex_buffer{edge_line_vertex_buffer}
    , m_fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = erhe::graphics::Glsl_type::float_vec4,
            .location = 0
        }
    }
    , m_triangle_vertex_format{
        {
            0,
            {
                {erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::position},
                {erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::color},
                {erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::custom, 0},
            }
        }
    }
    , m_compute_shader_stages {compute_shader_stages}
    , m_graphics_shader_stages{graphics_shader_stages}
    , m_vertex_input{
        graphics_device,
        erhe::graphics::Vertex_input_state_data::make(m_triangle_vertex_format)
    }
    , m_view_buffer{
        graphics_device,
        erhe::graphics::Buffer_target::uniform,
        "Content_wide_line_renderer::m_view_buffer",
        3 // view binding point
    }
    , m_triangle_vertex_buffer_client{
        graphics_device,
        erhe::graphics::Buffer_target::storage,
        "Content_wide_line_renderer::m_triangle_vertex_buffer",
        1 // output triangle SSBO binding point
    }
{
    if (!graphics_device.get_info().use_compute_shader) {
        m_enabled = false;
        return;
    }

    // Edge line vertex struct for the input SSBO
    m_edge_line_vertex_struct = std::make_unique<erhe::graphics::Shader_resource>(graphics_device, "edge_line_vertex");
    m_edge_line_vertex_struct->add_vec4("position");
    m_edge_line_vertex_struct->add_vec4("normal");

    m_edge_line_vertex_buffer_block = std::make_unique<erhe::graphics::Shader_resource>(
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "edge_line_vertex_buffer",
            .binding_point = 0,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    );
    m_edge_line_vertex_buffer_block->add_struct("vertices", m_edge_line_vertex_struct.get(), erhe::graphics::Shader_resource::unsized_array);

    // Triangle output SSBO
    m_triangle_vertex_struct = std::make_unique<erhe::graphics::Shader_resource>(graphics_device, "triangle_vertex");
    m_triangle_vertex_buffer_block = std::make_unique<erhe::graphics::Shader_resource>(
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "triangle_vertex_buffer",
            .binding_point = 1,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .writeonly     = true
        }
    );
    erhe::graphics::add_vertex_stream(
        m_triangle_vertex_format.streams.front(),
        *m_triangle_vertex_struct.get(),
        *m_triangle_vertex_buffer_block.get()
    );

    // View UBO (per-dispatch: camera + primitive transform + color)
    m_view_block = std::make_unique<erhe::graphics::Shader_resource>(
        graphics_device,
        "view",
        3,
        erhe::graphics::Shader_resource::Type::uniform_block
    );
    m_clip_from_world_offset        = m_view_block->add_mat4("clip_from_world")->get_offset_in_parent();
    m_world_from_node_offset        = m_view_block->add_mat4("world_from_node")->get_offset_in_parent();
    m_viewport_offset               = m_view_block->add_vec4("viewport"       )->get_offset_in_parent();
    m_fov_offset                    = m_view_block->add_vec4("fov"            )->get_offset_in_parent();
    m_line_color_offset             = m_view_block->add_vec4("line_color"     )->get_offset_in_parent();
    m_edge_count_offset             = m_view_block->add_uint ("edge_count"             )->get_offset_in_parent();
    m_vp_y_sign_offset              = m_view_block->add_float("vp_y_sign"              )->get_offset_in_parent();
    m_clip_depth_direction_offset   = m_view_block->add_float("clip_depth_direction"   )->get_offset_in_parent();
    m_padding0_offset               = m_view_block->add_float("_padding0"              )->get_offset_in_parent();
    m_padding1_offset               = m_view_block->add_float("_padding1"              )->get_offset_in_parent();
    m_view_position_in_world_offset = m_view_block->add_vec4 ("view_position_in_world" )->get_offset_in_parent();

    m_bind_group_layout = std::make_unique<erhe::graphics::Bind_group_layout>(
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                {m_edge_line_vertex_buffer_block->get_binding_point(), erhe::graphics::Binding_type::storage_buffer},
                {m_triangle_vertex_buffer_block->get_binding_point(),  erhe::graphics::Binding_type::storage_buffer},
                {m_view_block->get_binding_point(),
                    (m_view_block->get_type() == erhe::graphics::Shader_resource::Type::shader_storage_block)
                        ? erhe::graphics::Binding_type::storage_buffer
                        : erhe::graphics::Binding_type::uniform_buffer},
            },
            .debug_label = "Content wide line"
        }
    );

    if ((m_compute_shader_stages != nullptr) && m_compute_shader_stages->is_valid() &&
        (m_graphics_shader_stages != nullptr) && m_graphics_shader_stages->is_valid()) {
        m_compute_pipeline.emplace(
            erhe::graphics::Compute_pipeline_data{
                .name          = "compute_before_content_line",
                .shader_stages = m_compute_shader_stages
            }
        );
        m_enabled = true;
    }
}

Content_wide_line_renderer::~Content_wide_line_renderer() noexcept = default;

auto Content_wide_line_renderer::is_enabled() const -> bool
{
    return m_enabled;
}

void Content_wide_line_renderer::set_shader_stages(
    erhe::graphics::Shader_stages* compute_shader_stages,
    erhe::graphics::Shader_stages* graphics_shader_stages
)
{
    m_compute_shader_stages  = compute_shader_stages;
    m_graphics_shader_stages = graphics_shader_stages;

    if ((m_compute_shader_stages != nullptr) && m_compute_shader_stages->is_valid() &&
        (m_graphics_shader_stages != nullptr) && m_graphics_shader_stages->is_valid()) {
        m_compute_pipeline.emplace(
            erhe::graphics::Compute_pipeline_data{
                .name          = "compute_before_content_line",
                .shader_stages = m_compute_shader_stages
            }
        );
        m_enabled = true;
    }
}

auto Content_wide_line_renderer::get_edge_line_vertex_struct      () const -> erhe::graphics::Shader_resource* { return m_edge_line_vertex_struct.get(); }
auto Content_wide_line_renderer::get_edge_line_vertex_buffer_block() const -> erhe::graphics::Shader_resource* { return m_edge_line_vertex_buffer_block.get(); }
auto Content_wide_line_renderer::get_triangle_vertex_struct       () const -> erhe::graphics::Shader_resource* { return m_triangle_vertex_struct.get(); }
auto Content_wide_line_renderer::get_triangle_vertex_buffer_block () const -> erhe::graphics::Shader_resource* { return m_triangle_vertex_buffer_block.get(); }
auto Content_wide_line_renderer::get_view_block                   () const -> erhe::graphics::Shader_resource* { return m_view_block.get(); }
auto Content_wide_line_renderer::get_bind_group_layout            () const -> erhe::graphics::Bind_group_layout* { return m_bind_group_layout.get(); }
auto Content_wide_line_renderer::get_fragment_outputs             () -> erhe::graphics::Fragment_outputs&       { return m_fragment_outputs; }
auto Content_wide_line_renderer::get_triangle_vertex_format       () -> erhe::dataformat::Vertex_format&       { return m_triangle_vertex_format; }
auto Content_wide_line_renderer::get_vertex_input                 () -> erhe::graphics::Vertex_input_state*    { return &m_vertex_input; }
auto Content_wide_line_renderer::get_graphics_shader_stages       () -> erhe::graphics::Shader_stages*         { return m_graphics_shader_stages; }

void Content_wide_line_renderer::begin_frame()
{
    if (!m_enabled) {
        return;
    }

    m_dispatches.clear();
}

void Content_wide_line_renderer::add_mesh(
    const erhe::scene::Mesh& mesh,
    const glm::vec4&         color,
    const float              line_width,
    const uint32_t           group
)
{
    if (!m_enabled) {
        return;
    }

    const erhe::scene::Node* node = mesh.get_node();
    if (node == nullptr) {
        return;
    }

    const glm::mat4 world_from_node = node->world_from_node();

    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh.get_primitives()) {
        if (!mesh_primitive.primitive) {
            continue;
        }
        const erhe::primitive::Buffer_mesh* buffer_mesh = mesh_primitive.primitive->get_renderable_mesh();
        if (buffer_mesh == nullptr) {
            continue;
        }

        const erhe::primitive::Buffer_range& edge_range = buffer_mesh->edge_line_vertex_buffer_range;
        if (edge_range.count == 0) {
            continue;
        }

        const std::size_t edge_count = edge_range.count / 2;
        m_dispatches.push_back(Dispatch_entry{
            .edge_buffer_byte_offset = edge_range.byte_offset,
            .edge_count              = edge_count,
            .world_from_node         = world_from_node,
            .color                   = color,
            .line_width              = line_width,
            .group                   = group
        });
    }
}

void Content_wide_line_renderer::compute(
    erhe::graphics::Compute_command_encoder&  command_encoder,
    const erhe::math::Viewport&               viewport,
    const erhe::scene::Camera&                camera,
    const bool                                reverse_depth,
    const erhe::math::Depth_range             depth_range,
    const erhe::math::Coordinate_conventions& conventions
)
{
    if (!m_enabled || m_dispatches.empty()) {
        return;
    }

    ERHE_VERIFY(m_compute_pipeline.has_value());
    command_encoder.set_bind_group_layout(m_bind_group_layout.get());
    command_encoder.set_compute_pipeline_state(m_compute_pipeline.value());

    const erhe::scene::Node* camera_node = camera.get_node();
    ERHE_VERIFY(camera_node != nullptr);

    const erhe::scene::Camera_projection_transforms projection_transforms =
        camera.projection_transforms(viewport, reverse_depth, depth_range, conventions);
    const glm::mat4 clip_from_world = projection_transforms.clip_from_world.get_matrix();

    const glm::mat4 camera_world_from_node = camera_node->world_from_node();
    const glm::vec4 view_position_in_world{camera_world_from_node[3]};
    const float     clip_depth_direction = reverse_depth ? -1.0f : 1.0f;

    const erhe::scene::Projection::Fov_sides fov_sides = camera.projection()->get_fov_sides(viewport);
    const glm::vec4 viewport_vec4{
        static_cast<float>(viewport.x),
        static_cast<float>(viewport.y),
        static_cast<float>(viewport.width),
        static_cast<float>(viewport.height)
    };
    const glm::vec4 fov_sides_vec4{fov_sides.left, fov_sides.right, fov_sides.up, fov_sides.down};

    const std::size_t triangle_vertex_stride = m_triangle_vertex_format.streams.front().stride;

    for (Dispatch_entry& dispatch : m_dispatches) {
        const std::size_t view_size = m_view_block->get_size_bytes();
        erhe::graphics::Ring_buffer_range view_buffer_range = m_view_buffer.acquire(
            erhe::graphics::Ring_buffer_usage::CPU_write,
            view_size
        );
        const std::span<std::byte> view_data = view_buffer_range.get_span();

        using erhe::graphics::write;
        using erhe::graphics::as_span;
        write(view_data, m_clip_from_world_offset, as_span(clip_from_world));
        write(view_data, m_world_from_node_offset, as_span(dispatch.world_from_node));
        write(view_data, m_viewport_offset,        as_span(viewport_vec4));
        write(view_data, m_fov_offset,             as_span(fov_sides_vec4));
        const glm::vec4 line_color_with_width{dispatch.color.x, dispatch.color.y, dispatch.color.z, dispatch.line_width};
        write(view_data, m_line_color_offset,      as_span(line_color_with_width));
        const uint32_t edge_count_uint = static_cast<uint32_t>(dispatch.edge_count);
        write(view_data, m_edge_count_offset,      as_span(edge_count_uint));
        const bool top_left = (m_graphics_device.get_info().coordinate_conventions.framebuffer_origin == erhe::math::Framebuffer_origin::top_left);
        const float vp_y_sign = top_left ? -1.0f : 1.0f;
        const float zero      = 0.0f;
        write(view_data, m_vp_y_sign_offset,              as_span(vp_y_sign));
        write(view_data, m_clip_depth_direction_offset,   as_span(clip_depth_direction));
        write(view_data, m_padding0_offset,               as_span(zero));
        write(view_data, m_padding1_offset,               as_span(zero));
        write(view_data, m_view_position_in_world_offset, as_span(view_position_in_world));

        view_buffer_range.bytes_written(view_size);
        view_buffer_range.close();
        m_view_buffer.bind(command_encoder, view_buffer_range);

        command_encoder.set_buffer(
            erhe::graphics::Buffer_target::storage,
            &m_edge_line_vertex_buffer,
            dispatch.edge_buffer_byte_offset,
            dispatch.edge_count * 2 * sizeof(float) * 8,
            m_edge_line_vertex_buffer_block->get_binding_point()
        );

        // ceil(edge_count / 32) workgroups, local_size_x = 32 threads each
        const uint32_t    workgroup_count     = static_cast<uint32_t>((dispatch.edge_count + 31) / 32);
        const std::size_t padded_edge_count   = static_cast<std::size_t>(workgroup_count) * 32;
        const std::size_t triangle_byte_count = 6 * padded_edge_count * triangle_vertex_stride;
        dispatch.triangle_buffer_range = m_triangle_vertex_buffer_client.acquire(
            erhe::graphics::Ring_buffer_usage::GPU_access,
            triangle_byte_count
        );
        dispatch.triangle_buffer_range.bytes_gpu_used(triangle_byte_count);
        dispatch.triangle_buffer_range.close();
        m_triangle_vertex_buffer_client.bind(command_encoder, dispatch.triangle_buffer_range);

        command_encoder.dispatch_compute(workgroup_count, 1, 1);

        dispatch.dispatched = true;
        view_buffer_range.release();
    }
}

void Content_wide_line_renderer::render(
    erhe::graphics::Render_command_encoder&      render_encoder,
    const erhe::graphics::Render_pipeline_state& pipeline_state,
    const uint32_t                               group
)
{
    if (!m_enabled || m_dispatches.empty()) {
        return;
    }

    m_graphics_device.memory_barrier(erhe::graphics::Memory_barrier_mask::vertex_attrib_array_barrier_bit);

    erhe::graphics::Scoped_debug_group debug_scope{"Content_wide_line_renderer::render"};

    render_encoder.set_bind_group_layout(m_bind_group_layout.get());
    render_encoder.set_render_pipeline_state(pipeline_state);

    for (const Dispatch_entry& dispatch : m_dispatches) {
        if (!dispatch.dispatched || (dispatch.group != group)) {
            continue;
        }

        erhe::graphics::Buffer* triangle_buffer        = dispatch.triangle_buffer_range.get_buffer()->get_buffer();
        std::size_t             triangle_buffer_offset = dispatch.triangle_buffer_range.get_byte_start_offset_in_buffer();
        render_encoder.set_vertex_buffer(triangle_buffer, triangle_buffer_offset, 0);
        render_encoder.draw_primitives(
            erhe::graphics::Primitive_type::triangle,
            0,
            static_cast<uint32_t>(6 * dispatch.edge_count)
        );
    }
}

void Content_wide_line_renderer::end_frame()
{
    if (!m_enabled) {
        return;
    }

    for (Dispatch_entry& dispatch : m_dispatches) {
        if (dispatch.dispatched) {
            dispatch.triangle_buffer_range.release();
        }
    }
    m_dispatches.clear();

    m_graphics_device.memory_barrier(erhe::graphics::Memory_barrier_mask::shader_storage_barrier_bit);
}

} // namespace erhe::scene_renderer
