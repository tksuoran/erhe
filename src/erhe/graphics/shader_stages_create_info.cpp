#include "erhe/graphics/fragment_outputs.hpp"
#include "erhe/graphics/graphics_log.hpp"
#include "erhe/graphics/instance.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/toolkit/file.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::graphics
{

auto glsl_token(gl::Attribute_type type) -> const char*
{
    switch (type) {
        //using enum gl::Attribute_type;
        case gl::Attribute_type::int_:              return "int      ";
        case gl::Attribute_type::int_vec2:          return "ivec2    ";
        case gl::Attribute_type::int_vec3:          return "ivec3    ";
        case gl::Attribute_type::int_vec4:          return "ivec4    ";
        case gl::Attribute_type::unsigned_int:      return "uint     ";
        case gl::Attribute_type::unsigned_int_vec2: return "uvec2    ";
        case gl::Attribute_type::unsigned_int_vec3: return "uvec3    ";
        case gl::Attribute_type::unsigned_int_vec4: return "uvec4    ";
        case gl::Attribute_type::unsigned_int64_arb:return "uint64_t ";
        case gl::Attribute_type::float_:            return "float    ";
        case gl::Attribute_type::float_vec2:        return "vec2     ";
        case gl::Attribute_type::float_vec3:        return "vec3     ";
        case gl::Attribute_type::float_vec4:        return "vec4     ";
        case gl::Attribute_type::float_mat4:        return "mat4     ";
        default: {
            ERHE_FATAL("TODO");
        }
    }
}

auto Shader_stages::Create_info::attributes_source() const -> std::string
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

auto Shader_stages::Create_info::fragment_outputs_source() const -> std::string
{
    std::stringstream sb;

    sb << "// Fragment outputs\n";
    if (fragment_outputs != nullptr) {
        sb << fragment_outputs->source();
    }
    sb << "\n";

    return sb.str();
}

auto Shader_stages::Create_info::struct_types_source() const -> std::string
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

auto Shader_stages::Create_info::interface_blocks_source() const -> std::string
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

auto Shader_stages::Create_info::interface_source() const -> std::string
{
    std::stringstream sb;
    sb << attributes_source      ();
    sb << fragment_outputs_source();
    sb << struct_types_source    ();
    sb << interface_blocks_source();
    return sb.str();
}

auto Shader_stages::Create_info::final_source(
    const Shader_stage& shader
) const -> std::string
{
    std::stringstream sb;
    sb << "#version " << Instance::info.glsl_version << " core\n\n";

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
        std::error_code error_code;
        const bool exists = std::filesystem::exists(shader.path, error_code);
        if (error_code) {
            log_program->warn("Shader: std::filesystem::exists('{}') returned error code {}", shader.path.string(), error_code.value());
        }
        if (!exists) {
            log_program->warn("Shader: Cannot load from non-existing file '{}'", shader.path.string());
        }
        const bool is_regular_file = std::filesystem::is_regular_file(shader.path, error_code);
        if (error_code) {
            log_program->warn("Shader: std::filesystem::is_regular_file('{}') returned error code {}", shader.path.string(), error_code.value());
        }
        if (!is_regular_file) {
            log_program->warn("Shader: Source file '{}' is not regular file", shader.path.string());
        }
        const bool is_empty = std::filesystem::is_empty(shader.path, error_code);
        if (error_code) {
            log_program->warn("Shader: std::filesystem::is_empty('{}') returned error code {}", shader.path.string(), error_code.value());
        }
        if (is_empty) {
            log_program->warn("Shader: Source file '{}' is empty", shader.path.string());
        }

        auto source = erhe::toolkit::read(shader.path);
        if (source.has_value()) {
            sb << "\n// Loaded from: ";
            sb << shader.path;
            sb << "\n\n";
            sb << source.value();
        } else {
            sb << "\n// Source load failed";
        }
    }

    return sb.str(); // shaders.emplace_back(type, source, sb.str());
}

void Shader_stages::Create_info::add_interface_block(
    gsl::not_null<const Shader_resource*> interface_block
)
{
    interface_blocks.push_back(interface_block);
}

} // namespace erhe::graphics
