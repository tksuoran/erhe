#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_renderer/debug_renderer_bucket.hpp"
#include "erhe_renderer/primitive_renderer.hpp"

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_renderer/renderer_log.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::renderer {

Debug_renderer_program_interface::Debug_renderer_program_interface(erhe::graphics::Device& graphics_device)
    : use_compute{graphics_device.get_info().use_compute_shader}
    , fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = erhe::graphics::Glsl_type::float_vec4,
            .location = 0
        }
    }
    , triangle_vertex_format{
        {
            0, // TODO
            {
                {erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::position},
                {erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::color},
                {erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::custom, 0}, // clipped line start (xy) and end (zw)
            }
        }
    }
    , line_vertex_format{
        {
            0,
            {
                {erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::position},
                {erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::color},
            }
        }
    }
{
    // Line vertex struct defines the per-vertex data layout: position (xyz + width) and color.
    // Used as SSBO struct in compute path, and matches the vertex buffer layout in simple line path.
    line_vertex_struct = std::make_unique<erhe::graphics::Shader_resource>(graphics_device, "line_vertex");
    line_vertex_struct->add_vec4("position_0");
    line_vertex_struct->add_vec4("color_0");

    view_block = std::make_unique<erhe::graphics::Shader_resource>(
        graphics_device,
        "view",
        3,
        erhe::graphics::Shader_resource::Type::uniform_block
    );

    clip_from_world_offset = view_block->add_mat4 ("clip_from_world")->get_offset_in_parent();
    viewport_offset        = view_block->add_vec4 ("viewport"       )->get_offset_in_parent();
    fov_offset             = view_block->add_vec4 ("fov"            )->get_offset_in_parent();
    vp_y_sign_offset       = view_block->add_float("vp_y_sign"      )->get_offset_in_parent();
    padding0_offset        = view_block->add_float("_padding0"      )->get_offset_in_parent();
    padding1_offset        = view_block->add_float("_padding1"      )->get_offset_in_parent();
    padding2_offset        = view_block->add_float("_padding2"      )->get_offset_in_parent();

    const auto shader_path = std::filesystem::path("res") / std::filesystem::path("shaders");

    // Bind group layout will be created after all blocks are known
    auto make_bind_group_layout = [&]() {
        erhe::graphics::Bind_group_layout_create_info layout_info{.debug_label = "Debug renderer"};
        if (line_vertex_buffer_block) {
            layout_info.bindings.push_back({line_vertex_buffer_block->get_binding_point(), erhe::graphics::Binding_type::storage_buffer});
        }
        if (triangle_vertex_buffer_block) {
            layout_info.bindings.push_back({triangle_vertex_buffer_block->get_binding_point(), erhe::graphics::Binding_type::storage_buffer});
        }
        layout_info.bindings.push_back({
            view_block->get_binding_point(),
            (view_block->get_type() == erhe::graphics::Shader_resource::Type::shader_storage_block)
                ? erhe::graphics::Binding_type::storage_buffer
                : erhe::graphics::Binding_type::uniform_buffer
        });
        return std::make_unique<erhe::graphics::Bind_group_layout>(graphics_device, layout_info);
    };

    if (use_compute) {
        // Compute path: SSBO line vertices → compute shader → SSBO triangle vertices → render triangles
        line_vertex_buffer_block = std::make_unique<erhe::graphics::Shader_resource>(
            graphics_device,
            erhe::graphics::Shader_resource::Block_create_info{
                .name          = "line_vertex_buffer",
                .binding_point = 0,
                .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
                .readonly      = true
            }
        );
        line_vertex_buffer_block->add_struct("vertices", line_vertex_struct.get(), erhe::graphics::Shader_resource::unsized_array);

        triangle_vertex_struct = std::make_unique<erhe::graphics::Shader_resource>(graphics_device, "triangle_vertex");
        triangle_vertex_buffer_block = std::make_unique<erhe::graphics::Shader_resource>(
            graphics_device,
            erhe::graphics::Shader_resource::Block_create_info{
                .name          = "triangle_vertex_buffer",
                .binding_point = 1,
                .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
                .writeonly     = true
            }
        );
        add_vertex_stream(
            triangle_vertex_format.streams.front(),
            *triangle_vertex_struct.get(),
            *triangle_vertex_buffer_block.get()
        );

        bind_group_layout = make_bind_group_layout();

        using namespace erhe::graphics;
        // Compute shader
        {
            const std::filesystem::path comp_path = shader_path / std::filesystem::path("compute_before_line.comp");
            Shader_stages_create_info create_info{
                .name             = "compute_before_line",
                .struct_types     = { line_vertex_struct.get(), triangle_vertex_struct.get() },
                .interface_blocks = { line_vertex_buffer_block.get(), triangle_vertex_buffer_block.get(), view_block.get() },
                .shaders          = { { Shader_type::compute_shader, comp_path }, },
                .bind_group_layout = bind_group_layout.get(),
            };

            Shader_stages_prototype prototype{graphics_device, create_info};
            if (prototype.is_valid()) {
                compute_shader_stages = std::make_unique<Shader_stages>(graphics_device, std::move(prototype));
                graphics_device.get_shader_monitor().add(create_info, compute_shader_stages.get());
            } else {
                log_startup->error("Unable to load compute_before_line shader - check working directory '{}'", std::filesystem::current_path().string());
            }
        }
        // Triangle rendering shader (reads triangle vertices produced by compute shader)
        {
            const std::filesystem::path vert_path = shader_path / std::filesystem::path("line_after_compute.vert");
            const std::filesystem::path frag_path = shader_path / std::filesystem::path("line_after_compute.frag");
            Shader_stages_create_info create_info{
                .name             = "line_after_compute",
                .interface_blocks = { view_block.get() },
                .fragment_outputs = &fragment_outputs,
                .vertex_format    = &triangle_vertex_format,
                .shaders = {
                    { Shader_type::vertex_shader,   vert_path },
                    { Shader_type::fragment_shader, frag_path }
                },
                .bind_group_layout = bind_group_layout.get(),
            };

            Shader_stages_prototype prototype{graphics_device, create_info};
            if (prototype.is_valid()) {
                graphics_shader_stages = std::make_unique<Shader_stages>(graphics_device, std::move(prototype));
                graphics_device.get_shader_monitor().add(create_info, graphics_shader_stages.get());
            } else {
                log_startup->error("Unable to load line_after_compute shader - check working directory '{}'", std::filesystem::current_path().string());
            }
        }
    }

    // Simple line shader (used when compute shaders unavailable, or as fallback)
    {
        if (!bind_group_layout) {
            bind_group_layout = make_bind_group_layout();
        }
        using namespace erhe::graphics;

        const std::filesystem::path vert_path = shader_path / std::filesystem::path("line_simple.vert");
        const std::filesystem::path frag_path = shader_path / std::filesystem::path("line_simple.frag");
        Shader_stages_create_info create_info{
            .name             = "line_simple",
            .interface_blocks = { view_block.get() },
            .fragment_outputs = &fragment_outputs,
            .vertex_format    = &line_vertex_format,
            .shaders = {
                { Shader_type::vertex_shader,   vert_path },
                { Shader_type::fragment_shader, frag_path }
            },
            .bind_group_layout = bind_group_layout.get(),
        };

        Shader_stages_prototype prototype{graphics_device, create_info};
        if (prototype.is_valid()) {
            line_shader_stages = std::make_unique<Shader_stages>(graphics_device, std::move(prototype));
            graphics_device.get_shader_monitor().add(create_info, line_shader_stages.get());
        } else {
            log_startup->error("Unable to load line_simple shader - check working directory '{}'", std::filesystem::current_path().string());
        }
    }

    // Geometry shader path: wide lines without compute (GL_LINES -> geometry shader -> triangle strip)
    if (!use_compute) {
        using namespace erhe::graphics;

        const std::filesystem::path vert_path = shader_path / std::filesystem::path("debug_line.vert");
        const std::filesystem::path geom_path = shader_path / std::filesystem::path("debug_line.geom");
        const std::filesystem::path frag_path = shader_path / std::filesystem::path("line_after_compute.frag");
        Shader_stages_create_info create_info{
            .name             = "debug_line_geometry",
            .interface_blocks = { view_block.get() },
            .fragment_outputs = &fragment_outputs,
            .vertex_format    = &line_vertex_format,
            .shaders = {
                { Shader_type::vertex_shader,   vert_path },
                { Shader_type::geometry_shader, geom_path },
                { Shader_type::fragment_shader, frag_path }
            },
            .bind_group_layout = bind_group_layout.get(),
        };

        Shader_stages_prototype prototype{graphics_device, create_info};
        if (prototype.is_valid()) {
            geometry_shader_stages = std::make_unique<Shader_stages>(graphics_device, std::move(prototype));
            graphics_device.get_shader_monitor().add(create_info, geometry_shader_stages.get());
            use_geometry_shader = true;
            log_startup->info("Geometry shader wide lines enabled");
        } else {
            log_startup->warn("Unable to load debug_line geometry shader, falling back to simple lines");
            use_geometry_shader = false;
        }
    }
}

Debug_renderer::Debug_renderer(erhe::graphics::Device& graphics_device)
    : m_graphics_device  {graphics_device}
    , m_program_interface{graphics_device}
    , m_vertex_input{
        graphics_device,
        erhe::graphics::Vertex_input_state_data::make(m_program_interface.triangle_vertex_format)
    }
    , m_line_vertex_input{
        graphics_device,
        erhe::graphics::Vertex_input_state_data::make(m_program_interface.line_vertex_format)
    }
    , m_lines_to_triangles_compute_pipeline{
        m_program_interface.use_compute
            ? std::optional<erhe::graphics::Compute_pipeline_state>{
                erhe::graphics::Compute_pipeline_data{
                    .name          = "compute_before_line",
                    .shader_stages = m_program_interface.compute_shader_stages.get()
                }
              }
            : std::nullopt
    }
{
}

Debug_renderer::~Debug_renderer() noexcept
{
}

auto Debug_renderer::get(const Debug_renderer_config& config) -> Primitive_renderer
{
    for (Debug_renderer_bucket& bucket : m_buckets) {
        if (bucket.match(config)) {
            bucket.start_view(get_view());
            return Primitive_renderer{*this, bucket};
        }
    }
    Debug_renderer_bucket& bucket = m_buckets.emplace_back(m_graphics_device, *this, config);
    bucket.start_view(get_view());
    return Primitive_renderer{*this, bucket};
}

static constexpr std::string_view c_line_renderer_render{"Debug_renderer::render()"};

void Debug_renderer::begin_frame(
    const erhe::math::Viewport                viewport,
    const erhe::scene::Camera&                camera,
    const erhe::math::Coordinate_conventions& conventions
)
{
    const auto* camera_node = camera.get_node();
    ERHE_VERIFY(camera_node != nullptr);

    const erhe::scene::Camera_projection_transforms projection_transforms = camera.projection_transforms(viewport, true, erhe::math::Depth_range::zero_to_one, conventions);
    const glm::mat4                                 clip_from_world       = projection_transforms.clip_from_world.get_matrix();
    const erhe::scene::Projection::Fov_sides        fov_sides             = camera.projection()->get_fov_sides(viewport);

    for (size_t i = 0, end = m_view_stack.size(); i < end; ++i) {
        m_view_stack.pop();
    }

    push_view(
        {
            .clip_from_world = clip_from_world,
            .viewport        = glm::vec4{
                static_cast<float>(viewport.x),
                static_cast<float>(viewport.y),
                static_cast<float>(viewport.width),
                static_cast<float>(viewport.height)
            },
            .fov_sides = glm::vec4{
                fov_sides.left,
                fov_sides.right,
                fov_sides.up,
                fov_sides.down
            }
        }
    );
}

void Debug_renderer::compute(erhe::graphics::Compute_command_encoder& command_encoder)
{
    if (!m_program_interface.use_compute) {
        return;
    }

    ERHE_VERIFY(m_lines_to_triangles_compute_pipeline.has_value());
    command_encoder.set_compute_pipeline_state(m_lines_to_triangles_compute_pipeline.value());

    for (Debug_renderer_bucket& bucket : m_buckets) {
        bucket.dispatch_compute(command_encoder);
    }
}

void Debug_renderer::render(erhe::graphics::Render_command_encoder& encoder, const erhe::math::Viewport viewport)
{
    ERHE_PROFILE_FUNCTION();

    erhe::graphics::Scoped_debug_group scoped_debug_group{"Debug_renderer::render()"};

    encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);

    // Draw hidden
    for (Debug_renderer_bucket& bucket : m_buckets) {
        bucket.render(encoder, true, false);
    }

    if (m_program_interface.use_compute) {
        // Subsequent draw triangles reading vertex data must wait for compute shader
        m_graphics_device.memory_barrier(erhe::graphics::Memory_barrier_mask::vertex_attrib_array_barrier_bit);
    }

    // Draw visible
    for (Debug_renderer_bucket& bucket : m_buckets) {
        bucket.render(encoder, false, true);
    }

    if (m_program_interface.use_compute) {
        // Subsequent compute dispatch writing to triangle vertex buffer must wait for draw triangles reading that data
        // TODO Use gl::wait_sync()?
        m_graphics_device.memory_barrier(erhe::graphics::Memory_barrier_mask::shader_storage_barrier_bit);
    }
}

void Debug_renderer::end_frame()
{
    for (Debug_renderer_bucket& bucket : m_buckets) {
        bucket.release_buffers();
    }
}

} // namespace erhe::renderer
