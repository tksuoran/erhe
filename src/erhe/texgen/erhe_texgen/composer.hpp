#pragma once

#include "erhe_texgen/shader_code.hpp"

#include <cstddef>
#include <string>

namespace erhe::texgen {

class Compose_node;

enum class Uniform_declaration_mode : unsigned int {
    plain_uniforms = 0, // assemble_fragment() emits "uniform float name = value;" declarations
    none           = 1  // the caller declares the uniforms itself (e.g. a std140 UBO block)
};

enum class Common_library_mode : unsigned int {
    when_referenced = 0, // prepend the common library only when the assembled text references it
    always          = 1,
    never           = 2
};

class Compose_options
{
public:
    // Prepended to every textual uniform reference in generated code (not to
    // the uniform's registered name). Set to e.g. "params." when the editor
    // places the parameters in a UBO instance block. Default: empty (plain
    // uniforms referenced by name).
    std::string uniform_accessor_prefix{};

    // Name of the generated entry point function ("void <name>()").
    std::string function_name{"main"};

    // Variable the final vec4 result is assigned to.
    std::string output_variable_name{"out_color"};

    // GLSL expression initializing "vec2 uv" at the top of the entry point.
    std::string uv_source_expression{"v_texcoord"};

    Uniform_declaration_mode uniform_declaration_mode{Uniform_declaration_mode::plain_uniforms};
    Common_library_mode      common_library_mode     {Common_library_mode::when_referenced};
};

// Walks a Compose_node DAG from a sink node upstream and produces a single
// Shader_code (compose()), then assembles it into a complete fragment-shader
// body string (assemble_fragment()). Stateless apart from the options; each
// compose() call uses a fresh variant-tracking context.
//
// Composition semantics are ported from Material Maker's
// gen_shader.gd::_get_shader_code / gen_context.gd (MIT license).
class Composer
{
public:
    Composer() = default;
    explicit Composer(Compose_options options);

    [[nodiscard]] auto get_options() const -> const Compose_options&;

    // Composes the subgraph feeding sink_node's output_index into one
    // Shader_code (globals, uniforms, defs, inline code, output expression).
    [[nodiscard]] auto compose(const Compose_node& sink_node, std::size_t output_index) const -> Shader_code;

    // Produces the final fragment-shader body:
    //   [common library] + deduped globals + [uniform declarations] + defs +
    //   "void main() { vec2 uv = ...; <code>; <output_variable> = <vec4 expr>; }"
    // The output expression is converted to vec4 based on the output type
    // (f -> vec4(vec3(v), 1.0), rgb -> vec4(v, 1.0), rgba unchanged).
    [[nodiscard]] auto assemble_fragment(const Shader_code& shader_code) const -> std::string;

private:
    Compose_options m_options{};
};

} // namespace erhe::texgen
