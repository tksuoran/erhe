#pragma once

#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/enums.hpp"

#include <filesystem>
#include <map>
#include <string>

namespace glslang { class TShader; }
namespace erhe::dataformat { class Vertex_format; }

namespace erhe::graphics {

class Device_impl;
class Fragment_outputs;
class Shader_resource;
class Vertex_input_state;

class Shader_stage_extension
{
public:
    Shader_type shader_stage;
    std::string extension;
};

class Shader_stage
{
public:
    // user-provided constructors are needed for vector::emplace_back()
    // Sadly, this disables using designated initializers
    Shader_stage(Shader_type type, std::string_view source);
    Shader_stage(Shader_type type, const std::filesystem::path& path);

    [[nodiscard]] auto get_description() const -> std::string;

    Shader_type                        type;
    std::string                        source;
    std::vector<std::filesystem::path> paths;
};

class Shader_stages_create_info
{
public:
    // Adds #version, #extensions, #defines, fragment outputs, uniform blocks, samplers,
    // and source (possibly read from file).
    [[nodiscard]] auto final_source(
        Device&                             graphics_device,
        const Shader_stage&                 shader,
        std::vector<std::filesystem::path>* paths,
        std::optional<unsigned int>         gl_name = {}
    ) const -> std::string;

    [[nodiscard]] auto attributes_source      () const -> std::string;
    [[nodiscard]] auto fragment_outputs_source() const -> std::string;
    [[nodiscard]] auto struct_types_source    () const -> std::string;
    [[nodiscard]] auto interface_blocks_source() const -> std::string;
    [[nodiscard]] auto interface_source       () const -> std::string;

    void add_interface_block(const Shader_resource* uniform_block);

    [[nodiscard]] auto get_description() const -> std::string;

    std::string                                      name;
    std::vector<std::string>                         pragmas              {};
    std::vector<std::pair<std::string, std::string>> defines              {};
    std::vector<Shader_stage_extension>              extensions           {};
    std::vector<const Shader_resource*>              struct_types         {};
    std::vector<const Shader_resource*>              interface_blocks     {};
    const Fragment_outputs*                          fragment_outputs     {nullptr};
    const erhe::dataformat::Vertex_format*           vertex_format        {nullptr};
    const Shader_resource*                           default_uniform_block{nullptr}; // contains sampler uniforms
    std::vector<Shader_stage>                        shaders              {};
    bool                                             dump_reflection      {false};
    bool                                             dump_interface       {false};
    bool                                             dump_final_source    {false};
    bool                                             build                {false};
};

class Shader_stages_prototype_impl;
class Shader_stages_prototype final
{
public:
    Shader_stages_prototype (Device& device, Shader_stages_create_info&& create_info);
    Shader_stages_prototype (Device& device, const Shader_stages_create_info& create_info);
    ~Shader_stages_prototype() noexcept;
    Shader_stages_prototype (const Shader_stages_prototype&) = delete;
    void operator=          (const Shader_stages_prototype&) = delete;
    Shader_stages_prototype (Shader_stages_prototype&& old) noexcept;

    void compile_shaders() const;
    auto link_program   () const -> bool;

#if defined(ERHE_SPIRV)
    auto compile_glslang     (Device& device, const Shader_stage& shader) -> std::shared_ptr<glslang::TShader>;
    auto link_glslang_program() -> bool;
#endif

    [[nodiscard]] auto name            () const -> const std::string&;
    [[nodiscard]] auto create_info     () const -> const Shader_stages_create_info&;
    [[nodiscard]] auto is_valid        () const -> bool;
    [[nodiscard]] auto get_final_source(const Shader_stage& shader, std::optional<unsigned int> gl_name) const -> std::string;
    [[nodiscard]] auto get_impl        () -> Shader_stages_prototype_impl&;
    [[nodiscard]] auto get_impl        () const -> const Shader_stages_prototype_impl&;

private:
    std::unique_ptr<Shader_stages_prototype_impl> m_impl;
};

class Shader_stages_impl;
class Shader_stages final
{
public:
    Shader_stages(Device& device, Shader_stages_prototype&& prototype);
    Shader_stages(Device& device, const std::string& non_functional_name);
    ~Shader_stages() noexcept;
    Shader_stages(Shader_stages&&) noexcept;
    Shader_stages& operator=(Shader_stages&&) noexcept;

    // Reloads program by consuming prototype
    void reload    (Shader_stages_prototype&& prototype) const;
    void invalidate() const;

    [[nodiscard]] auto name    () const -> const std::string&;
    [[nodiscard]] auto is_valid() const -> bool;
    [[nodiscard]] auto get_impl() -> Shader_stages_impl&;
    [[nodiscard]] auto get_impl() const -> const Shader_stages_impl&;

private:
    std::unique_ptr<Shader_stages_impl> m_impl;
};

class Reloadable_shader_stages
{
public:
    Reloadable_shader_stages(Device& device, const std::string& non_functional_name);
    Reloadable_shader_stages(Device& device, Shader_stages_prototype&& prototype);
    Reloadable_shader_stages(Device& device, const Shader_stages_create_info& create_info);
    Reloadable_shader_stages(Reloadable_shader_stages&& old) noexcept;
    Reloadable_shader_stages& operator=(Reloadable_shader_stages&& old) noexcept;

    Shader_stages_create_info create_info;
    Shader_stages             shader_stages;

private:
    [[nodiscard]] auto make_prototype(Device& graphics_device) const -> Shader_stages_prototype;
};

// class Shader_stages_impl_hash
// {
// public:
//     [[nodiscard]] auto operator()(const Shader_stages_impl& shader_stages) const noexcept -> std::size_t
//     {
//         return static_cast<std::size_t>(shader_stages.gl_name());
//     }
// };

// [[nodiscard]] auto operator==(const Shader_stages& lhs, const Shader_stages& rhs) noexcept -> bool;
// [[nodiscard]] auto operator!=(const Shader_stages& lhs, const Shader_stages& rhs) noexcept -> bool;

} // namespace erhe::graphics
