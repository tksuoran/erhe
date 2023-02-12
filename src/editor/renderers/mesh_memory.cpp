#include "renderers/mesh_memory.hpp"

#include "renderers/program_interface.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/primitive/buffer_sink.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/raytrace/ibuffer.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/profile.hpp"

namespace editor {

IMesh_memory::~IMesh_memory() noexcept = default;

class Mesh_memory_impl
    : public IMesh_memory
{
public:
    Mesh_memory_impl()
    {
        ERHE_VERIFY(g_mesh_memory == nullptr);
        g_mesh_memory = this;

        int vertex_buffer_size{32}; // in megabytes
        int index_buffer_size  {8}; // in megabytes
        auto ini = erhe::application::get_ini("erhe.ini", "mesh_memory");
        ini->get("vertex_buffer_size", vertex_buffer_size);
        ini->get("index_buffer_size",  index_buffer_size);

        const erhe::application::Scoped_gl_context gl_context;

        static constexpr gl::Buffer_storage_mask storage_mask{gl::Buffer_storage_mask::map_write_bit};

        const std::size_t vertex_byte_count = static_cast<std::size_t>(vertex_buffer_size) * 1024 * 1024;
        const std::size_t index_byte_count  = static_cast<std::size_t>(index_buffer_size) * 1024 * 1024;

        gl_buffer_transfer_queue = std::make_unique<erhe::graphics::Buffer_transfer_queue>();

        {
            ERHE_PROFILE_SCOPE("GL VBO");

            gl_vertex_buffer = std::make_shared<erhe::graphics::Buffer>(
                gl::Buffer_target::array_buffer,
                vertex_byte_count,
                storage_mask
            );
        }

        {
            ERHE_PROFILE_SCOPE("GL IBO");

            gl_index_buffer = std::make_shared<erhe::graphics::Buffer>(
                gl::Buffer_target::element_array_buffer,
                index_byte_count,
                storage_mask
            );
        }
        gl_vertex_buffer->set_debug_label("Mesh Memory Vertex");
        gl_index_buffer ->set_debug_label("Mesh Memory Index");

        gl_buffer_sink = std::make_unique<erhe::primitive::Gl_buffer_sink>(
            *gl_buffer_transfer_queue.get(),
            *gl_vertex_buffer.get(),
            *gl_index_buffer.get()
        );

        build_info.buffer.buffer_sink = gl_buffer_sink.get();

        auto& format_info = build_info.format;
        auto& buffer_info = build_info.buffer;

        buffer_info.index_type = gl::Draw_elements_type::unsigned_int;

        format_info.features = {
            .fill_triangles   = true,
            .edge_lines       = true,
            .corner_points    = true,
            .centroid_points  = true,
            .position         = true,
            .normal           = true,
            .normal_smooth    = true, // TODO false?
            .tangent          = true,
            .bitangent        = true,
            .color            = true,
            .texcoord         = true,
            .id               = true
        };
        format_info.normal_style              = erhe::primitive::Normal_style::corner_normals;
        format_info.vertex_attribute_mappings = &g_program_interface->shader_resources->attribute_mappings;

        erhe::primitive::Primitive_builder::prepare_vertex_format(build_info);

        const auto& shader_resources = *g_program_interface->shader_resources.get();
        vertex_input = std::make_unique<erhe::graphics::Vertex_input_state>(
            erhe::graphics::Vertex_input_state_data::make(
                shader_resources.attribute_mappings,
                gl_vertex_format(),
                gl_vertex_buffer.get(),
                gl_index_buffer.get()
            )
        );
    }
    ~Mesh_memory_impl() noexcept override
    {
        ERHE_VERIFY(g_mesh_memory == this);
        g_mesh_memory = nullptr;
    }

    auto gl_vertex_format() const -> erhe::graphics::Vertex_format& override
    {
        return *build_info.buffer.vertex_format.get();
    }

    auto gl_index_type() const -> gl::Draw_elements_type override
    {
        return build_info.buffer.index_type;
    }

};

IMesh_memory* g_mesh_memory{nullptr};

Mesh_memory::Mesh_memory()
    : Component{c_type_name}
{
}

Mesh_memory::~Mesh_memory() noexcept
{
    ERHE_VERIFY(g_mesh_memory == nullptr);
}

void Mesh_memory::deinitialize_component()
{
    m_impl.reset();
}

void Mesh_memory::declare_required_components()
{
    require<erhe::application::Configuration>();
    require<erhe::application::Gl_context_provider>();
    require<Program_interface>();
}

void Mesh_memory::initialize_component()
{
    m_impl = std::make_unique<Mesh_memory_impl>();
}

}
