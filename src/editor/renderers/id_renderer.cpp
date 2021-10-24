#include "renderers/id_renderer.hpp"
#include "configuration.hpp"
#include "graphics/gl_context_provider.hpp"
#include "renderers/program_interface.hpp"
#include "renderers/mesh_memory.hpp"
#include "log.hpp"

#include "erhe/components/component.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/renderbuffer.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/tracy_client.hpp"
#include "erhe/toolkit/verify.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>

namespace editor
{

using namespace erhe::toolkit;
using namespace erhe::graphics;
using namespace erhe::scene;
using namespace erhe::primitive;
using namespace gl;
using namespace glm;
using namespace std;

Id_renderer::Id_renderer()
    : erhe::components::Component{c_name}
{
}

Id_renderer::~Id_renderer() = default;

void Id_renderer::connect()
{
    base_connect(this);

    require<Configuration>();
    require<Gl_context_provider>();
    require<Program_interface>();

    m_pipeline_state_tracker = erhe::components::Component::get<OpenGL_state_tracker>();
    m_mesh_memory            = require<Mesh_memory>();
    m_programs               = require<Programs>();
}

static constexpr std::string_view c_id_renderer_initialize_component{"Id_renderer::initialize_component()"};

void Id_renderer::initialize_component()
{
    ZoneScoped;

    TracyMessageL("ID: Waiting for GL context");
    Scoped_gl_context gl_context{Component::get<Gl_context_provider>().get()};
    TracyMessageL("ID: Got GL context");

    create_frame_resources(1, 1, 1, 1000, 1000);

    const auto  reverse_depth    = erhe::components::Component::get<Configuration>()->reverse_depth;
    const auto& shader_resources = *Component::get<Program_interface>()->shader_resources.get();

    m_vertex_input = std::make_unique<erhe::graphics::Vertex_input_state>(
        shader_resources.attribute_mappings,
        m_mesh_memory->gl_vertex_format(),
        m_mesh_memory->gl_vertex_buffer.get(),
        m_mesh_memory->gl_index_buffer.get()
    );

    m_pipeline.shader_stages  = m_programs->id.get();
    m_pipeline.vertex_input   = m_vertex_input.get();
    m_pipeline.input_assembly = &erhe::graphics::Input_assembly_state::triangles;
    m_pipeline.rasterization  = &erhe::graphics::Rasterization_state::cull_mode_back_ccw;
    m_pipeline.depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth);
    m_pipeline.color_blend    = &erhe::graphics::Color_blend_state::color_blend_disabled;
    m_pipeline.viewport       = nullptr;

    m_selective_depth_clear_pipeline.shader_stages  = m_programs->id.get();
    m_selective_depth_clear_pipeline.vertex_input   = m_vertex_input.get();
    m_selective_depth_clear_pipeline.input_assembly = &erhe::graphics::Input_assembly_state::triangles;
    m_selective_depth_clear_pipeline.rasterization  = &erhe::graphics::Rasterization_state::cull_mode_back_ccw;
    m_selective_depth_clear_pipeline.depth_stencil  = &erhe::graphics::Depth_stencil_state::depth_test_always_stencil_test_disabled;
    m_selective_depth_clear_pipeline.color_blend    = &erhe::graphics::Color_blend_state::color_writes_disabled;
    m_selective_depth_clear_pipeline.viewport       = nullptr;

    gl::push_debug_group(
        gl::Debug_source::debug_source_application,
        0,
        static_cast<GLsizei>(c_id_renderer_initialize_component.length()),
       c_id_renderer_initialize_component.data()
    );

    create_id_frame_resources();

    gl::pop_debug_group();
}

void Id_renderer::create_id_frame_resources()
{
    ZoneScoped;

    for (size_t i = 0; i < s_frame_resources_count; ++i)
    {
        m_id_frame_resources.emplace_back();
    }
}


auto Id_renderer::current_id_frame_resources() -> Id_frame_resources&
{
    return m_id_frame_resources[m_current_id_frame_resource_slot];
}

void Id_renderer::next_frame()
{
    Base_renderer::next_frame();
    m_current_id_frame_resource_slot = (m_current_id_frame_resource_slot + 1) % s_frame_resources_count;
}

void Id_renderer::update_framebuffer(const erhe::scene::Viewport viewport)
{
    VERIFY(m_use_renderbuffers != m_use_textures);

    if (m_use_renderbuffers)
    {
        if (!m_color_renderbuffer ||
            (m_color_renderbuffer->width()  != static_cast<unsigned int>(viewport.width)) ||
            (m_color_renderbuffer->height() != static_cast<unsigned int>(viewport.height)))
        {
            m_color_renderbuffer = std::make_unique<Renderbuffer>(gl::Internal_format::rgba8, viewport.width, viewport.height);
            m_depth_renderbuffer = std::make_unique<Renderbuffer>(gl::Internal_format::depth_component32f, viewport.width, viewport.height);
            m_color_renderbuffer->set_debug_label("ID Renderer Color Renderbuffer");
            m_depth_renderbuffer->set_debug_label("ID Renderer Depth Renderbuffer");
            Framebuffer::Create_info create_info;
            create_info.attach(gl::Framebuffer_attachment::color_attachment0, m_color_renderbuffer.get());
            create_info.attach(gl::Framebuffer_attachment::depth_attachment,  m_depth_renderbuffer.get());
            m_framebuffer = std::make_unique<Framebuffer>(create_info);
            m_framebuffer->set_debug_label("ID Renderer");
        }
    }

    if (m_use_textures)
    {
        if (!m_color_texture ||
            (m_color_texture->width()  != viewport.width) ||
            (m_color_texture->height() != viewport.height))
        {
            m_color_texture = std::make_unique<Texture>(Texture::Create_info(gl::Texture_target::texture_2d, gl::Internal_format::rgba8,              true, viewport.width, viewport.height));
            m_depth_texture = std::make_unique<Texture>(Texture::Create_info(gl::Texture_target::texture_2d, gl::Internal_format::depth_component32f, true, viewport.width, viewport.height));
            m_color_texture->set_debug_label("ID Renderer Color Texture");
            m_depth_texture->set_debug_label("ID Renderer Depth Texture");
            Framebuffer::Create_info create_info;
            create_info.attach(gl::Framebuffer_attachment::color_attachment0, m_color_texture.get());
            create_info.attach(gl::Framebuffer_attachment::depth_attachment,  m_depth_texture.get());
            m_framebuffer = std::make_unique<Framebuffer>(create_info);
            m_framebuffer->set_debug_label("ID Renderer");
            constexpr float clear_value[4] = {1.0f, 0.0f, 0.0f, 1.0f };
            gl::clear_tex_image(m_color_texture->gl_name(), 0, gl::Pixel_format::rgba, gl::Pixel_type::float_, &clear_value[0]);
        }
    }
}

void Id_renderer::render_layer(erhe::scene::Layer* layer)
{
    ZoneScoped;

    Layer_range layer_range;
    layer_range.offset = id_offset();
    layer_range.layer  = layer;

    erhe::scene::Visibility_filter id_filter{
        erhe::scene::Mesh::c_visibility_id,
        0u,
        0u,
        0u
    };
    update_primitive_buffer(layer->meshes, id_filter);
    auto draw_indirect_buffer_range = update_draw_indirect_buffer(
        layer->meshes,
        Primitive_mode::polygon_fill,
        id_filter
    );

    bind_primitive_buffer();
    bind_draw_indirect_buffer();

    gl::multi_draw_elements_indirect(
        m_pipeline.input_assembly->primitive_topology,
        m_mesh_memory->gl_index_type(),
        reinterpret_cast<const void *>(draw_indirect_buffer_range.range.first_byte_offset),
        static_cast<GLsizei>(draw_indirect_buffer_range.draw_indirect_count),
        static_cast<GLsizei>(sizeof(gl::Draw_elements_indirect_command))
    );
    layer_range.end = id_offset();
    m_layer_ranges.emplace_back(layer_range);
}

static constexpr std::string_view c_id_renderer_render_clear  {"Id_renderer::render() clear"  };
static constexpr std::string_view c_id_renderer_render_content{"Id_renderer::render() content"};
static constexpr std::string_view c_id_renderer_render_tool   {"Id_renderer::render() tool"   };
static constexpr std::string_view c_id_renderer_render_read   {"Id_renderer::render() read"   };

void Id_renderer::render(
    const erhe::scene::Viewport viewport,
    const Layer_collection&     content_layers,
    const Layer_collection&     tool_layers,
    erhe::scene::ICamera&       camera,
    const double                time,
    const int                   x,
    const int                   y)
{
    ZoneScoped;

    m_ranges.clear();

    if ((viewport.width == 0) || (viewport.height == 0))
    {
        return;
    }

    gl::push_debug_group(
        gl::Debug_source::debug_source_application,
        0,
        static_cast<GLsizei>(c_id_renderer_render_content.length()),
        c_id_renderer_render_content.data()
    );

    update_framebuffer(viewport);

    auto& idr = current_id_frame_resources();
    idr.time            = time;
    idr.x_offset        = std::max(x - (static_cast<int>(s_extent / 2)), 0);
    idr.y_offset        = std::max(y - (static_cast<int>(s_extent / 2)), 0);
    idr.clip_from_world = camera.clip_from_world();

    primitive_color_source = Primitive_color_source::id_offset;

    update_camera_buffer(camera, viewport);
    bind_camera_buffer();

    {
        TracyGpuZone(c_id_renderer_render_clear.data())

        m_pipeline_state_tracker->shader_stages.reset();
        m_pipeline_state_tracker->color_blend.execute(&erhe::graphics::Color_blend_state::color_blend_disabled);
        {
            gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer->gl_name());
            const auto status = gl::check_named_framebuffer_status(m_framebuffer->gl_name(), gl::Framebuffer_target::draw_framebuffer);
            if (status != gl::Framebuffer_status::framebuffer_complete)
            {
                log_framebuffer.error("draw framebuffer status = {}\n", c_str(status));
            }
            VERIFY(status == gl::Framebuffer_status::framebuffer_complete);
        }

        {
            gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, m_framebuffer->gl_name());
            const auto status = gl::check_named_framebuffer_status(m_framebuffer->gl_name(), gl::Framebuffer_target::draw_framebuffer);
            if (status != gl::Framebuffer_status::framebuffer_complete)
            {
                log_framebuffer.error("read framebuffer status = {}\n", c_str(status));
            }
            VERIFY(status == gl::Framebuffer_status::framebuffer_complete);
        }
        gl::disable    (gl::Enable_cap::framebuffer_srgb);
        gl::viewport   (viewport.x, viewport.y, viewport.width, viewport.height);
        gl::clear_color(1.0f, 1.0f, 1.0f, 0.1f);
        gl::clear      (gl::Clear_buffer_mask::color_buffer_bit | gl::Clear_buffer_mask::depth_buffer_bit);
        if (m_use_scissor)
        {
            gl::scissor(idr.x_offset, idr.y_offset, s_extent, s_extent);
            gl::enable (gl::Enable_cap::scissor_test);
        }
    }

    reset_id_ranges();
    m_layer_ranges.clear();

    m_pipeline_state_tracker->execute(&m_pipeline);
    for (auto layer : content_layers)
    {
        TracyGpuZone(c_id_renderer_render_content.data())
        render_layer(layer.get());
    }

    // Clear depth for tool pixels
    if constexpr (true)
    {
        TracyGpuZone(c_id_renderer_render_tool.data())
        m_pipeline_state_tracker->execute(&m_selective_depth_clear_pipeline);
        gl::depth_range(0.0f, 0.0f);
        for (auto layer : tool_layers)
        {
            render_layer(layer.get());
        }
    }

    // Resume normal depth usage
    {
        TracyGpuZone(c_id_renderer_render_tool.data())

        m_pipeline_state_tracker->execute(&m_pipeline);
        gl::depth_range(0.0f, 1.0f);

        for (auto layer : tool_layers)
        {
            render_layer(layer.get());
        }
    }

    {
        TracyGpuZone(c_id_renderer_render_read.data())

        if (m_use_scissor)
        {
            gl::disable(gl::Enable_cap::scissor_test);
        }
        gl::bind_buffer(gl::Buffer_target::pixel_pack_buffer, idr.pixel_pack_buffer.gl_name());
        void* const color_offset = nullptr;
        void* const depth_offset = reinterpret_cast<void*>(s_extent * s_extent * 4);
        gl::read_pixels(idr.x_offset, idr.y_offset, s_extent, s_extent, gl::Pixel_format::rgba,            gl::Pixel_type::unsigned_byte, color_offset);
        gl::read_pixels(idr.x_offset, idr.y_offset, s_extent, s_extent, gl::Pixel_format::depth_component, gl::Pixel_type::float_,        depth_offset);
        gl::bind_buffer(gl::Buffer_target::pixel_pack_buffer, 0);
        idr.sync = gl::fence_sync(gl::Sync_condition::sync_gpu_commands_complete, 0);
        idr.state = Id_frame_resources::State::Waiting_for_read;
    }

    gl::pop_debug_group();
}

template<typename T>
inline T read_as(uint8_t const* raw_memory)
{
    static_assert(std::is_trivially_copyable<T>());
    T result;
    memcpy(&result, raw_memory, sizeof(T));
    return result;
}

bool Id_renderer::get(const int x, const int y, uint32_t& id, float& depth)
{
    int slot = static_cast<int>(m_current_id_frame_resource_slot);

    for (size_t i = 0; i < s_frame_resources_count; ++i)
    {
        --slot;
        if (slot < 0)
        {
            slot = s_frame_resources_count - 1;
        }

        auto& idr = m_id_frame_resources[slot];

        if (idr.state == Id_frame_resources::State::Waiting_for_read)
        {
            GLint sync_status = GL_UNSIGNALED;
            gl::get_sync_iv(idr.sync, gl::Sync_parameter_name::sync_status, 4, nullptr, &sync_status);

            if (sync_status == GL_SIGNALED)
            {
                gl::bind_buffer(gl::Buffer_target::pixel_pack_buffer, idr.pixel_pack_buffer.gl_name());

                auto gpu_data = idr.pixel_pack_buffer.map();

                memcpy(&idr.data[0], gpu_data.data(), gpu_data.size_bytes());
                idr.state = Id_frame_resources::State::Read_complete;
            }
        }

        if (idr.state == Id_frame_resources::State::Read_complete)
        {
            if ((x >= idr.x_offset) && (y >= idr.y_offset))
            {
                const size_t x_ = x - idr.x_offset;
                const size_t y_ = y - idr.y_offset;
                if ((x_ < s_extent) && (y_ < s_extent))
                {
                    const uint32_t       stride      = s_extent * 4;
                    const uint8_t        r           = idr.data[x_ * 4 + y_ * stride + 0];
                    const uint8_t        g           = idr.data[x_ * 4 + y_ * stride + 1];
                    const uint8_t        b           = idr.data[x_ * 4 + y_ * stride + 2];
                    const uint8_t* const depth_ptr   = &idr.data[s_extent * s_extent * 4 + x_ * 4 + y_ * stride];
                    id                               = (r << 16) | (g << 8) | b;
                    //const float* const   depth_f_ptr = reinterpret_cast<const float*>(depth_ptr);
                    //float*   depth_f_ptr = reinterpret_cast<float*>(depth_ptr);
                    //depth                = *depth_f_ptr;
                    depth                = read_as<float>(depth_ptr);
                    return true;
                }
            }
        }
    }
    //log_id_render.trace("Id_renderer::get(x = {}, y = {}): Warning: No data\n", x, y);
    return false;
}

Id_renderer::Mesh_primitive Id_renderer::get(const int x, const int y, float& depth)
{
    Mesh_primitive result;
    uint32_t id{0};
    const bool ok = get(x, y, id, depth);
    if (!ok)
    {
        return result;
    }
    result.valid = true;

    for (auto& r : id_ranges())
    {
        if ((id >= r.offset) && id < (r.offset + r.length))
        {
            result.mesh                 = r.mesh;
            result.mesh_primitive_index = r.primitive_index;
            result.local_index          = id - r.offset;
            for (auto& layer_range : m_layer_ranges)
            {
                if ((id >= layer_range.offset) && (id < layer_range.end))
                {
                    result.layer = layer_range.layer;
                }
            }
            return result;
        }
    }

    return result;
}

}
