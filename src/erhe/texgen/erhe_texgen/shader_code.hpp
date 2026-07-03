#pragma once

#include "erhe_texgen/value_type.hpp"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::texgen {

enum class Uniform_kind : unsigned int {
    float_uniform = 0, // GLSL float - scalar parameters and per-node seeds
    vec4_uniform  = 1  // GLSL vec4  - color parameters
};

// One live-editable shader parameter produced by composition.
// The ordered uniform list is the contract for later phases: it is
// sufficient to declare a UBO block (or plain uniforms) and to upload the
// current values without recompiling the shader.
class Uniform
{
public:
    std::string          name {};
    Uniform_kind         kind {Uniform_kind::float_uniform};
    std::array<float, 4> value{0.0f, 0.0f, 0.0f, 0.0f}; // float uniforms use value[0] only
};

// Accumulated result of composing a node graph into shader source fragments.
// Ported from Material Maker's ShaderCode (gen_base.gd, MIT license).
class Shader_code
{
public:
    // Adds a global (function library) string; exact duplicates are dropped.
    void add_global(std::string_view global_source);

    // Merges all globals from another Shader_code (deduplicated).
    void add_globals(const Shader_code& other);

    // Adds a uniform; a uniform with an already-registered name is dropped.
    void add_uniform(const Uniform& uniform);

    // Merges all uniforms from another Shader_code (deduplicated by name).
    void add_uniforms(const Shader_code& other);

    // Appends to the definitions section (emitted input helper functions).
    void append_defs(std::string_view text);

    // Appends inline statements that run inside the shader entry point.
    void append_code(std::string_view text);

    // Sets the final output expression (a GLSL expression valid after the
    // inline code has run) and its value type.
    void set_output(std::string expression, Value_type type);

    [[nodiscard]] auto get_globals          () const -> const std::vector<std::string>&;
    [[nodiscard]] auto get_uniforms         () const -> const std::vector<Uniform>&;
    [[nodiscard]] auto get_defs             () const -> const std::string&;
    [[nodiscard]] auto get_code             () const -> const std::string&;
    [[nodiscard]] auto get_output_expression() const -> const std::string&;
    [[nodiscard]] auto get_output_type      () const -> Value_type;

private:
    std::vector<std::string> m_globals;           // de-duplicated by exact content, registration order
    std::vector<Uniform>     m_uniforms;          // de-duplicated by name, registration order
    std::string              m_defs;              // helper function definitions
    std::string              m_code;              // inline statements
    std::string              m_output_expression; // expression yielding the output value
    Value_type               m_output_type{Value_type::rgba};
};

} // namespace erhe::texgen
