#include "erhe/graphics/fragment_outputs.hpp"
#include "erhe/graphics/log.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/toolkit/file.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::graphics
{

auto glsl_token(gl::Attribute_type type)
-> const char*
{
    switch (type)
    {
        case gl::Attribute_type::int_:              return "int   ";
        case gl::Attribute_type::int_vec2:          return "ivec2 ";
        case gl::Attribute_type::int_vec3:          return "ivec3 ";
        case gl::Attribute_type::int_vec4:          return "ivec4 ";
        case gl::Attribute_type::unsigned_int:      return "uint  ";
        case gl::Attribute_type::unsigned_int_vec2: return "uvec2 ";
        case gl::Attribute_type::unsigned_int_vec3: return "uvec3 ";
        case gl::Attribute_type::unsigned_int_vec4: return "uvec4 ";
        case gl::Attribute_type::float_:            return "float ";
        case gl::Attribute_type::float_vec2:        return "vec2  ";
        case gl::Attribute_type::float_vec3:        return "vec3  ";
        case gl::Attribute_type::float_vec4:        return "vec4  ";
        case gl::Attribute_type::float_mat4:        return "mat4  ";
        default:
        {
            FATAL("TODO");
        }
    }
}
Shader_stages::Create_info::Create_info(std::string_view           name,
                                        Shader_resource*           default_uniform_block, // containing samplers
                                        Vertex_attribute_mappings* vertex_attribute_mappings,
                                        Fragment_outputs*          fragment_outputs)
    : name                     {name}
    , vertex_attribute_mappings{vertex_attribute_mappings}
    , fragment_outputs         {fragment_outputs}
    , default_uniform_block    {default_uniform_block}
{
}

auto Shader_stages::Create_info::final_source(const Shader_stage& shader) const
-> std::string
{
    std::stringstream sb;
    sb << "#version 460 core\n\n";

    if (shader.type == gl::Shader_type::vertex_shader)
    {
        // Apply attrib location bindings
        if (vertex_attribute_mappings->mappings.size() > 0)
        {
            sb << "// Attributes\n";
            for (auto mapping : vertex_attribute_mappings->mappings)
            {
                sb << "in layout(location = " << mapping->layout_location << ") ";
                sb << glsl_token(mapping->shader_type) << " ";
                sb << mapping->name,
                sb << ";\n";
            }
            sb << "\n";
        }
    }
    else if (shader.type == gl::Shader_type::fragment_shader)
    {
        sb << "// Fragment outputs\n";
        if (fragment_outputs != nullptr)
        {
            sb << fragment_outputs->source();
        }
        sb << "\n";
    }

    if (defines.size() > 0)
    {
        sb << "// Defines\n";
        for (const auto& i : defines)
        {
            sb << "#define " << i.first << " " << i.second << '\n';
        }
        sb << "\n";
    }

    if (struct_types.size() > 0)
    {
        sb << "// Struct types\n";
        for (const auto& struct_type : struct_types)
        {
            VERIFY(struct_type != nullptr);
            sb << struct_type->source();
        }
        sb << "\n";
    }

    if (interface_blocks.size() > 0)
    {
        sb << "// Blocks\n";
        for (const auto& i : interface_blocks)
        {
            auto block = i.second;
            sb << block->source();
        }
        sb << "\n";
    }

    if (default_uniform_block != nullptr)
    {
        sb << "// Default uniform block\n";
        sb << default_uniform_block->source();
        sb << "\n";
    }

    if (!shader.source.empty())
    {
        sb << shader.source;
    }
    else if (!shader.path.empty())
    {
        if (!std::filesystem::exists(shader.path))
        {
            log_program.warn("Cannot load shader from non-existing file '{}'", shader.path.string());
        }
        if (!std::filesystem::is_regular_file(shader.path))
        {
            log_program.warn("Cannot load shader from non-regular file '{}'", shader.path.string());
        }
        if (std::filesystem::is_empty(shader.path))
        {
            log_program.warn("Cannot load shader from empty path");
        }

        auto source = erhe::toolkit::read(shader.path);
        if (source.has_value())
        {
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

void Shader_stages::Create_info::add_interface_block(gsl::not_null<Shader_resource*> interface_block)
{
    interface_blocks.emplace(interface_block->name() + "_block", interface_block);
}

} // namespace erhe::graphics
