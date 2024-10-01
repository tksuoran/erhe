#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/vertex_attribute_mappings.hpp"
#include "erhe_file/file.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

auto glsl_token(Glsl_type attribute_type) -> const char*
{
    switch (attribute_type) {
        //using enum gl::Attribute_type;
        case Glsl_type::float_:            return "float    ";
        case Glsl_type::float_vec2:        return "vec2     ";
        case Glsl_type::float_vec3:        return "vec3     ";
        case Glsl_type::float_vec4:        return "vec4     ";
        case Glsl_type::bool_:             return "bool     ";
        case Glsl_type::int_:              return "int      ";
        case Glsl_type::int_vec2:          return "ivec2    ";
        case Glsl_type::int_vec3:          return "ivec3    ";
        case Glsl_type::int_vec4:          return "ivec4    ";
        case Glsl_type::unsigned_int:      return "uint     ";
        case Glsl_type::unsigned_int_vec2: return "uvec2    ";
        case Glsl_type::unsigned_int_vec3: return "uvec3    ";
        case Glsl_type::unsigned_int_vec4: return "uvec4    ";
        case Glsl_type::unsigned_int64_arb:return "uint64_t ";
        case Glsl_type::float_mat_2x2:     return "mat2     ";
        case Glsl_type::float_mat_3x3:     return "mat3     ";
        case Glsl_type::float_mat_4x4:     return "mat4     ";
        default: {
            ERHE_FATAL("TODO");
        }
    }
}

auto Shader_stages_create_info::attributes_source() const -> std::string
{
    std::stringstream sb;

    // Apply attrib location bindings
    if (
        (vertex_attribute_mappings != nullptr) &&
        (vertex_attribute_mappings->mappings.size() > 0)
    ) {
        sb << "// Attributes\n";
        for (const auto& mapping : vertex_attribute_mappings->mappings) {
            sb << "in layout(location = " << mapping.layout_location << ") ";
            sb << glsl_token(mapping.shader_type) << " ";
            sb << mapping.name,
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

auto Shader_stages_create_info::final_source(Instance& graphics_instance, const Shader_stage& shader, std::optional<unsigned int> gl_name) const -> std::string
{
    ERHE_PROFILE_FUNCTION();

    std::stringstream sb;
    sb << "#version " << graphics_instance.info.glsl_version << " core\n\n";

    if (!shader.path.empty()) {
        sb << "// Path: " << shader.path << "\n";
    }
    sb << "// Stage: " << gl::c_str(shader.type) << "\n";
    if (gl_name.has_value()) {
        sb << "// Shader name: " << gl_name.value() << "\n";
    }

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
    } else if (!shader.path.empty()) {
        auto source = erhe::file::read("Shader_stages_create_info::final_source", shader.path);
        sb << (source.has_value() ? "\n// Loaded from: " : "\n// Source load failed from: ");
        sb << shader.path;
        sb << "\n\n";
        if (source.has_value()) {
            sb << source.value();
        }
    }

    return sb.str(); // shaders.emplace_back(type, source, sb.str());
}

void Shader_stages_create_info::add_interface_block(const Shader_resource* interface_block)
{
    ERHE_VERIFY(interface_block != nullptr);
    interface_blocks.push_back(interface_block);
}

} // namespace erhe::graphics
