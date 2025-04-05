#include "renderers/id_renderer.hpp"

#include "editor_log.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_gl/command_info.hpp"
#include "erhe_gl/draw_indirect.hpp"
#include "erhe_gl/enum_bit_mask_operators.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/debug.hpp"
#include "erhe_graphics/framebuffer.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/renderbuffer.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

using erhe::graphics::Framebuffer;
using erhe::graphics::Renderbuffer;
using erhe::graphics::Texture;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;

using glm::mat4;

namespace {

constexpr gl::Buffer_storage_mask storage_mask{
    gl::Buffer_storage_mask::map_coherent_bit   |
    gl::Buffer_storage_mask::map_persistent_bit |
    gl::Buffer_storage_mask::map_read_bit
};

constexpr gl::Map_buffer_access_mask access_mask{
    gl::Map_buffer_access_mask::map_coherent_bit   |
    gl::Map_buffer_access_mask::map_persistent_bit |
    gl::Map_buffer_access_mask::map_read_bit
};

}

Id_renderer::Id_frame_resources::Id_frame_resources(erhe::graphics::Instance& graphics_instance, const std::size_t /*slot*/)
    : pixel_pack_buffer{
        graphics_instance,
        erhe::graphics::Buffer_create_info{
            .target              = gl::Buffer_target::pixel_pack_buffer,
            .capacity_byte_count = s_id_buffer_size,
            .storage_mask        = storage_mask,
            .access_mask         = access_mask,
            .debug_label         = "ID"
        }
    }
{
}

Id_renderer::Id_frame_resources::Id_frame_resources(Id_frame_resources&& other) noexcept = default;

auto Id_renderer::Id_frame_resources::operator=(Id_frame_resources&& other) noexcept -> Id_frame_resources& = default;

static constexpr std::string_view c_id_renderer_initialize_component{"Id_renderer::initialize_component()"};

[[nodiscard]] auto get_max_draw_count() -> std::size_t
{
    int max_draw_count = 1000;
    const auto& ini = erhe::configuration::get_ini_file_section("erhe.ini", "renderer");
    ini.get("max_draw_count", max_draw_count);
    return max_draw_count;
}

using Vertex_input_state   = erhe::graphics::Vertex_input_state;
using Input_assembly_state = erhe::graphics::Input_assembly_state;
using Rasterization_state  = erhe::graphics::Rasterization_state;
using Depth_stencil_state  = erhe::graphics::Depth_stencil_state;
using Color_blend_state    = erhe::graphics::Color_blend_state;

Id_renderer::Id_renderer(
    erhe::graphics::Instance&                graphics_instance,
    erhe::scene_renderer::Program_interface& program_interface,
    Mesh_memory&                             mesh_memory,
    Programs&                                programs
)
    : m_graphics_instance    {graphics_instance}
    , m_mesh_memory          {mesh_memory}
    , m_camera_buffers       {graphics_instance, program_interface.camera_interface}
    , m_draw_indirect_buffers{graphics_instance}
    , m_primitive_buffers    {graphics_instance, program_interface.primitive_interface}
    //{
    //const auto reverse_depth = graphics_instance.configuration.reverse_depth;

#define REVERSE_DEPTH graphics_instance.configuration.reverse_depth

    , m_pipeline{erhe::graphics::Pipeline_data{
        .name           = "ID Renderer",
        .shader_stages  = &programs.id.shader_stages,
        .vertex_input   = &mesh_memory.vertex_input,
        .input_assembly = Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_back_ccw,
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(REVERSE_DEPTH),
        .color_blend    = Color_blend_state::color_blend_disabled
    }}

    , m_selective_depth_clear_pipeline{erhe::graphics::Pipeline_data{
        .name           = "ID Renderer selective depth clear",
        .shader_stages  = &programs.id.shader_stages,
        .vertex_input   = &mesh_memory.vertex_input,
        .input_assembly = Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_back_ccw,
        .depth_stencil  = Depth_stencil_state::depth_test_always_stencil_test_disabled,
        .color_blend    = Color_blend_state::color_writes_disabled,
    }}
    , m_gpu_timer{"Id_renderer"}

#undef REVERSE_DEPTH
{
    create_id_frame_resources();

    const auto& ini = erhe::configuration::get_ini_file_section("erhe.ini", "id_renderer");
    ini.get("enabled", enabled);
}

void Id_renderer::create_id_frame_resources()
{
    ERHE_PROFILE_FUNCTION();

    for (size_t slot = 0; slot < s_frame_resources_count; ++slot) {
        m_id_frame_resources.emplace_back(m_graphics_instance, slot);
    }
}

auto Id_renderer::current_id_frame_resources() -> Id_frame_resources&
{
    return m_id_frame_resources[m_current_id_frame_resource_slot];
}

void Id_renderer::next_frame()
{
    if (!m_enabled) {
        return;
    }

    m_current_id_frame_resource_slot = (m_current_id_frame_resource_slot + 1) % s_frame_resources_count;
}

void Id_renderer::update_framebuffer(const erhe::math::Viewport viewport)
{
    ERHE_PROFILE_FUNCTION();

    if (!m_enabled) {
        return;
    }

    ERHE_VERIFY(m_use_renderbuffers != m_use_textures);

    if (m_use_renderbuffers) {
        if (
            !m_color_renderbuffer ||
            (m_color_renderbuffer->width()  != static_cast<unsigned int>(viewport.width)) ||
            (m_color_renderbuffer->height() != static_cast<unsigned int>(viewport.height))
        ) {
            m_color_renderbuffer = std::make_unique<Renderbuffer>(
                m_graphics_instance,
                gl::Internal_format::rgba8,
                viewport.width,
                viewport.height
            );
            m_depth_renderbuffer = std::make_unique<Renderbuffer>(
                m_graphics_instance,
                gl::Internal_format::depth_component32f,
                viewport.width,
                viewport.height
            );
            m_color_renderbuffer->set_debug_label("ID Color");
            m_depth_renderbuffer->set_debug_label("ID Depth");
            Framebuffer::Create_info create_info;
            create_info.attach(gl::Framebuffer_attachment::color_attachment0, m_color_renderbuffer.get());
            create_info.attach(gl::Framebuffer_attachment::depth_attachment,  m_depth_renderbuffer.get());
            m_framebuffer = std::make_unique<Framebuffer>(create_info);
            m_framebuffer->set_debug_label("ID");
        }
    }

    if (m_use_textures) {
        if (
            !m_color_texture ||
            (m_color_texture->width()  != viewport.width) ||
            (m_color_texture->height() != viewport.height)
        ) {
            m_color_texture = std::make_unique<Texture>(
                Texture::Create_info{
                    .instance        = m_graphics_instance,
                    .target          = gl::Texture_target::texture_2d,
                    .internal_format = gl::Internal_format::rgba8,
                    .use_mipmaps     = false,
                    .width           = viewport.width,
                    .height          = viewport.height,
                    .debug_label     = "ID Render color"
                }
            );
            m_depth_texture = std::make_unique<Texture>(
                Texture::Create_info{
                    .instance        = m_graphics_instance,
                    .target          = gl::Texture_target::texture_2d,
                    .internal_format = gl::Internal_format::depth_component32f,
                    .use_mipmaps     = false,
                    .width           = viewport.width,
                    .height          = viewport.height,
                    .debug_label     = "ID Render depth"
                }
            );
            m_color_texture->set_debug_label("ID Color");
            m_depth_texture->set_debug_label("ID Depth");
            Framebuffer::Create_info create_info;
            create_info.attach(gl::Framebuffer_attachment::color_attachment0, m_color_texture.get());
            create_info.attach(gl::Framebuffer_attachment::depth_attachment,  m_depth_texture.get());
            m_framebuffer = std::make_unique<Framebuffer>(create_info);
            m_framebuffer->set_debug_label("ID");
            constexpr float clear_value[4] = {1.0f, 0.0f, 0.0f, 1.0f };
            if (gl::is_command_supported(gl::Command::Command_glClearTexImage)) {
                gl::clear_tex_image(
                    m_color_texture->gl_name(),
                    0,
                    gl::Pixel_format::rgba,
                    gl::Pixel_type::float_,
                    &clear_value[0]
                );
            } else {
                // TODO
            }
        }
    }
}

void Id_renderer::render(const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes)
{
    ERHE_PROFILE_FUNCTION();

    const erhe::Item_filter id_filter{
        .require_all_bits_set           = erhe::Item_flags::visible | erhe::Item_flags::id,
        .require_at_least_one_bit_set   = 0u,
        .require_all_bits_clear         = 0u,
        .require_at_least_one_bit_clear = 0u
    };

    const erhe::scene_renderer::Primitive_interface_settings settings{
        .color_source = erhe::scene_renderer::Primitive_color_source::id_offset
    };

    const erhe::primitive::Primitive_mode primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
    std::size_t primitive_count{0};
    erhe::renderer::Buffer_range               primitive_range            = m_primitive_buffers.update(meshes, primitive_mode, id_filter, settings, primitive_count, true);
    erhe::renderer::Draw_indirect_buffer_range draw_indirect_buffer_range = m_draw_indirect_buffers.update(meshes, primitive_mode, id_filter);
    if (draw_indirect_buffer_range.draw_indirect_count == 0) {
        return;
    }
    if (primitive_count != draw_indirect_buffer_range.draw_indirect_count) {
        log_render->warn("primitive_range != draw_indirect_buffer_range.draw_indirect_count");
    }

    primitive_range.bind();
    draw_indirect_buffer_range.range.bind();
    {
        static constexpr std::string_view c_draw{"draw"};

        ERHE_PROFILE_SCOPE("mdi");
        //ERHE_PROFILE_GPU_SCOPE(c_draw)
        gl::multi_draw_elements_indirect(
            m_pipeline.data.input_assembly.primitive_topology,
            erhe::graphics::to_gl_index_type(m_mesh_memory.buffer_info.index_type),
            reinterpret_cast<const void*>(draw_indirect_buffer_range.range.get_byte_start_offset_in_buffer()),
            static_cast<GLsizei>(draw_indirect_buffer_range.draw_indirect_count),
            static_cast<GLsizei>(sizeof(gl::Draw_elements_indirect_command))
        );
    }

    primitive_range.submit();
    draw_indirect_buffer_range.range.submit();
}

static constexpr std::string_view c_id_renderer_render_clear  {"Id_renderer::render() clear"  };
static constexpr std::string_view c_id_renderer_render_content{"Id_renderer::render() content"};
static constexpr std::string_view c_id_renderer_render_tool   {"Id_renderer::render() tool"   };
static constexpr std::string_view c_id_renderer_render_read   {"Id_renderer::render() read"   };

void Id_renderer::render(const Render_parameters& parameters)
{
    ERHE_PROFILE_FUNCTION();

    if (!m_enabled) {
        return;
    }

    const auto& viewport           = parameters.viewport;
    const auto& camera             = parameters.camera;
    const auto& content_mesh_spans = parameters.content_mesh_spans;
    const auto& tool_mesh_spans    = parameters.tool_mesh_spans;
    const auto  x                  = parameters.x;
    const auto  y                  = parameters.y;

    m_ranges.clear();

    if ((viewport.width == 0) || (viewport.height == 0)) {
        return;
    }

    erhe::graphics::Scoped_debug_group debug_group{c_id_renderer_render_content};
    erhe::graphics::Scoped_gpu_timer   timer      {m_gpu_timer};

    update_framebuffer(viewport);

    const auto projection_transforms = camera.projection_transforms(viewport);
    const mat4 clip_from_world       = projection_transforms.clip_from_world.get_matrix();

    auto& idr = current_id_frame_resources();
    idr.x_offset        = std::max(x - (static_cast<int>(s_extent / 2)), 0);
    idr.y_offset        = std::max(y - (static_cast<int>(s_extent / 2)), 0);
    idr.clip_from_world = clip_from_world;

    const auto camera_range = m_camera_buffers.update(
        *camera.projection(),
        *camera.get_node(),
        viewport,
        1.0f,
        glm::vec4{0.0f},
        glm::vec4{0.0f}
    );
    m_camera_buffers.bind(camera_range);

    {
        //ERHE_PROFILE_GPU_SCOPE(c_id_renderer_render_clear)

        m_graphics_instance.opengl_state_tracker.shader_stages.reset();
        m_graphics_instance.opengl_state_tracker.color_blend.execute(erhe::graphics::Color_blend_state::color_blend_disabled);
        {
            gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer->gl_name());

#if !defined(NDEBUG)
            const auto status = gl::check_named_framebuffer_status(m_framebuffer->gl_name(), gl::Framebuffer_target::draw_framebuffer);
            if (status != gl::Framebuffer_status::framebuffer_complete)
            {
                log_framebuffer->error("draw framebuffer status = {}", c_str(status));
            }
            ERHE_VERIFY(status == gl::Framebuffer_status::framebuffer_complete);
#endif
        }

        {
            gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, m_framebuffer->gl_name());
#if !defined(NDEBUG)
            const auto status = gl::check_named_framebuffer_status(m_framebuffer->gl_name(), gl::Framebuffer_target::draw_framebuffer);
            if (status != gl::Framebuffer_status::framebuffer_complete) {
                log_framebuffer->error("read framebuffer status = {}", c_str(status));
            }
            ERHE_VERIFY(status == gl::Framebuffer_status::framebuffer_complete);
#endif
        }
        gl::disable    (gl::Enable_cap::framebuffer_srgb);
        gl::viewport   (viewport.x, viewport.y, viewport.width, viewport.height);
        if (m_use_scissor) {
            gl::scissor(idr.x_offset, idr.y_offset, s_extent, s_extent);
            gl::enable (gl::Enable_cap::scissor_test);
        }
        gl::clear_color(1.0f, 1.0f, 1.0f, 0.1f);
        gl::clear      (gl::Clear_buffer_mask::color_buffer_bit | gl::Clear_buffer_mask::depth_buffer_bit);
    }

    m_primitive_buffers.reset_id_ranges();

    m_graphics_instance.opengl_state_tracker.execute(m_pipeline);
    m_graphics_instance.opengl_state_tracker.vertex_input.set_index_buffer(parameters.index_buffer);
    m_graphics_instance.opengl_state_tracker.vertex_input.set_vertex_buffer(0, parameters.vertex_buffer, parameters.vertex_buffer_offset);

    for (auto meshes : content_mesh_spans) {
        //ERHE_PROFILE_GPU_SCOPE(c_id_renderer_render_content)
        render(meshes);
    }

    // Clear depth for tool pixels
    {
        //ERHE_PROFILE_GPU_SCOPE(c_id_renderer_render_tool)
        m_graphics_instance.opengl_state_tracker.execute(m_selective_depth_clear_pipeline);
        gl::depth_range(0.0f, 0.0f);
        for (auto mesh_spans : tool_mesh_spans) {
            render(mesh_spans);
        }
    }

    // Resume normal depth usage
    {
        //ERHE_PROFILE_GPU_SCOPE(c_id_renderer_render_tool)

        m_graphics_instance.opengl_state_tracker.execute(m_pipeline);
        m_graphics_instance.opengl_state_tracker.vertex_input.set_index_buffer(parameters.index_buffer);
        m_graphics_instance.opengl_state_tracker.vertex_input.set_vertex_buffer(0, parameters.vertex_buffer, parameters.vertex_buffer_offset);

        gl::depth_range(0.0f, 1.0f);

        for (auto meshes : tool_mesh_spans) {
            render(meshes);
        }
    }

    {
        //ERHE_PROFILE_GPU_SCOPE(c_id_renderer_render_read)

        if (m_use_scissor) {
            gl::disable(gl::Enable_cap::scissor_test);
        }
        gl::bind_buffer(gl::Buffer_target::pixel_pack_buffer, idr.pixel_pack_buffer.gl_name());
        void* const color_offset = nullptr;
        void* const depth_offset = reinterpret_cast<void*>(s_extent * s_extent * 4);
        gl::read_pixels(
            idr.x_offset,
            idr.y_offset,
            s_extent,
            s_extent,
            gl::Pixel_format::rgba,
            gl::Pixel_type::unsigned_byte,
            color_offset
        );
        gl::read_pixels(
            idr.x_offset,
            idr.y_offset,
            s_extent,
            s_extent,
            gl::Pixel_format::depth_component,
            gl::Pixel_type::float_,
            depth_offset
        );
        gl::bind_buffer(gl::Buffer_target::pixel_pack_buffer, 0);
        idr.sync = gl::fence_sync(gl::Sync_condition::sync_gpu_commands_complete, 0);
        idr.state = Id_frame_resources::State::Waiting_for_read;
    }

    gl::enable(gl::Enable_cap::framebuffer_srgb);
}

template<typename T>
inline T read_as(uint8_t const* raw_memory)
{
    static_assert(std::is_trivially_copyable<T>());
    T result;
    memcpy(&result, raw_memory, sizeof(T));
    return result;
}

auto Id_renderer::get(const int x, const int y, uint32_t& id, float& depth) -> bool
{
    if (m_id_frame_resources.empty()) {
        return false;
    }
    int slot = static_cast<int>(m_current_id_frame_resource_slot);

    for (size_t i = 0; i < s_frame_resources_count; ++i) {
        --slot;
        if (slot < 0) {
            slot = s_frame_resources_count - 1;
        }

        auto& idr = m_id_frame_resources[slot];

        if (idr.state == Id_frame_resources::State::Waiting_for_read) {
            GLint sync_status = GL_UNSIGNALED;
            gl::get_sync_iv(idr.sync, gl::Sync_parameter_name::sync_status, 4, nullptr, &sync_status);

            if (sync_status == GL_SIGNALED) {
                gl::bind_buffer(gl::Buffer_target::pixel_pack_buffer, idr.pixel_pack_buffer.gl_name());

                auto gpu_data = idr.pixel_pack_buffer.map();

                memcpy(&idr.data[0], gpu_data.data(), gpu_data.size_bytes());
                idr.state = Id_frame_resources::State::Read_complete;
            }
        }

        if (idr.state == Id_frame_resources::State::Read_complete) {
            if ((x >= idr.x_offset) && (y >= idr.y_offset)) {
                const int x_ = x - idr.x_offset;
                const int y_ = y - idr.y_offset;
                if ((static_cast<size_t>(x_) < s_extent) && (static_cast<size_t>(y_) < s_extent)) {
                    const uint32_t       stride    = s_extent * 4;
                    const uint8_t        r         = idr.data[x_ * 4 + y_ * stride + 0];
                    const uint8_t        g         = idr.data[x_ * 4 + y_ * stride + 1];
                    const uint8_t        b         = idr.data[x_ * 4 + y_ * stride + 2];
                    const uint8_t* const depth_ptr = &idr.data[s_extent * s_extent * 4 + x_ * 4 + y_ * stride];
                    id                             = (r << 16) | (g << 8) | b;
                    depth                          = read_as<float>(depth_ptr);
                    return true;
                }
            }
        }
    }
    return false;
}

auto Id_renderer::get(const int x, const int y) -> Id_query_result
{
    Id_query_result result;
    const bool ok = get(x, y, result.id, result.depth);
    if (!ok) {
        return result;
    }
    result.valid = true;

    for (auto& r : m_primitive_buffers.id_ranges()) {
        if (
            (result.id >= r.offset) &&
            (result.id < (r.offset + r.length))
        ) {
            result.mesh            = r.mesh;
            result.primitive_index = r.primitive_index;
            result.triangle_id     = result.id - r.offset;
            return result;
        }
    }

    return result;
}

} // namespace editor
