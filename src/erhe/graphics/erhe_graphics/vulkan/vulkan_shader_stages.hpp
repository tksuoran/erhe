#pragma once

#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/glsl_to_spirv.hpp"

#include "glslang/Public/ShaderLang.h"
namespace glslang {
    class TShader;
    class TProgram;
}
#include <unordered_map>

#include <filesystem>
#include <map>
#include <string>

namespace erhe::graphics {

class Device;
class Gl_shader;

enum class Shader_build_state : int {
    init                       = 0,
    shader_compilation_started = 1,
    program_link_started       = 2,
    ready                      = 3,
    fail                       = 4
};

// NOTE: There is no Shader_stages_prototype (non-impl)
class Shader_stages_prototype_impl final
{
public:
    Shader_stages_prototype_impl (Device& device, Shader_stages_create_info&& create_info);
    Shader_stages_prototype_impl (Device& device, const Shader_stages_create_info& create_info);
    ~Shader_stages_prototype_impl() noexcept = default;
    Shader_stages_prototype_impl (const Shader_stages_prototype_impl&) = delete;
    void operator=               (const Shader_stages_prototype_impl&) = delete;
    Shader_stages_prototype_impl (Shader_stages_prototype_impl&&) = default;

    [[nodiscard]] auto name       () const -> const std::string&;
    [[nodiscard]] auto create_info() const -> const Shader_stages_create_info&;
    [[nodiscard]] auto is_valid   () -> bool;

    void compile_shaders();
    auto link_program   () -> bool;

    auto get_final_source(const Shader_stage& shader, std::optional<unsigned int> gl_name) -> std::string;

private:
    friend class Shader_stages_impl;
    friend class Reloadable_shader_stages;

    Device&                                             m_device;
    Shader_stages_create_info                           m_create_info;
    Shader_build_state                                  m_state{Shader_build_state::init};
    Shader_resource                                     m_default_uniform_block;
    std::map<std::string, Shader_resource, std::less<>> m_resources;
    std::map<unsigned int, std::string>                 m_final_sources;
    std::vector<std::filesystem::path>                  m_paths;

    Glslang_shader_stages                               m_glslang_shader_stages;
};

class Shader_stages_impl
{
public:
    Shader_stages_impl(Device& device, Shader_stages_prototype&& prototype);
    Shader_stages_impl(Device& device, const std::string& non_functional_name);
    Shader_stages_impl(Shader_stages_impl&& old);
    Shader_stages_impl& operator=(Shader_stages_impl&& old);

    // Reloads program by consuming prototype
    void reload    (Shader_stages_prototype&& prototype);
    void invalidate();
    [[nodiscard]] auto name    () const -> const std::string&;
    [[nodiscard]] auto is_valid() const -> bool;

private:
    Device&     m_device;
    std::string m_name;
    bool        m_is_valid{false};
};

class Shader_stages_impl_hash
{
public:
    [[nodiscard]] auto operator()(const Shader_stages_impl& shader_stages) const noexcept -> std::size_t
    {
        static_cast<void>(shader_stages);
        return 0;
    }
};

[[nodiscard]] auto operator==(const Shader_stages_impl& lhs, const Shader_stages_impl& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Shader_stages_impl& lhs, const Shader_stages_impl& rhs) noexcept -> bool;

class Shader_stages_tracker
{
public:
    void reset  ();
    void execute(const Shader_stages* state);

private:
    unsigned int m_last{0};
};

} // namespace erhe::graphics
