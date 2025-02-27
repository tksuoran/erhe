#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_graphics/glsl_file_loader.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_file/file.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <map>
#include <sstream>
#include <string>

namespace erhe::graphics {

auto glsl_token(const gl::Attribute_type attribute_type) -> const char*
{
    switch (attribute_type) {
        //using enum gl::Attribute_type;
        case gl::Attribute_type::float_:            return "float    ";
        case gl::Attribute_type::float_vec2:        return "vec2     ";
        case gl::Attribute_type::float_vec3:        return "vec3     ";
        case gl::Attribute_type::float_vec4:        return "vec4     ";
        case gl::Attribute_type::bool_:             return "bool     ";
        case gl::Attribute_type::int_:              return "int      ";
        case gl::Attribute_type::int_vec2:          return "ivec2    ";
        case gl::Attribute_type::int_vec3:          return "ivec3    ";
        case gl::Attribute_type::int_vec4:          return "ivec4    ";
        case gl::Attribute_type::unsigned_int:      return "uint     ";
        case gl::Attribute_type::unsigned_int_vec2: return "uvec2    ";
        case gl::Attribute_type::unsigned_int_vec3: return "uvec3    ";
        case gl::Attribute_type::unsigned_int_vec4: return "uvec4    ";
        case gl::Attribute_type::unsigned_int64_arb:return "uint64_t ";
        case gl::Attribute_type::float_mat2:        return "mat2     ";
        case gl::Attribute_type::float_mat3:        return "mat3     ";
        case gl::Attribute_type::float_mat4:        return "mat4     ";
        default: {
            ERHE_FATAL("TODO");
        }
    }
}

auto Shader_stages_create_info::attributes_source() const -> std::string
{
    std::stringstream sb;

    // Apply attrib location bindings
    if (vertex_format != nullptr) {
        Vertex_input_state_data vertex_input = Vertex_input_state_data::make(*vertex_format);
        sb << "// Attributes\n";
        for (const auto& attribute : vertex_input.attributes) {
            sb << "in layout(location = " << attribute.layout_location << ") ";
            sb << glsl_token(attribute.gl_attribute_type) << " ";
            sb << attribute.name,
            sb << ";\n";
        }
        sb << "\n";
    }

    return sb.str();
}

auto Shader_stages_create_info::fragment_outputs_source() const -> std::string
{
    std::stringstream sb;

    sb << "// Fragment outputs\n";
    if (fragment_outputs != nullptr) {
        sb << fragment_outputs->source();
    }
    sb << "\n";

    return sb.str();
}

auto Shader_stages_create_info::struct_types_source() const -> std::string
{
    std::stringstream sb;

    if (struct_types.size() > 0) {
        sb << "// Struct types\n";
        for (const auto& struct_type : struct_types) {
            ERHE_VERIFY(struct_type != nullptr);
            sb << struct_type->source();
            sb << "\n";
        }
        sb << "\n";
    }

    return sb.str();
}

auto Shader_stages_create_info::interface_blocks_source() const -> std::string
{
    std::stringstream sb;

    if (interface_blocks.size() > 0) {
        sb << "// Blocks\n";
        for (const auto* block : interface_blocks) {
            sb << block->source();
            sb << "\n";
        }
        sb << "\n";
    }

    return sb.str();
}

auto Shader_stages_create_info::interface_source() const -> std::string
{
    std::stringstream sb;
    sb << attributes_source      ();
    sb << fragment_outputs_source();
    sb << struct_types_source    ();
    sb << interface_blocks_source();
    return sb.str();
}

auto Shader_stages_create_info::final_source(
    Instance&                           graphics_instance,
    const Shader_stage&                 shader,
    std::vector<std::filesystem::path>* paths,
    std::optional<unsigned int>         gl_name
) const -> std::string
{
    ERHE_PROFILE_FUNCTION();

    std::stringstream sb;
    sb << "#version " << graphics_instance.info.glsl_version << " core\n\n";
    sb << "// " << gl::c_str(shader.type);
    if (gl_name.has_value()) {
        sb << " " << gl_name.value();
    }
    bool first_path = true;
    for (const std::filesystem::path& path : shader.paths) {
        if (path.empty()) {
            continue;
        }
        if (!first_path) {
            sb << ",";
        }
        sb << " " << path;
        first_path = false;
    }
    sb << "\n";

    if (!pragmas.empty()) {
        sb << "// Pragmas\n";
        for (const auto& i : pragmas) {
            sb << "#pragma " << i << "\n";
        }
        sb << "\n";
    }

    if (!extensions.empty()) {
        sb << "// Extensions\n";
        for (const auto& i : extensions) {
            if (i.shader_stage == shader.type) {
                sb << "#extension " << i.extension << " : require\n";
            }
        }
        sb << "\n";
    }

    if (shader.type == gl::Shader_type::vertex_shader) {
        sb << attributes_source();
    } else if (shader.type == gl::Shader_type::fragment_shader) {
        sb << fragment_outputs_source();
    }

    if (defines.size() > 0) {
        sb << "// Defines\n";
        for (const auto& i : defines) {
            sb << "#define " << i.first << " " << i.second << '\n';
        }
        sb << "\n";
    }

    sb << struct_types_source();
    sb << interface_blocks_source();

    if (default_uniform_block != nullptr) {
        sb << "// Default uniform block\n";
        sb << default_uniform_block->source();
        sb << "\n";
    }

    if (!shader.source.empty()) {
        sb << shader.source;
        if (shader.source.back() != '\n') {
            sb << '\n';
        }
    }
    if (!shader.paths.empty()) {
        Glsl_file_loader loader;
        ERHE_VERIFY(shader.paths.size() == 1);
        std::string source = loader.read_shader_source_file(shader.paths.front());
        if (paths != nullptr) {
            for (const std::filesystem::path& path : loader.get_file_paths()) {
                paths->push_back(path);
            }
        }
        sb << source;
    }

    return sb.str();
}

void Shader_stages_create_info::add_interface_block(const Shader_resource* interface_block)
{
    ERHE_VERIFY(interface_block != nullptr);
    interface_blocks.push_back(interface_block);
}

auto Shader_stages_create_info::get_description() const -> std::string
{
    std::stringstream ss;
    ss << name;
    for (const Shader_stage& shader_stage : shaders) {
        ss << " stage: " << shader_stage.get_description();
    }
    return ss.str();
}

} // namespace erhe::graphics
