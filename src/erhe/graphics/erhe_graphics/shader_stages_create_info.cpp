#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/vertex_attribute_mappings.hpp"
#include "erhe_file/file.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics
{

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
            sb << c_str(mapping.shader_type) << " ";
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

auto Shader_stages_create_info::final_source(const Shader_stage& shader) const -> std::string
{
    std::stringstream sb;
    sb << "#version 450 core\n\n";

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

    if (shader.type == igl::ShaderStage::Vertex) {
        sb << attributes_source();
    } else if (shader.type == igl::ShaderStage::Fragment) {
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

void Shader_stages_create_info::add_interface_block(
    gsl::not_null<const Shader_resource*> interface_block
)
{
    interface_blocks.push_back(interface_block);
}

} // namespace erhe::graphics
