#pragma once

#include "erhe_graphics/shader_stages.hpp"

#include <string>

namespace erhe::graphics {

class Device;

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

    [[nodiscard]] auto get_final_source(const Shader_stage& shader, std::optional<unsigned int> gl_name) -> std::string;

private:
    friend class Shader_stages_impl;
    friend class Reloadable_shader_stages;

    Device&                   m_device;
    Shader_stages_create_info m_create_info;
    bool                      m_is_valid{true};
};

class Shader_stages_impl
{
public:
    Shader_stages_impl(Device& device, Shader_stages_prototype&& prototype);
    Shader_stages_impl(Device& device, const std::string& non_functional_name);
    Shader_stages_impl(Shader_stages_impl&& old) noexcept;
    Shader_stages_impl& operator=(Shader_stages_impl&& old) noexcept;

    // Reloads program by consuming prototype
    void reload    (Shader_stages_prototype&& prototype);
    void invalidate();
    [[nodiscard]] auto name    () const -> const std::string&;
    [[nodiscard]] auto gl_name () const -> unsigned int;
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
    [[nodiscard]] auto get_draw_id_uniform_location() const -> int;

private:
    unsigned int m_last{0};
};

} // namespace erhe::graphics
