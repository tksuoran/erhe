#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/vertex_attribute_mappings.hpp"
#include "erhe_file/file.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <map>
#include <sstream>
#include <string>

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
    if ((vertex_attribute_mappings != nullptr) && (vertex_attribute_mappings->mappings.size() > 0)) {
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

class Glsl_file_loader
{
public:
    std::vector<std::filesystem::path> source_string_index_to_path;
    std::vector<std::filesystem::path> include_stack;

    [[nodiscard]] auto process_includes(std::size_t source_string_index, const std::string& source) -> std::string
    {
        static_cast<void>(source_string_index);
        ERHE_PROFILE_FUNCTION();

        const char* head = source.c_str();

        std::stringstream sb;

        bool line_start_white_space_only = true;
        int line{1};
        for (;;) {
            char c = *head;
            if (c == 0) {
                break;
            }

            if (c == '\r' || c == '\n') {
                line += (c == '\n') ? 1 : 0;
                line_start_white_space_only = true;
                sb << c;
                ++head;
                continue;
            }

            if (line_start_white_space_only && (c == '#')) {
                const char* include_start = head;
                const char* include_token = "#include";
                const std::size_t include_token_length = strlen(include_token);
                const int diff = strncmp(head, include_token, include_token_length);
                if (diff == 0) {
                    const char* p = head + include_token_length;
                    if (isspace(*p)) {
                        while (isspace(*p)) {
                            ++p;
                        }
                        bool open_quotation     = *p == '"';
                        bool open_angle_bracket = *p == '<';
                        if (open_quotation || open_angle_bracket) {
                            ++p;
                            const char* path_start = p;
                            std::size_t path_length = 0;
                            while (
                                (*p != '\0') && 
                                (*p != '\r') && 
                                (*p != '\n') &&
                                (*p != '"') &&
                                (*p != '>')
                            ) {
                                ++p;
                                ++path_length;
                            }
                            bool close_quotation = open_quotation && (*p == '"');
                            bool close_angle_bracket = open_angle_bracket && (*p == '>');
                            if (close_quotation || close_angle_bracket) {
                                std::string filename{path_start, path_length};
                                ERHE_VERIFY(!source_string_index_to_path.empty());
                                const std::filesystem::path first = source_string_index_to_path.front();
                                const std::filesystem::path directory = first.parent_path();
                                const std::filesystem::path path = directory / filename;

                                // Log include operation
                                std::stringstream lss;
                                for (std::size_t i = 1, end = include_stack.size(); i < end; ++i) {
                                    lss << "    ";
                                }
                                lss << fmt::format("{} includes {}", include_stack.back().string(), path.string());
                                log_glsl->trace(lss.str());

                                // Place the include directive in comments
                                sb << "//";
                                for (const char* q = include_start; (*q != '\0') && (*q != '\n'); ++q) {
                                    if (*q != '\r') {
                                        sb << *q;
                                    }
                                }
                                sb << "\n";

                                const std::string included_source = read_shader_source_file(path);
                                sb << included_source;
                                if (included_source.empty() || included_source.back() != '\n') {
                                    sb << '\n';
                                }
                                sb << "#line " << line << ' ' << source_string_index << '\n';

                                // Drop rest of the line
                                while ((*p != '\0') && (*p != '\n')) {
                                    ++p;
                                }
                                if (*p == '\n') {
                                    ++p;
                                }

                                // "start" a new line after include
                                ++line;
                                line_start_white_space_only = true;
                                head = p;
                                continue;
                            }
                        }
                    }
                }
            }

            if (line_start_white_space_only && !isspace(c)) {
                line_start_white_space_only = false;
            }

            sb << c;
            ++head;
        }
        return sb.str();
    }

    [[nodiscard]] auto read_shader_source_file(const std::filesystem::path& path) -> std::string
    {
        auto i = std::find(include_stack.begin(), include_stack.end(), path);
        if (i != include_stack.end()) {
            log_glsl->warn("#include cycle for {}", path.string());
            return {};
        }
        if (path.empty()) {
            return {};
        }
        auto source = erhe::file::read("Shader_stages_create_info::final_source", path);
        if (!source.has_value()) {
            return fmt::format("// Source load failed from: {}", path.string());
        }

        include_stack.push_back(path);

        std::size_t source_string_index = 1 + source_string_index_to_path.size();
        source_string_index_to_path.push_back(path);
        std::string processed_source = process_includes(source_string_index, source.value());
        std::stringstream ss;
        ss << "#line 1 " << source_string_index << " // " << path.string() << '\n';
        ss << processed_source;

        include_stack.pop_back();

        return ss.str();
    }
};

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
            for (const std::filesystem::path& path : loader.source_string_index_to_path) {
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
