#include <fmt/core.h>
#include <fmt/ostream.h>

#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/vertex_attribute_mappings.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_verify/verify.hpp"

#include <gsl/assert>

#include <memory>
#include <sstream>

namespace erhe::graphics
{

std::mutex                       Vertex_input_state::s_mutex;
std::vector<Vertex_input_state*> Vertex_input_state::s_all_vertex_input_states;

void Vertex_input_state::on_thread_enter()
{
    const std::lock_guard lock{s_mutex};

    std::stringstream this_thread_id_ss;
    this_thread_id_ss << std::this_thread::get_id();
    const std::string this_thread_id_string = this_thread_id_ss.str();

    for (auto* vertex_input_state : s_all_vertex_input_states) {
        std::stringstream owner_thread_ss;
        owner_thread_ss << vertex_input_state->m_owner_thread;
        log_threads->trace(
            "{}: on thread enter: vertex input state @ {} owned by thread {}",
            this_thread_id_string, //std::this_thread::get_id(),
            fmt::ptr(vertex_input_state),
            owner_thread_ss.str() //vertex_input_state->m_owner_thread
        );
        if (vertex_input_state->m_owner_thread == std::thread::id{}) {
            vertex_input_state->create();
        }
    }
}

void Vertex_input_state::on_thread_exit()
{
    const std::lock_guard lock{s_mutex};

    gl::bind_vertex_array(0);
    auto this_thread_id = std::this_thread::get_id();
    std::stringstream this_thread_id_ss;
    this_thread_id_ss << this_thread_id;
    const std::string this_thread_id_string = this_thread_id_ss.str();
    for (auto* vertex_input_state : s_all_vertex_input_states) {
        std::stringstream owner_thread_ss;
        owner_thread_ss << vertex_input_state->m_owner_thread;
        log_threads->trace(
            "{}: on thread exit: vertex input state @ {} owned by thread {}",
            this_thread_id_string, //std::this_thread::get_id(),
            fmt::ptr(vertex_input_state),
            owner_thread_ss.str() // vertex_input_state->m_owner_thread
        );
        if (vertex_input_state->m_owner_thread == this_thread_id) {
            vertex_input_state->reset();
        }
    }
}

auto Vertex_input_state_data::make(
    const Vertex_attribute_mappings& mappings,
    const Vertex_format&             vertex_format,
    Buffer* const                    vertex_buffer,
    Buffer* const                    index_buffer
) -> Vertex_input_state_data
{
    Vertex_input_state_data result;
    result.index_buffer = index_buffer;
    mappings.collect_attributes(
        result.attributes,
        vertex_buffer,
        vertex_format
    );
    return result;
}

Vertex_input_state::Vertex_input_state()
{
    const std::lock_guard lock{s_mutex};

    s_all_vertex_input_states.push_back(this);

    create();
}

Vertex_input_state::Vertex_input_state(Vertex_input_state_data&& create_info)
    : m_data{std::move(create_info)}
{
    const std::lock_guard lock{s_mutex};

    s_all_vertex_input_states.push_back(this);

    create();
}

Vertex_input_state::~Vertex_input_state() noexcept
{
    if (!m_gl_vertex_array.has_value()) {
        return;
    }

    const std::lock_guard lock{s_mutex};

    s_all_vertex_input_states.erase(
        std::remove(
            s_all_vertex_input_states.begin(),
            s_all_vertex_input_states.end(),
            this
        ),
        s_all_vertex_input_states.end()
    );
}

void Vertex_input_state::set(const Vertex_input_state_data& data)
{
    m_data = data;
    update();
}

void Vertex_input_state::reset()
{
    // Delete VAO
    std::stringstream this_thread_id_ss;
    this_thread_id_ss << std::this_thread::get_id();
    //log_threads.trace("{}: reset @ {}\n", std::this_thread::get_id(), fmt::ptr(this));
    log_threads->trace("{}: reset @ {}", this_thread_id_ss.str(), fmt::ptr(this));
    m_owner_thread = {};
    m_gl_vertex_array.reset();

    Ensures(!m_gl_vertex_array.has_value());
}

void Vertex_input_state::create()
{
    std::stringstream this_thread_id_ss;
    this_thread_id_ss << std::this_thread::get_id();
    //log_threads.trace("{}: create @ {}\n", std::this_thread::get_id(), fmt::ptr(this));
    log_threads->trace("{}: create @ {}", this_thread_id_ss.str(), fmt::ptr(this));
    if (m_gl_vertex_array.has_value()) {
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

    //// const unsigned int max_attribute_count = std::min(
    ////     MAX_ATTRIBUTE_COUNT,
    ////     erhe::graphics::g_instance->limits.max_vertex_attribs
    //// );

    {
        const auto ibo_gl_name = (m_data.index_buffer != nullptr)
            ? m_data.index_buffer->gl_name()
            : 0;
        if (ibo_gl_name != 0) {
            gl::vertex_array_element_buffer(gl_name(), ibo_gl_name);
        }
    }

    for (const auto& attribute : m_data.attributes) {
        if (attribute.vertex_buffer == nullptr) {
            log_vertex_attribute_mappings->error("bad vertex input state: vbo == nullptr");
            continue;
        }
        //// if (attribute.layout_location >= max_attribute_count) {
        ////     log_vertex_attribute_mappings->error("bad vertex input state: layout location >= max attribute count");
        ////     continue;
        //// }

        gl::vertex_array_vertex_buffer(
            gl_name(),
            attribute.layout_location,
            attribute.vertex_buffer->gl_name(),
            intptr_t{attribute.offset},
            attribute.stride
        );

        switch (attribute.shader_type) {
            //using enum gl::Attribute_type;
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
            case gl::Attribute_type::unsigned_int_vec4: {
                gl::vertex_array_attrib_i_format(
                    gl_name(),
                    attribute.layout_location,
                    attribute.dimension,
                    static_cast<gl::Vertex_attrib_i_type>(attribute.data_type),
                    0
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
            case gl::Attribute_type::float_mat4x3: {
                gl::vertex_array_attrib_format(
                    gl_name(),
                    attribute.layout_location,
                    attribute.dimension,
                    attribute.data_type,
                    attribute.normalized ? GL_TRUE : GL_FALSE,
                    0
                );
                break;
            }

            case gl::Attribute_type::unsigned_int64_arb:
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
            case gl::Attribute_type::double_mat4x3: {
                gl::vertex_array_attrib_l_format(
                    gl_name(),
                    attribute.layout_location,
                    attribute.dimension,
                    static_cast<gl::Vertex_attrib_l_type>(attribute.data_type),
                    0
                );
                break;
            }

            default: {
                ERHE_FATAL("Bad vertex attrib pointer type");
            }
        }

        gl::enable_vertex_array_attrib(gl_name(), attribute.layout_location);
        gl::vertex_array_binding_divisor(gl_name(), attribute.layout_location, attribute.divisor);
    }
}

auto Vertex_input_state::gl_name() const -> unsigned int
{
    Expects(m_owner_thread == std::this_thread::get_id());

    return m_gl_vertex_array.has_value()
        ? m_gl_vertex_array.value().gl_name()
        : 0;
}

void Vertex_input_state_tracker::reset()
{
    gl::bind_vertex_array(0);
    m_last = 0;
}

void Vertex_input_state_tracker::execute(const Vertex_input_state* const state)
{
    const unsigned int name = (state != nullptr)
        ? state->gl_name()
        : 0;
    if (m_last == name) {
        return;
    }
    gl::bind_vertex_array(name);
    m_last = name;
}

} // namespace erhe::graphics
