#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/log.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"

#include <algorithm>
#include <sstream>

namespace erhe::graphics
{

using std::string;

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

auto member_interface(gl::Program_interface interface)
-> std::optional<gl::Program_interface>
{
    switch (interface)
    {
        case gl::Program_interface::uniform_block:
        {
            return gl::Program_interface::uniform;
        }

        case gl::Program_interface::shader_storage_block:
        {
            return gl::Program_interface::buffer_variable;
        }

        default:
        {
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

template <typename T> auto
is_in_list(const T& item, std::initializer_list<T> items)
-> bool
{
    return std::find(items.begin(),
                     items.end(),
                     item) != items.end();
}

// [OpenGL 4.6 (Core Profile)] Table 7.2 GetProgramResourceiv properties and supported interfaces
auto is_program_interface_allowed(gl::Program_resource_property property,
                                  gl::Program_interface         interface)
-> bool
{
    switch (property)
    {
        case gl::Program_resource_property::active_variables:
        case gl::Program_resource_property::buffer_binding:
        case gl::Program_resource_property::num_active_variables:
        {
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

        case gl::Program_resource_property::array_size:
        {
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
        case gl::Program_resource_property::matrix_stride:
        {
            return is_in_list<gl::Program_interface>(
                interface,
                {
                    gl::Program_interface::buffer_variable,
                    gl::Program_interface::uniform
                }
            );
        }

        case gl::Program_resource_property::atomic_counter_buffer_index:
        {
            return interface == gl::Program_interface::uniform;
        }

        case gl::Program_resource_property::buffer_data_size:
        {
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
        case gl::Program_resource_property::compatible_subroutines:
        {
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

        case gl::Program_resource_property::is_per_patch:
        {
            return is_in_list<gl::Program_interface>(
                interface,
                {
                    gl::Program_interface::program_input,
                    gl::Program_interface::program_output
                }
            );
        }

        case gl::Program_resource_property::location:
        {
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

        case gl::Program_resource_property::location_component:
        {
            return is_in_list<gl::Program_interface>(
                interface,
                {
                    gl::Program_interface::program_input,
                    gl::Program_interface::program_output
                }
            );
        }

        case gl::Program_resource_property::location_index:
        {
            return is_in_list<gl::Program_interface>(
                interface,
                {
                    gl::Program_interface::program_output
                }
            );
        }

        case gl::Program_resource_property::name_length:
        {
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

        case gl::Program_resource_property::offset:
        {
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
        case gl::Program_resource_property::referenced_by_compute_shader:
        {
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

        case gl::Program_resource_property::transform_feedback_buffer_index:
        {
            return interface == gl::Program_interface::transform_feedback_varying;
        }

        case gl::Program_resource_property::transform_feedback_buffer_stride:
        {
            return interface == gl::Program_interface::transform_feedback_buffer;
        }

        case gl::Program_resource_property::top_level_array_size:
        case gl::Program_resource_property::top_level_array_stride:
        {
            return interface == gl::Program_interface::buffer_variable;
        }

        case gl::Program_resource_property::type:
        {
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

        default:
        {
            FATAL("Bad program property");
        }
    }
}

}

auto Shader_stages::Prototype::try_compile_shader(const Shader_stages::Create_info&               create_info,
                                                  const Shader_stages::Create_info::Shader_stage& shader)
-> std::optional<Gl_shader>
{
    Gl_shader gl_shader(shader.type);
    auto gl_name = gl_shader.gl_name();

    string source = create_info.final_source(shader);
    VERIFY(source.length() > 0);
    const char* c_source = source.c_str();
    std::array<const char* , 1> sources { c_source};

    log_glsl.trace("Shader_stage source:\n{}\n", format(create_info.final_source(shader)));

    gl::shader_source(gl_name, static_cast<GLsizei>(sources.size()), sources.data(), nullptr);
    gl::compile_shader(gl_name);

    int delete_status {0};
    int compile_status{0};
    gl::get_shader_iv(gl_name, gl::Shader_parameter_name::compile_status, &compile_status);
    gl::get_shader_iv(gl_name, gl::Shader_parameter_name::delete_status,  &delete_status);
    if (compile_status != GL_TRUE)
    {
        int length{0};
        gl::get_shader_iv(gl_name, gl::Shader_parameter_name::info_log_length, &length);
        string log(static_cast<size_t>(length) + 1, 0);
        gl::get_shader_info_log(gl_name, length, nullptr, &log[0]);
        string f_source = format(sources[0]);
        log_program.error("Shader_stage compilation failed: \n");
        log_program.error("{}", log);
        log_glsl.error("\n{}\n", f_source);
        log_program.error("Shader_stage compilation failed: \n");
        log_program.error("{}", &log[0]);
        return {};
    }
    return {std::move(gl_shader)};
}

Shader_stages::Prototype::Prototype(const Shader_stages::Create_info& create_info)
    : m_name{create_info.name}
{
    Expects(m_handle.gl_name() != 0);

    auto gl_name = m_handle.gl_name();
    for (const auto& shader : create_info.shaders)
    {
        auto gl_shader = try_compile_shader(create_info, shader);
        if (!gl_shader.has_value())
        {
            return;
        }
        gl::attach_shader(gl_name, gl_shader->gl_name());
        m_attached_shaders.emplace_back(std::move(gl_shader.value()));
    }

    if (!create_info.transform_feedback_varyings.empty())
    {
        std::vector<char const *> c_array(create_info.transform_feedback_varyings.size());
        for (size_t i = 0; i < create_info.transform_feedback_varyings.size(); ++i)
        {
            c_array[i] = create_info.transform_feedback_varyings[i].c_str();
        }

        gl::transform_feedback_varyings(gl_name,
                                        static_cast<GLsizei>(c_array.size()),
                                        c_array.data(),
                                        create_info.transform_feedback_buffer_mode);
    }

    gl::link_program(gl_name);

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

    // Only if there is compute shader
    //int compute_work_group_size[3]          = {0, 0, 0};
    //gl::get_program_iv(gl_name, gl::Program_property::compute_work_group_size,               &compute_work_group_size[0]);

    if (link_status != GL_TRUE)
    {
        string log(static_cast<size_t>(info_log_length) + 1, 0);
        gl::get_program_info_log(gl_name, info_log_length, nullptr, &log[0]);
        log_program.error("Shader_stages linking failed:\n");
        log_program.error(log);
        log_program.error("\n");
        for (const auto& s : create_info.shaders)
        {
            string f_source = format(create_info.final_source(s));
            log_glsl.error("\n{}\n", f_source);
        }
        log_program.error("Shader_stages linking failed: \n");
        log_program.error(log);
        log_program.error("\n");
        return;
    }
    else
    {
        log_program.trace("Shader_stages linking succeeded: \n");
        for (const auto& s : create_info.shaders)
        {
            string f_source = format(create_info.final_source(s));
            log_glsl.trace("\n{}\n", f_source);
        }
        m_link_succeeded = true;
    }
}

void Shader_stages::Prototype::dump_reflection() const
{
    int gl_name = m_handle.gl_name();

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
    for (auto interface : program_interfaces)
    {
        if ((interface == gl::Program_interface::atomic_counter_buffer    ) ||
            (interface == gl::Program_interface::transform_feedback_buffer))
        {
            continue;
        }
        int interface_max_name_length{0};
        gl::get_program_interface_iv(gl_name,
                                     interface,
                                     gl::Program_interface_p_name::max_name_length,
                                     &interface_max_name_length);
        max_name_length = std::max(max_name_length, interface_max_name_length);
    }

    GLsizei name_length{0};
    std::vector<char> name_buffer;
    name_buffer.resize(static_cast<size_t>(max_name_length) + 1);

    for (auto interface : program_interfaces)
    {
        int active_resource_count{0};
        gl::get_program_interface_iv(gl_name,
                                     interface,
                                     gl::Program_interface_p_name::active_resources,
                                     &active_resource_count);

        if (active_resource_count == 0)
        {
            continue;

        }
        log_program.trace("{:<40} : {} resources\n", c_str(interface), active_resource_count);

        std::string name;
        for (int i = 0; i < active_resource_count; ++i)
        {
            if (interface != gl::Program_interface::atomic_counter_buffer)
            {
                std::fill(begin(name_buffer), end(name_buffer), '\0');
                gl::get_program_resource_name(gl_name,
                                              interface,
                                              i,
                                              max_name_length,
                                              &name_length,
                                              name_buffer.data());
                name = std::string(name_buffer.data(), name_length);
                log_program.trace("\t{:<40} : {}\n", i, name);
            }
            else
            {
                log_program.trace("\t{:<40} :\n", i);
            }

            gl::Program_resource_property property_num_active_variables = gl::Program_resource_property::num_active_variables;
            gl::Program_resource_property property_active_variables     = gl::Program_resource_property::active_variables;
            if (is_program_interface_allowed(property_num_active_variables, interface) &&
                is_program_interface_allowed(property_active_variables,     interface))
            {
                GLsizei length{0};
                GLint num_active_variables{0};
                gl::get_program_resource_iv(gl_name,
                                            interface,
                                            i,
                                            1,
                                            &property_num_active_variables,
                                            1,
                                            &length,
                                            &num_active_variables);
                log_program.trace("\t\t{:<40} = {}\n",
                                  c_str(property_num_active_variables),
                                  num_active_variables);
                if (num_active_variables > 0)
                {
                    std::vector<GLint> indices;
                    indices.resize(num_active_variables);
                    std::fill(begin(indices), end(indices), 0);
                    gl::get_program_resource_iv(gl_name,
                                                interface,
                                                i,
                                                1,
                                                &property_active_variables,
                                                num_active_variables,
                                                &length,
                                                indices.data());
                    log_program.trace("\t\t{:<40} = [ ", c_str(property_active_variables));
                    for (int i = 0; i < num_active_variables; ++i)
                    {
                        if (i > 0)
                        {
                            log_program.trace(", ");
                        }
                        auto member_interface_ = member_interface(interface);
                        if (member_interface_.has_value())
                        {
                            std::fill(begin(name_buffer), end(name_buffer), '\0');
                            gl::get_program_resource_name(gl_name,
                                                          member_interface_.value(),
                                                          indices[i],
                                                          max_name_length,
                                                          &name_length,
                                                          name_buffer.data());
                            name = std::string(name_buffer.data(), name_length);
                            log_program.trace("{} {}", indices[i], name);
                        }
                        else
                        {
                            log_program.trace("{}", indices[i]);
                        }
                    }
                    log_program.trace(" ]\n");
                }
            }

            // Query resource properties
            for (auto property : program_resource_properties)
            {
                if (!is_program_interface_allowed(property, interface))
                {
                    continue;
                }
                GLsizei length{0};
                int param{0};
                gl::get_program_resource_iv(gl_name,
                                            interface,
                                            i,
                                            1,
                                            &property,
                                            1,
                                            &length,
                                            &param);
                if (property != gl::Program_resource_property::type)
                {
                    log_program.trace("\t\t{:<40} = {}\n", c_str(property), param);
                }
                else
                {
                    log_program.trace("\t\t{:<40} = {}\n", c_str(property), gl::enum_string(param));
                }
            }
        }
    }

    int buffer_size = std::max(active_uniform_max_length, active_uniform_block_max_name_length);
    std::vector<char> buffer(static_cast<size_t>(buffer_size) + 1);

    if (transform_feedback_varyings > 0)
    {
        VERIFY(transform_feedback_varying_max_length > 0);
        string             buffer_(static_cast<size_t>(transform_feedback_varying_max_length) + 1, 0);
        GLsizei            length{0};
        GLsizei            size2{0};
        gl::Attribute_type type2;
        for (unsigned int i = 0; i < static_cast<unsigned int>(transform_feedback_varyings); ++i)
        {
            gl::get_transform_feedback_varying(gl_name,
                                               i,
                                               transform_feedback_varying_max_length,
                                               &length,
                                               &size2,
                                               &type2,
                                               &buffer_[0]);

            if (size2 > 1)
            {
                log_program.info("transform feedback varying {} {} {}[{}]\n", i, gl::c_str(type2), &buffer_[0], size2);
            }
            else
            {
                log_program.info("Transform feedback varying {} {} {}\n", i, gl::c_str(type2), &buffer_[0]);
            }
        }
    }

    // For each interface block,
    // ask GL driver locations of those uniforms, and place to uniform map for the program (prototype)
    /// for (const auto& i : create_info.interface_blocks)
    /// {
    ///     auto block = i.second;
    ///     m_uniform_map.resize(block->members().size());
    ///     for (auto& member : block->members())
    ///     {
    ///         std::stringstream ss;
    ///         ss << block->name() << "_" << member.name();
    ///         auto key = member.index_in_block();
    ///         if (key >= 0)
    ///         {
    ///             auto u_key = static_cast<unsigned int>(key);
    ///             auto uniform_name = ss.str();
    ///             if (m_uniform_map.size() < (u_key + 1))
    ///             {
    ///                 m_uniform_map.resize(u_key + 1);
    ///             }
    ///  ///             m_uniform_map[u_key] = gl::get_uniform_location(gl_name, uniform_name.c_str());
    ///         }
    ///     }
    /// }
}

} // namespace erhe::graphics
