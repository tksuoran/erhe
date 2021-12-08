#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/log.hpp"
#include "erhe/graphics/vertex_attribute.hpp"
#include "erhe/graphics/vertex_attribute_mapping.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/toolkit/verify.hpp"

#include <fmt/ostream.h>

#include <bitset>
#include <gsl/assert>
#include <memory>

namespace erhe::graphics
{

std::mutex                       Vertex_input_state::s_mutex;
size_t                           Vertex_input_state::s_serial{0};
std::vector<Vertex_input_state*> Vertex_input_state::s_all_vertex_input_states;

Vertex_input_state::Vertex_input_state()
    : m_serial{get_next_serial()}
{
    std::lock_guard lock{s_mutex};

    s_all_vertex_input_states.push_back(this);
}

Vertex_input_state::Vertex_input_state(
    const Vertex_attribute_mappings& attribute_mappings,
    const Vertex_format&             vertex_format,
    const Buffer*                    vertex_buffer,
    const Buffer*                    index_buffer
)
    : m_index_buffer{index_buffer}
    , m_serial      {get_next_serial()}
{
    {
        std::lock_guard lock{s_mutex};

        s_all_vertex_input_states.push_back(this);
    }

    attribute_mappings.apply_to_vertex_input_state(*this, vertex_buffer, vertex_format);
    create();
}

Vertex_input_state::~Vertex_input_state()
{
    std::lock_guard lock{s_mutex};

    s_all_vertex_input_states.erase(
        std::remove(
            s_all_vertex_input_states.begin(),
            s_all_vertex_input_states.end(),
            this
        ),
        s_all_vertex_input_states.end()
    );
}

void Vertex_input_state::emplace_back(
    gsl::not_null<const Buffer*>                     vertex_buffer,
    const std::shared_ptr<Vertex_attribute_mapping>& mapping,
    const Vertex_attribute*                          attribute,
    const size_t                                     stride
) //-> Vertex_input_state::Binding&
{
    //return
    auto binding = std::make_shared<Vertex_input_state::Binding>(vertex_buffer, mapping, attribute, stride);
    m_bindings.push_back(binding);
}

void Vertex_input_state::reset()
{
    // Delete VAO
    log_threads.trace("{}: reset @ {}\n", std::this_thread::get_id(), fmt::ptr(this));
    m_owner_thread = {};
    m_gl_vertex_array.reset();

    Ensures(!m_gl_vertex_array.has_value());
}

void Vertex_input_state::set_index_buffer(const Buffer* buffer)
{
    Expects(m_index_buffer == nullptr);

    m_index_buffer = buffer;
}

void Vertex_input_state::create()
{
    log_threads.trace("{}: create @ {}\n", std::this_thread::get_id(), fmt::ptr(this));
    if (m_gl_vertex_array.has_value())
    {
        ERHE_VERIFY(m_owner_thread == std::this_thread::get_id());
        return;
    }

    m_owner_thread = std::this_thread::get_id();
    m_gl_vertex_array.emplace(Gl_vertex_array{});

    update();

    Ensures(m_gl_vertex_array.has_value());
}

void Vertex_input_state::update()
{
    ERHE_VERIFY(m_owner_thread == std::this_thread::get_id());
    Expects(m_gl_vertex_array.has_value());
    Expects(gl_name() > 0);

    unsigned int max_attribute_count = std::min(
        MAX_ATTRIBUTE_COUNT,
        erhe::graphics::Instance::limits.max_vertex_attribs
    );

    {
        auto ibo_gl_name = (m_index_buffer != nullptr)
            ? m_index_buffer->gl_name()
            : 0;
        gl::vertex_array_element_buffer(gl_name(), ibo_gl_name);
    }

    std::bitset<MAX_ATTRIBUTE_COUNT> enabled_attributes;

    for (auto binding : m_bindings)
    {
        const auto*       vbo       = binding->vertex_buffer;
        const auto* const attribute = binding->vertex_attribute;
        const auto        mapping   = binding->vertex_attribute_mapping;

        ERHE_VERIFY(vbo != nullptr);
        ERHE_VERIFY(attribute != nullptr);
        ERHE_VERIFY(mapping->layout_location < max_attribute_count);

        gl::vertex_array_vertex_buffer(
            gl_name(),
            static_cast<GLuint>(mapping->layout_location),
            vbo->gl_name(),
            intptr_t(0),
            static_cast<int>(binding->stride)
        );

        switch (attribute->shader_type)
        {
            case gl::Attribute_type::bool_:
            case gl::Attribute_type::bool_vec2:
            case gl::Attribute_type::bool_vec3:
            case gl::Attribute_type::bool_vec4:
            case gl::Attribute_type::int_:
            case gl::Attribute_type::int_vec2:
            case gl::Attribute_type::int_vec3:
            case gl::Attribute_type::int_vec4:
            case gl::Attribute_type::unsigned_int:
            case gl::Attribute_type::unsigned_int_vec2:
            case gl::Attribute_type::unsigned_int_vec3:
            case gl::Attribute_type::unsigned_int_vec4:
            {
                gl::vertex_array_attrib_i_format(
                    gl_name(),
                    static_cast<GLuint>(mapping->layout_location),
                    static_cast<GLint>(attribute->data_type.dimension),
                    static_cast<gl::Vertex_attrib_i_type>(attribute->data_type.type),
                    static_cast<unsigned int>(attribute->offset)
                );
                break;
            }

            case gl::Attribute_type::float_:
            case gl::Attribute_type::float_vec2:
            case gl::Attribute_type::float_vec3:
            case gl::Attribute_type::float_vec4:
            case gl::Attribute_type::float_mat2:
            case gl::Attribute_type::float_mat2x3:
            case gl::Attribute_type::float_mat2x4:
            case gl::Attribute_type::float_mat3:
            case gl::Attribute_type::float_mat3x2:
            case gl::Attribute_type::float_mat3x4:
            case gl::Attribute_type::float_mat4:
            case gl::Attribute_type::float_mat4x2:
            case gl::Attribute_type::float_mat4x3:
            {
                gl::vertex_array_attrib_format(
                    gl_name(),
                    static_cast<GLuint>(mapping->layout_location),
                    static_cast<GLint>(attribute->data_type.dimension),
                    attribute->data_type.type,
                    attribute->data_type.normalized ? GL_TRUE : GL_FALSE,
                    static_cast<unsigned int>(attribute->offset)
                );
                break;
            }

            case gl::Attribute_type::double_:
            case gl::Attribute_type::double_vec2:
            case gl::Attribute_type::double_vec3:
            case gl::Attribute_type::double_vec4:
            case gl::Attribute_type::double_mat2:
            case gl::Attribute_type::double_mat2x3:
            case gl::Attribute_type::double_mat2x4:
            case gl::Attribute_type::double_mat3:
            case gl::Attribute_type::double_mat3x2:
            case gl::Attribute_type::double_mat3x4:
            case gl::Attribute_type::double_mat4:
            case gl::Attribute_type::double_mat4x2:
            case gl::Attribute_type::double_mat4x3:
            {
                gl::vertex_array_attrib_l_format(
                    gl_name(),
                    static_cast<GLuint>(mapping->layout_location),
                    static_cast<GLint>(attribute->data_type.dimension),
                    static_cast<gl::Vertex_attrib_l_type>(attribute->data_type.type),
                    static_cast<unsigned int>(attribute->offset)
                );
                break;
            }

            default:
            {
                ERHE_FATAL("Bad vertex attrib pointer type");
            }
        }

        enabled_attributes.set(mapping->layout_location);
        gl::enable_vertex_array_attrib(gl_name(), static_cast<GLuint>(mapping->layout_location));
        gl::vertex_array_binding_divisor(gl_name(), static_cast<GLuint>(mapping->layout_location), attribute->divisor);
    }

#if 0
    // Avoid leaking previously enabled attributes
    for (size_t i = 0; i < max_attribute_count; ++i)
    {
        if (!enabled_attributes.test(i))
        {
            gl::disable_vertex_array_attrib(gl_name(), i);
        }
    }
#endif
}

auto Vertex_input_state::gl_name() const
-> unsigned int
{
    ERHE_VERIFY(m_owner_thread == std::this_thread::get_id());
    return m_gl_vertex_array.has_value()
        ? m_gl_vertex_array.value().gl_name()
        : 0;

}

} // namespace erhe::graphics
