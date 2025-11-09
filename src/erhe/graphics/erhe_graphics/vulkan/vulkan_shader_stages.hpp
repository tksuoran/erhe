#pragma once

#include "erhe_graphics/shader_stages.hpp"

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
    void dump_reflection() const;

    auto get_final_source(
        const Shader_stage&         shader,
        std::optional<unsigned int> gl_name
    ) -> std::string;

private:
    auto compile_glslang     (const Shader_stage& shader) -> std::shared_ptr<glslang::TShader>;
    auto link_glslang_program() -> bool;

    static constexpr int state_init                       = 0;
    static constexpr int state_shader_compilation_started = 1;
    static constexpr int state_program_link_started       = 2;
    static constexpr int state_ready                      = 3;
    static constexpr int state_fail                       = 4;

    friend class Shader_stages_impl;
    friend class Reloadable_shader_stages;

    Device&                                             m_device;
    Shader_stages_create_info                           m_create_info;
    int                                                 m_state{state_init};
    Shader_resource                                     m_default_uniform_block;
    std::map<std::string, Shader_resource, std::less<>> m_resources;
    std::map<unsigned int, std::string>                 m_final_sources;
    std::vector<std::filesystem::path>                  m_paths;

    std::vector<std::shared_ptr<glslang::TShader>>               m_glslang_shaders;
    std::unordered_map<::EShLanguage, std::vector<unsigned int>> m_spirv_shaders;
    std::shared_ptr<glslang::TProgram>                           m_glslang_program;
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
        return static_cast<std::size_t>(shader_stages.gl_name());
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
