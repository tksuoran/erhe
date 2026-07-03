#include "erhe_texgen/shader_code.hpp"

#include "erhe_verify/verify.hpp"

namespace erhe::texgen {

void Shader_code::add_global(const std::string_view global_source)
{
    for (const std::string& existing_global : m_globals) {
        if (existing_global == global_source) {
            return;
        }
    }
    m_globals.emplace_back(global_source);
}

void Shader_code::add_globals(const Shader_code& other)
{
    for (const std::string& global_source : other.m_globals) {
        add_global(global_source);
    }
}

void Shader_code::add_uniform(const Uniform& uniform)
{
    for (const Uniform& existing_uniform : m_uniforms) {
        if (existing_uniform.name == uniform.name) {
            // A same-named uniform must be the same uniform (re-registration
            // from another sampling of the node). A kind mismatch means two
            // different uniforms collided on one name - e.g. duplicate node
            // ids - and silently dropping it would hide that bug.
            ERHE_VERIFY(existing_uniform.kind == uniform.kind);
            return;
        }
    }
    m_uniforms.push_back(uniform);
}

void Shader_code::add_uniforms(const Shader_code& other)
{
    for (const Uniform& uniform : other.m_uniforms) {
        add_uniform(uniform);
    }
}

void Shader_code::add_sampler(const Sampler_binding& sampler)
{
    for (const Sampler_binding& existing_sampler : m_samplers) {
        if (existing_sampler.name == sampler.name) {
            // A same-named sampler must be the same binding (re-registration
            // from another sampling of the node). A binding-index mismatch
            // means two different sampler sources collided on one name - e.g.
            // duplicate node ids - which must not be hidden.
            ERHE_VERIFY(existing_sampler.binding == sampler.binding);
            return;
        }
    }
    m_samplers.push_back(sampler);
}

void Shader_code::add_samplers(const Shader_code& other)
{
    for (const Sampler_binding& sampler : other.m_samplers) {
        add_sampler(sampler);
    }
}

void Shader_code::append_defs(const std::string_view text)
{
    m_defs.append(text);
}

void Shader_code::append_code(const std::string_view text)
{
    m_code.append(text);
}

void Shader_code::set_output(std::string expression, const Value_type type)
{
    m_output_expression = std::move(expression);
    m_output_type = type;
}

auto Shader_code::get_globals() const -> const std::vector<std::string>&
{
    return m_globals;
}

auto Shader_code::get_uniforms() const -> const std::vector<Uniform>&
{
    return m_uniforms;
}

auto Shader_code::get_samplers() const -> const std::vector<Sampler_binding>&
{
    return m_samplers;
}

auto Shader_code::get_defs() const -> const std::string&
{
    return m_defs;
}

auto Shader_code::get_code() const -> const std::string&
{
    return m_code;
}

auto Shader_code::get_output_expression() const -> const std::string&
{
    return m_output_expression;
}

auto Shader_code::get_output_type() const -> Value_type
{
    return m_output_type;
}

} // namespace erhe::texgen
