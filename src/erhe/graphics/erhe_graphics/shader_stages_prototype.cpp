// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/glsl_format_source.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace erhe::graphics {

namespace {

gl::Program_interface program_interfaces[]
{
    gl::Program_interface::atomic_counter_buffer,              // GL 4.2
    gl::Program_interface::buffer_variable,                    // GL 4.3
    gl::Program_interface::compute_subroutine,                 // GL 4.3
    gl::Program_interface::compute_subroutine_uniform,         // GL 4.3
    gl::Program_interface::fragment_subroutine,                // GL 4.3
    gl::Program_interface::fragment_subroutine_uniform,        // GL 4.3
    gl::Program_interface::geometry_subroutine,                // GL 4.3
    gl::Program_interface::geometry_subroutine_uniform,        // GL 4.3
    gl::Program_interface::program_input,                      // GL 4.3
    gl::Program_interface::program_output,                     // GL 4.3
    gl::Program_interface::shader_storage_block,               // GL 4.3
    gl::Program_interface::tess_control_subroutine,            // GL 4.3
    gl::Program_interface::tess_control_subroutine_uniform,    // GL 4.3
    gl::Program_interface::tess_evaluation_subroutine,         // GL 4.3
    gl::Program_interface::tess_evaluation_subroutine_uniform, // GL 4.3
    gl::Program_interface::transform_feedback_buffer,          // GL 3.0
    gl::Program_interface::transform_feedback_varying,         // GL 4.3
    gl::Program_interface::uniform,                            // GL 4.3
    gl::Program_interface::uniform_block,                      // GL 4.3
    gl::Program_interface::vertex_subroutine,                  // GL 4.3
    gl::Program_interface::vertex_subroutine_uniform           // GL 4.3
};

[[nodiscard]] auto member_interface(const gl::Program_interface interface) -> std::optional<gl::Program_interface>
{
    switch (interface) {
        //using enum gl::Program_interface;
        case gl::Program_interface::uniform_block: {
            return gl::Program_interface::uniform;
        }

        case gl::Program_interface::shader_storage_block: {
            return gl::Program_interface::buffer_variable;
        }

        default: {
            return {};
        }
    }
}

gl::Program_resource_property program_resource_properties[]
{
    //gl::Program_resource_property::active_variables,                   // GL 4.3
    gl::Program_resource_property::array_size,                           // GL 4.3
    gl::Program_resource_property::array_stride,                         // GL 4.3
    gl::Program_resource_property::atomic_counter_buffer_index,          // GL 4.3
    gl::Program_resource_property::block_index,                          // GL 4.3
    gl::Program_resource_property::buffer_binding,                       // GL 4.3
    gl::Program_resource_property::buffer_data_size,                     // GL 4.3
    gl::Program_resource_property::compatible_subroutines,               // GL 4.0
    gl::Program_resource_property::is_per_patch,                         // GL 4.3
    gl::Program_resource_property::is_row_major,                         // GL 4.3
    gl::Program_resource_property::location,                             // GL 4.3
    gl::Program_resource_property::location_component,                   // GL 4.4
    gl::Program_resource_property::location_index,                       // GL 4.3
    gl::Program_resource_property::matrix_stride,                        // GL 4.3
    gl::Program_resource_property::name_length,                          // GL 4.3
    //gl::Program_resource_property::num_active_variables,               // GL 4.3
    gl::Program_resource_property::num_compatible_subroutines,           // GL 4.0
    gl::Program_resource_property::offset,                               // GL 4.3
    gl::Program_resource_property::referenced_by_compute_shader,         // GL 4.3
    gl::Program_resource_property::referenced_by_fragment_shader,        // GL 4.3
    gl::Program_resource_property::referenced_by_geometry_shader,        // GL 4.3
    gl::Program_resource_property::referenced_by_tess_control_shader,    // GL 4.3
    gl::Program_resource_property::referenced_by_tess_evaluation_shader, // GL 4.3
    gl::Program_resource_property::referenced_by_vertex_shader,          // GL 4.3
    gl::Program_resource_property::top_level_array_size,                 // GL 4.3
    gl::Program_resource_property::top_level_array_stride,               // GL 4.3
    gl::Program_resource_property::transform_feedback_buffer_index,      // GL 4.4
    gl::Program_resource_property::transform_feedback_buffer_stride,     // GL 4.4
    gl::Program_resource_property::type,                                 // GL 4.3
    // TODO why is this listed as program resource property?
    //gl::Program_resource_property::uniform
};

template <typename T>
[[nodiscard]] auto is_in_list(const T& item, std::initializer_list<T> items) -> bool
{
    return std::find(items.begin(), items.end(), item) != items.end();
}

// [OpenGL 4.6 (Core Profile)] Table 7.2 GetProgramResourceiv properties and supported interfaces
[[nodiscard]] auto is_program_interface_allowed(
    const gl::Program_resource_property property,
    const gl::Program_interface         interface
) -> bool
{
    switch (property) {
        //using enum gl::Program_resource_property;
        case gl::Program_resource_property::active_variables:
        case gl::Program_resource_property::buffer_binding:
        case gl::Program_resource_property::num_active_variables: {
            return is_in_list<gl::Program_interface>(
                interface,
                {
                    gl::Program_interface::atomic_counter_buffer,
                    gl::Program_interface::shader_storage_block,
                    gl::Program_interface::transform_feedback_buffer,
                    gl::Program_interface::uniform_block
                }
            );
        }

        case gl::Program_resource_property::array_size: {
            return is_in_list<gl::Program_interface>(
                interface,
                {
                    gl::Program_interface::buffer_variable,
                    gl::Program_interface::compute_subroutine_uniform,
                    gl::Program_interface::fragment_subroutine_uniform,
                    gl::Program_interface::geometry_subroutine_uniform,
                    gl::Program_interface::program_input,
                    gl::Program_interface::program_output,
                    gl::Program_interface::tess_control_subroutine_uniform,
                    gl::Program_interface::tess_evaluation_subroutine_uniform,
                    gl::Program_interface::transform_feedback_varying,
                    gl::Program_interface::uniform,
                    gl::Program_interface::vertex_subroutine_uniform
                }
            );
        }

        case gl::Program_resource_property::array_stride:
        case gl::Program_resource_property::block_index:
        case gl::Program_resource_property::is_row_major:
        case gl::Program_resource_property::matrix_stride: {
            return is_in_list<gl::Program_interface>(
                interface,
                {
                    gl::Program_interface::buffer_variable,
                    gl::Program_interface::uniform
                }
            );
        }

        case gl::Program_resource_property::atomic_counter_buffer_index: {
            return interface == gl::Program_interface::uniform;
        }

        case gl::Program_resource_property::buffer_data_size: {
            return is_in_list<gl::Program_interface>(
                interface,
                {
                    gl::Program_interface::atomic_counter_buffer,
                    gl::Program_interface::shader_storage_block,
                    gl::Program_interface::uniform_block
                }
            );
        }

        case gl::Program_resource_property::num_compatible_subroutines:
        case gl::Program_resource_property::compatible_subroutines: {
            return is_in_list<gl::Program_interface>(
                interface,
                {
                    gl::Program_interface::compute_subroutine_uniform,
                    gl::Program_interface::fragment_subroutine_uniform,
                    gl::Program_interface::geometry_subroutine_uniform,
                    gl::Program_interface::tess_control_subroutine_uniform,
                    gl::Program_interface::tess_evaluation_subroutine_uniform,
                    gl::Program_interface::vertex_subroutine_uniform
                }
            );
        }

        case gl::Program_resource_property::is_per_patch: {
            return is_in_list<gl::Program_interface>(
                interface,
                {
                    gl::Program_interface::program_input,
                    gl::Program_interface::program_output
                }
            );
        }

        case gl::Program_resource_property::location: {
            return is_in_list<gl::Program_interface>(
                interface,
                {
                    gl::Program_interface::compute_subroutine_uniform,
                    gl::Program_interface::fragment_subroutine_uniform,
                    gl::Program_interface::geometry_subroutine_uniform,
                    gl::Program_interface::program_input,
                    gl::Program_interface::program_output,
                    gl::Program_interface::tess_control_subroutine_uniform,
                    gl::Program_interface::tess_evaluation_subroutine_uniform,
                    gl::Program_interface::uniform,
                    gl::Program_interface::vertex_subroutine_uniform
                }
            );
        }

        case gl::Program_resource_property::location_component: {
            return is_in_list<gl::Program_interface>(
                interface,
                {
                    gl::Program_interface::program_input,
                    gl::Program_interface::program_output
                }
            );
        }

        case gl::Program_resource_property::location_index: {
            return is_in_list<gl::Program_interface>(
                interface,
                {
                    gl::Program_interface::program_output
                }
            );
        }

        case gl::Program_resource_property::name_length: {
            return is_in_list<gl::Program_interface>(
                interface,
                {
                    //gl::Program_interface::atomic_counter_buffer,
                    gl::Program_interface::buffer_variable,
                    gl::Program_interface::compute_subroutine,
                    gl::Program_interface::compute_subroutine_uniform,
                    gl::Program_interface::fragment_subroutine,
                    gl::Program_interface::fragment_subroutine_uniform,
                    gl::Program_interface::geometry_subroutine,
                    gl::Program_interface::geometry_subroutine_uniform,
                    gl::Program_interface::program_input,
                    gl::Program_interface::program_output,
                    gl::Program_interface::shader_storage_block,
                    gl::Program_interface::tess_control_subroutine,
                    gl::Program_interface::tess_control_subroutine_uniform,
                    gl::Program_interface::tess_evaluation_subroutine,
                    gl::Program_interface::tess_evaluation_subroutine_uniform,
                    //gl::Program_interface::transform_feedback_buffer,
                    gl::Program_interface::transform_feedback_varying,
                    gl::Program_interface::uniform,
                    gl::Program_interface::uniform_block,
                    gl::Program_interface::vertex_subroutine,
                    gl::Program_interface::vertex_subroutine_uniform
                }
            );
        }

        case gl::Program_resource_property::offset: {
            return is_in_list<gl::Program_interface>(
                interface,
                {
                    gl::Program_interface::buffer_variable,
                    gl::Program_interface::transform_feedback_varying,
                    gl::Program_interface::uniform
                }
            );
        }

        case gl::Program_resource_property::referenced_by_vertex_shader:
        case gl::Program_resource_property::referenced_by_tess_control_shader:
        case gl::Program_resource_property::referenced_by_tess_evaluation_shader:
        case gl::Program_resource_property::referenced_by_geometry_shader:
        case gl::Program_resource_property::referenced_by_fragment_shader:
        case gl::Program_resource_property::referenced_by_compute_shader: {
            return is_in_list<gl::Program_interface>(
                interface,
                {
                    gl::Program_interface::atomic_counter_buffer,
                    gl::Program_interface::buffer_variable,
                    gl::Program_interface::program_input,
                    gl::Program_interface::program_output,
                    gl::Program_interface::shader_storage_block,
                    gl::Program_interface::uniform,
                    gl::Program_interface::uniform_block
                }
            );
        }

        case gl::Program_resource_property::transform_feedback_buffer_index: {
            return interface == gl::Program_interface::transform_feedback_varying;
        }

        case gl::Program_resource_property::transform_feedback_buffer_stride: {
            return interface == gl::Program_interface::transform_feedback_buffer;
        }

        case gl::Program_resource_property::top_level_array_size:
        case gl::Program_resource_property::top_level_array_stride: {
            return interface == gl::Program_interface::buffer_variable;
        }

        case gl::Program_resource_property::type: {
            return is_in_list<gl::Program_interface>(
                interface,
                {
                    gl::Program_interface::buffer_variable,
                    gl::Program_interface::program_input,
                    gl::Program_interface::program_output,
                    gl::Program_interface::transform_feedback_varying,
                    gl::Program_interface::uniform
                }
            );
        }

        default: {
            ERHE_FATAL("Bad program property");
        }
    }
}

} // anonymous namespace

auto Shader_stages_prototype::compile(const Shader_stage& shader) -> Gl_shader
{
    ERHE_PROFILE_FUNCTION();

    Gl_shader gl_shader{m_graphics_instance, shader.type};

    if (m_state == state_fail) {
        return gl_shader;
    }
    ERHE_VERIFY((m_state == state_init) || (m_state == state_shader_compilation_started));
    const auto gl_name = gl_shader.gl_name();

    const std::string source = get_final_source(shader, gl_name);
    if (source.empty()) {
        m_state = state_fail;
        return gl_shader;
    }
    m_final_sources[gl_name] = source;
    const char* const c_source = source.c_str();
    std::array<const char* , 1> sources{ c_source };

    SPDLOG_LOGGER_TRACE(
        log_glsl,
        "Shader_stage compile {} {} source:\nPath: {}\n{}\n",
        gl::c_str(shader.type),
        gl_name,
        shader.path.string(),
        format_source(source)
    );

    {
        ERHE_PROFILE_SCOPE("glCompileShader");
        gl::shader_source(gl_name, static_cast<GLsizei>(sources.size()), sources.data(), nullptr);
        gl::compile_shader(gl_name);
    }

    m_state = state_shader_compilation_started;

    return gl_shader;
}

auto Shader_stages_prototype::get_final_source(const Shader_stage& shader, std::optional<unsigned int> gl_name) -> std::string
{
    if (gl_name.has_value()) {
        const auto i = m_final_sources.find(gl_name.value());
        if (i != m_final_sources.end()) {
            return i->second;
        }
    }
    return m_create_info.final_source(m_graphics_instance, shader, &m_paths, gl_name);
}

auto Shader_stages_prototype::post_compile(const Shader_stage& shader, Gl_shader& gl_shader) -> bool
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(m_state == state_shader_compilation_started);

    const auto gl_name = gl_shader.gl_name();

    int delete_status {0};
    int compile_status{0};
    gl::get_shader_iv(gl_name, gl::Shader_parameter_name::compile_status, &compile_status);
    gl::get_shader_iv(gl_name, gl::Shader_parameter_name::delete_status,  &delete_status);

    if (compile_status != GL_TRUE) {
        ERHE_PROFILE_SCOPE("get info log");
        int length{0};
        gl::get_shader_iv(gl_name, gl::Shader_parameter_name::info_log_length, &length);
        std::string log(static_cast<std::string::size_type>(length) + 1, '\0');
        gl::get_shader_info_log(gl_name, length, nullptr, &log[0]);
        const std::string source = get_final_source(shader, gl_name);
        const std::string f_source = format_source(source);
        log_program->error("Shader_stage compilation failed:");
        log_program->error("{}", log);
        log_glsl->error("\n{}", f_source);
        log_program->error("Shader_stage compilation failed:");
        log_program->error("{}", log);
        return false;
    }
    return true;
}

Shader_stages_prototype::Shader_stages_prototype(Instance& graphics_instance, Shader_stages_create_info&& create_info)
    : m_graphics_instance    {graphics_instance}
    , m_handle               {graphics_instance}
    , m_create_info          {create_info}
    , m_default_uniform_block{graphics_instance}
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(m_handle.gl_name() != 0);
    if (create_info.build) {
        post_link();
    }
}
Shader_stages_prototype::Shader_stages_prototype(Instance& graphics_instance, const Shader_stages_create_info& create_info)
    : m_graphics_instance    {graphics_instance}
    , m_handle               {graphics_instance}
    , m_create_info          {create_info}
    , m_default_uniform_block{graphics_instance}
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(m_handle.gl_name() != 0);
    if (create_info.build) {
        post_link();
    }
}

void Shader_stages_prototype::compile_shaders()
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(m_state == state_init);
    for (const auto& shader : m_create_info.shaders) {
        m_prelink_shaders.emplace_back(compile(shader));

#if defined(ERHE_SPIRV)
        m_glslang_shaders.push_back(compile_glslang(shader));
#endif
        if (m_state == state_fail) {
            break;
        }
    }
}

auto Shader_stages_prototype::link_program() -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (m_state == state_fail) {
        return false;
    }

    if (m_state == state_init) {
        compile_shaders();
    }

    if (m_state == state_fail) {
        return false;
    }

    ERHE_VERIFY(m_state == state_shader_compilation_started);

    const auto gl_name = m_handle.gl_name();
    ERHE_VERIFY(m_prelink_shaders.size() == m_create_info.shaders.size());
    for (std::size_t i = 0, end = m_prelink_shaders.size(); i < end; ++i) {
        if (!post_compile(m_create_info.shaders[i], m_prelink_shaders[i])) {
            m_state = state_fail;
            return false;
        }
        ERHE_PROFILE_SCOPE("glAttachShader");
        gl::attach_shader(gl_name, m_prelink_shaders[i].gl_name());
    }

    {
        ERHE_PROFILE_SCOPE("glLinkProgram");
        gl::link_program(gl_name);
    }
    m_state = state_program_link_started;

    if (m_graphics_instance.info.glsl_version < 420) {
        query_bindings();
    }

#if defined(ERHE_SPIRV)
    link_glslang_program();
#endif
    return true;
}

void Shader_stages_prototype::query_bindings()
{
    std::map<std::string, unsigned int> erhe_bindings;
    unsigned int max_binding = 0;
    for (const Shader_resource* block : m_create_info.interface_blocks) {
        if (block->get_binding_target() == gl::Buffer_target::uniform_buffer) {
            std::string key = block->name() + "_block";
            erhe_bindings[key] = block->binding_point();
            max_binding = std::max(max_binding, block->binding_point());
        }
    }

    m_gl_from_erhe_bindings.resize(max_binding + 1);

    const auto gl_name = m_handle.gl_name();
    int active_uniform_block_count = 0;
    gl::get_program_iv(gl_name, gl::Program_property::active_uniform_blocks, &active_uniform_block_count);
    for (int i = 0; i < active_uniform_block_count; ++i) {
        int name_length = 0;
        gl::get_active_uniform_block_iv(gl_name, i, gl::Uniform_block_p_name::uniform_block_name_length, &name_length);
        ERHE_VERIFY(name_length > 1);
        std::string name(static_cast<std::size_t>(name_length - 1), 0);
        gl::get_active_uniform_block_name(gl_name, i, name_length, nullptr, name.data());
        auto fi = erhe_bindings.find(name);
        ERHE_VERIFY(fi != erhe_bindings.end());
        unsigned int erhe_binding = fi->second;

        int gl_binding = 0;
        gl::get_active_uniform_block_iv(gl_name, i, gl::Uniform_block_p_name::uniform_block_binding, &gl_binding);
        ERHE_VERIFY(gl_binding > -1);

        m_gl_from_erhe_bindings[erhe_binding] = gl_binding;
    }
}

void Shader_stages_prototype::post_link()
{
    ERHE_PROFILE_FUNCTION();

    if (m_state == state_fail) {
        return;
    }

    if (m_state != state_program_link_started) {
        link_program();
        if (m_state == state_fail) {
            return;
        }
    }

    ERHE_VERIFY(m_state == state_program_link_started);
    int link_status                          {0};
    int validate_status                      {0};
    int info_log_length                      {0};
    int attached_shaders                     {0};
    int active_attributes                    {0};
    int active_attribute_max_length          {128};
    int active_uniforms                      {0};
    int active_uniform_max_length            {128};
    int active_uniform_blocks                {0};
    int active_uniform_block_max_name_length {128};
    int transform_feedback_buffer_mode       {static_cast<int>(gl::Transform_feedback_buffer_mode::interleaved_attribs)};
    int transform_feedback_varyings          {0};
    int transform_feedback_varying_max_length{0};

    const auto gl_name = m_handle.gl_name();

    gl::get_program_iv(gl_name, gl::Program_property::link_status,                           &link_status);
    gl::get_program_iv(gl_name, gl::Program_property::validate_status,                       &validate_status);
    gl::get_program_iv(gl_name, gl::Program_property::info_log_length,                       &info_log_length);
    gl::get_program_iv(gl_name, gl::Program_property::attached_shaders,                      &attached_shaders);
    gl::get_program_iv(gl_name, gl::Program_property::active_attributes,                     &active_attributes);
    gl::get_program_iv(gl_name, gl::Program_property::active_attribute_max_length,           &active_attribute_max_length);
    gl::get_program_iv(gl_name, gl::Program_property::active_uniforms,                       &active_uniforms);
    gl::get_program_iv(gl_name, gl::Program_property::active_uniform_max_length,             &active_uniform_max_length);
    gl::get_program_iv(gl_name, gl::Program_property::active_uniform_blocks,                 &active_uniform_blocks);
    gl::get_program_iv(gl_name, gl::Program_property::active_uniform_block_max_name_length,  &active_uniform_block_max_name_length);
    gl::get_program_iv(gl_name, gl::Program_property::transform_feedback_buffer_mode,        &transform_feedback_buffer_mode);
    gl::get_program_iv(gl_name, gl::Program_property::transform_feedback_varyings,           &transform_feedback_varyings);
    gl::get_program_iv(gl_name, gl::Program_property::transform_feedback_varying_max_length, &transform_feedback_varying_max_length);

    // Only if there is compute shader
    //int compute_work_group_size[3]          = {0, 0, 0};
    //gl::get_program_iv(gl_name, gl::Program_property::compute_work_group_size,               &compute_work_group_size[0]);

    if (link_status != GL_TRUE) {
        ERHE_PROFILE_SCOPE("get link log");
        m_state = state_fail;
        std::string log(static_cast<std::size_t>(info_log_length) + 1, 0);
        gl::get_program_info_log(gl_name, info_log_length, nullptr, &log[0]);
        log_program->error("Shader_stages linking failed:");
        log_program->error("{}", log);
        ERHE_VERIFY(m_prelink_shaders.size() == m_create_info.shaders.size());
        for (size_t i = 0, end = m_prelink_shaders.size(); i < end; ++i) {
            log_program->error("Shader_stages linking failed:");
            const std::string source = get_final_source(m_create_info.shaders[i], m_prelink_shaders[i].gl_name());
            const std::string f_source = format_source(source);
            log_glsl->error("\n{}", f_source);
        }
        log_program->error("Shader_stages linking failed:");
        log_program->error("{}", log);
        return;
    } else {
        m_state = state_ready;
        log_program->trace("Shader_stages linking succeeded:");
        ERHE_VERIFY(m_prelink_shaders.size() == m_create_info.shaders.size());
        for (size_t i = 0, end = m_prelink_shaders.size(); i < end; ++i) {
            const std::string source = get_final_source(m_create_info.shaders[i], m_prelink_shaders[i].gl_name());
            const std::string f_source = format_source(source);
            log_glsl->trace("\n{}", f_source);
        }
        if (m_create_info.dump_reflection) {
            dump_reflection();
        }
        if (m_create_info.dump_interface) {
            const std::string f_source = format_source(m_create_info.interface_source());
            log_glsl->info("\n{}", f_source);
        }
        if (m_create_info.dump_final_source) {
            ERHE_VERIFY(m_prelink_shaders.size() == m_create_info.shaders.size());
            for (size_t i = 0, end = m_prelink_shaders.size(); i < end; ++i) {
                const std::string source = get_final_source(m_create_info.shaders[i], m_prelink_shaders[i].gl_name());
                const std::string f_source = format_source(source);
                log_glsl->info("\n{}", f_source);
            }
        }
    }
    m_prelink_shaders.clear();
}

auto Shader_stages_prototype::is_valid() -> bool
{
    if ((m_state != state_ready) && (m_state != state_fail)) {
        post_link();
    }
    if (m_state == state_ready) {
        return true;
    }
    //if (m_state == state_fail)
    {
        return false;
    }
}

auto is_array_and_nonzero(const std::string& name)
{
    const std::size_t open_bracket_pos = name.find_first_of('[');
    if (open_bracket_pos == std::string::npos) {
        return false;
    }

    const std::size_t digit_pos = name.find_first_of("0123456789", open_bracket_pos + 1);
    if (digit_pos != open_bracket_pos + 1) {
        return false;
    }

    const std::size_t non_digit_pos = name.find_first_not_of("0123456789", digit_pos + 1);
    if (non_digit_pos == std::string::npos) {
        return false;
    }

    if (name.at(non_digit_pos) != ']') {
        return false;
    }

    const std::size_t close_bracket_pos = non_digit_pos;
    const char        digit             = name.at(digit_pos);

    if (
        (close_bracket_pos == (open_bracket_pos + 2)) &&
        (
            (digit == '0') || (digit == '1')
        )
    ) {
        return false;
    }

    return true;
}

void Shader_stages_prototype::dump_reflection() const
{
    ERHE_PROFILE_FUNCTION();

    const int gl_name = m_handle.gl_name();

    int link_status                          {0};
    int validate_status                      {0};
    int info_log_length                      {0};
    int attached_shaders                     {0};
    int active_attributes                    {0};
    int active_attribute_max_length          {128};
    int active_uniforms                      {0};
    int active_uniform_max_length            {128};
    int active_uniform_blocks                {0};
    int active_uniform_block_max_name_length {128};
    int transform_feedback_buffer_mode       {static_cast<int>(gl::Transform_feedback_buffer_mode::interleaved_attribs)};
    int transform_feedback_varyings          {0};
    int transform_feedback_varying_max_length{0};

    gl::get_program_iv(gl_name, gl::Program_property::link_status,                           &link_status);
    gl::get_program_iv(gl_name, gl::Program_property::validate_status,                       &validate_status);
    gl::get_program_iv(gl_name, gl::Program_property::info_log_length,                       &info_log_length);
    gl::get_program_iv(gl_name, gl::Program_property::attached_shaders,                      &attached_shaders);
    gl::get_program_iv(gl_name, gl::Program_property::active_attributes,                     &active_attributes);
    gl::get_program_iv(gl_name, gl::Program_property::active_attribute_max_length,           &active_attribute_max_length);
    gl::get_program_iv(gl_name, gl::Program_property::active_uniforms,                       &active_uniforms);
    gl::get_program_iv(gl_name, gl::Program_property::active_uniform_max_length,             &active_uniform_max_length);
    gl::get_program_iv(gl_name, gl::Program_property::active_uniform_blocks,                 &active_uniform_blocks);
    gl::get_program_iv(gl_name, gl::Program_property::active_uniform_block_max_name_length,  &active_uniform_block_max_name_length);
    gl::get_program_iv(gl_name, gl::Program_property::transform_feedback_buffer_mode,        &transform_feedback_buffer_mode);
    gl::get_program_iv(gl_name, gl::Program_property::transform_feedback_varyings,           &transform_feedback_varyings);
    gl::get_program_iv(gl_name, gl::Program_property::transform_feedback_varying_max_length, &transform_feedback_varying_max_length);

    // Figure out maximum name length across all interfaces
    int max_name_length{0};
    for (auto interface : program_interfaces) {
        if (
            (interface == gl::Program_interface::atomic_counter_buffer    ) ||
            (interface == gl::Program_interface::transform_feedback_buffer)
        ) {
            continue;
        }

        int interface_max_name_length{0};
        gl::get_program_interface_iv(gl_name, interface, gl::Program_interface_p_name::max_name_length, &interface_max_name_length);
        max_name_length = std::max(max_name_length, interface_max_name_length);
    }

    GLsizei name_length{0};
    std::vector<char> name_buffer;
    name_buffer.resize(static_cast<size_t>(max_name_length) + 1);

    for (auto interface : program_interfaces) {
        int active_resource_count{0};
        gl::get_program_interface_iv(gl_name, interface, gl::Program_interface_p_name::active_resources, &active_resource_count);
        if (active_resource_count == 0) {
            continue;

        }
        log_program->info("{:<40} : {} resources", c_str(interface), active_resource_count);

        std::string name;
        for (int i = 0; i < active_resource_count; ++i) {
            if (interface != gl::Program_interface::atomic_counter_buffer) {
                std::fill(begin(name_buffer), end(name_buffer), '\0');
                gl::get_program_resource_name(gl_name, interface, i, max_name_length, &name_length, name_buffer.data());
                name = std::string(name_buffer.data(), name_length);
                if (is_array_and_nonzero(name)) {
                    continue;
                }
                log_program->info("\t{:<40} : {}", i, name);
            } else {
                log_program->info("\t{:<40} :", i);
            }

            gl::Program_resource_property property_num_active_variables = gl::Program_resource_property::num_active_variables;
            gl::Program_resource_property property_active_variables     = gl::Program_resource_property::active_variables;
            if (
                is_program_interface_allowed(property_num_active_variables, interface) &&
                is_program_interface_allowed(property_active_variables,     interface)
            ) {
                GLsizei length{0};
                GLint num_active_variables{0};
                gl::get_program_resource_iv(gl_name, interface, i, 1, &property_num_active_variables, 1, &length, &num_active_variables);
                log_program->info("\t\t{:<40} = {}", c_str(property_num_active_variables), num_active_variables);
                if (num_active_variables > 0) {
                    std::vector<GLint> indices;
                    indices.resize(num_active_variables);
                    std::fill(begin(indices), end(indices), 0);
                    gl::get_program_resource_iv(gl_name, interface, i, 1, &property_active_variables, num_active_variables, &length, indices.data());
                    std::stringstream ss;
                    ss << fmt::format("\t\t{:<40} = [ ", c_str(property_active_variables));
                    bool first{true};
                    bool skipped{false};
                    for (int j = 0; j < num_active_variables; ++j) {
                        const auto member_interface_ = member_interface(interface);
                        if (member_interface_.has_value()) {
                            std::fill(begin(name_buffer), end(name_buffer), '\0');
                            gl::get_program_resource_name( gl_name, member_interface_.value(), indices[j], max_name_length, &name_length, name_buffer.data());
                            name = std::string{name_buffer.data(), static_cast<size_t>(name_length)};
                            if (is_array_and_nonzero(name)) {
                                skipped = true;
                                continue;
                            }
                            if (skipped) {
                                ss << " ... ";
                                skipped = false;
                            }
                            if (!first) {
                                ss << ", ";
                            }
                            ss << fmt::format("{} {}", indices[j], name);
                            first = false;
                        } else {
                            ss << fmt::format("{}", indices[j]);
                        }
                    }
                    ss << " ]";
                    log_program->info("{}", ss.str());
                }
            }

            // Query resource properties
            for (auto property : program_resource_properties) {
                if (!is_program_interface_allowed(property, interface)) {
                    continue;
                }
                GLsizei length{0};
                int param{0};
                gl::get_program_resource_iv(gl_name, interface, i, 1, &property, 1, &length, &param);
                if (property != gl::Program_resource_property::type) {
                    log_program->info("\t\t{:<40} = {}", c_str(property), param);
                } else {
                    log_program->info("\t\t{:<40} = {}", c_str(property), gl::enum_string(param));
                }
            }
        }
    }

    //const int buffer_size = std::max(active_uniform_max_length, active_uniform_block_max_name_length);
    //std::vector<char> buffer(static_cast<size_t>(buffer_size) + 1);

    // For each interface block,
    // ask GL driver locations of those uniforms, and place to uniform map for the program (prototype)
    /// for (const auto& i : create_info.interface_blocks) {
    ///     auto block = i.second;
    ///     m_uniform_map.resize(block->members().size());
    ///     for (auto& member : block->members()) {
    ///         std::stringstream ss;
    ///         ss << block->name() << "_" << member.name();
    ///         auto key = member.index_in_block();
    ///         if (key >= 0) {
    ///             auto u_key = static_cast<unsigned int>(key);
    ///             auto uniform_name = ss.str();
    ///             if (m_uniform_map.size() < (u_key + 1)) {
    ///                 m_uniform_map.resize(u_key + 1);
    ///             }
    ///  ///             m_uniform_map[u_key] = gl::get_uniform_location(gl_name, uniform_name.c_str());
    ///         }
    ///     }
    /// }
}

auto Shader_stages_prototype::create_info() const -> const Shader_stages_create_info&
{
    return m_create_info;
}

auto Shader_stages_prototype::name() const -> const std::string&
{
    return m_create_info.name;
}

} // namespace erhe::graphics
