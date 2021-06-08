#include "gl_context_provider.hpp"
#include "renderers/program_interface.hpp"
#include "renderers/mesh_memory.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/tracy_client.hpp"

namespace editor {

using namespace erhe::graphics;
using namespace erhe::primitive;
using namespace std;

Mesh_memory::Mesh_memory()
    : Component{c_name}
{
}

Mesh_memory::~Mesh_memory() = default;

void Mesh_memory::connect()
{
    require<Gl_context_provider>();
    require<Program_interface  >();
}

void Mesh_memory::initialize_component()
{
    ZoneScoped;

    Scoped_gl_context gl_context(Component::get<Gl_context_provider>().get());

    static constexpr gl::Buffer_storage_mask storage_mask{gl::Buffer_storage_mask::map_write_bit};

    const size_t vertex_byte_count  = 256 * 1024 * 1024;
    const size_t index_byte_count   =  64 * 1024 * 1024;
    m_buffer_info.index_type    = gl::Draw_elements_type::unsigned_int;
    m_buffer_info.vertex_buffer = make_shared<Buffer>(gl::Buffer_target::array_buffer,
                                                      vertex_byte_count,
                                                      storage_mask);
    m_buffer_info.index_buffer = make_shared<Buffer>(gl::Buffer_target::element_array_buffer,
                                                     index_byte_count,
                                                     storage_mask);

    m_buffer_info.vertex_buffer->set_debug_label("Scene Manager Vertex");
    m_buffer_info.index_buffer ->set_debug_label("Scene Manager Index");

    m_format_info.want_fill_triangles       = true;
    m_format_info.want_edge_lines           = true;
    m_format_info.want_centroid_points      = true;
    m_format_info.want_corner_points        = true;
    m_format_info.want_position             = true;
    m_format_info.want_normal               = true;
    m_format_info.want_normal_smooth        = true;
    m_format_info.want_tangent              = true;
    m_format_info.want_bitangent            = true;
    m_format_info.want_texcoord             = true;
    m_format_info.want_color                = true;
    m_format_info.want_id                   = true;
    m_format_info.normal_style              = Normal_style::corner_normals;
    m_format_info.vertex_attribute_mappings = &Component::get<Program_interface>()->attribute_mappings;

    Primitive_builder::prepare_vertex_format(m_format_info, m_buffer_info);

    m_buffer_transfer_queue   = make_unique<Buffer_transfer_queue>();
    m_primitive_build_context = make_unique<Primitive_build_context>(*m_buffer_transfer_queue.get(), m_format_info, m_buffer_info);
}

auto Mesh_memory::vertex_buffer() -> Buffer*
{
    return m_buffer_info.vertex_buffer.get();
}

auto Mesh_memory::index_buffer() -> Buffer*
{
    return m_buffer_info.index_buffer.get();
}

auto Mesh_memory::index_type() -> gl::Draw_elements_type
{
    return m_buffer_info.index_type;
}

auto Mesh_memory::vertex_format() -> shared_ptr<Vertex_format>
{
    return m_buffer_info.vertex_format;
}

auto Mesh_memory::vertex_format_info() const -> const erhe::primitive::Format_info&
{
    return m_format_info;
}

auto Mesh_memory::vertex_buffer_info() -> erhe::primitive::Buffer_info&
{
    return m_buffer_info;
}

auto Mesh_memory::primitive_build_context() -> erhe::primitive::Primitive_build_context&
{
    VERIFY(m_primitive_build_context);
    return *m_primitive_build_context.get();
}

auto Mesh_memory::buffer_transfer_queue() -> erhe::graphics::Buffer_transfer_queue&
{
    VERIFY(m_buffer_transfer_queue);
    return *m_buffer_transfer_queue.get();
}

}