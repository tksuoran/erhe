#pragma once

#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/gl_objects.hpp"

#if defined(ERHE_SPIRV)
#include "glslang/Public/ShaderLang.h"
namespace glslang {
    class TShader;
    class TProgram;
}
#include <unordered_map>
#endif

#include <filesystem>
#include <map>
#include <string>

namespace erhe::dataformat {
    class Vertex_format;
}

namespace erhe::graphics {

class Fragment_outputs;
class Shader_resource;
class Gl_shader;
class Vertex_input_state;

class Shader_stage_extension
{
public:
    gl::Shader_type shader_stage;
    std::string     extension;
};

class Shader_stage
{
public:
    // user-provided constructors are needed for vector::emplace_back()
    // Sadly, this disables using designated initializers
    Shader_stage(gl::Shader_type type, std::string_view source);
    Shader_stage(gl::Shader_type type, const std::filesystem::path& path);

    auto get_description() const -> std::string;

    gl::Shader_type                    type;
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

    auto get_description() const -> std::string;

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

class Shader_stages_prototype final
{
public:
    Shader_stages_prototype (Device& graphics_device, Shader_stages_create_info&& create_info);
    Shader_stages_prototype (Device& graphics_device, const Shader_stages_create_info& create_info);
    ~Shader_stages_prototype() noexcept = default;
    Shader_stages_prototype (const Shader_stages_prototype&) = delete;
    void operator=          (const Shader_stages_prototype&) = delete;
    Shader_stages_prototype (Shader_stages_prototype&&) = default;

    [[nodiscard]] auto name       () const -> const std::string&;
    [[nodiscard]] auto create_info() const -> const Shader_stages_create_info&;
    [[nodiscard]] auto is_valid   () -> bool;

    void compile_shaders();
    auto link_program   () -> bool;
    void dump_reflection() const;

    auto get_final_source(const Shader_stage& shader, std::optional<unsigned int> gl_name) -> std::string;

private:
    void post_link();
    void query_bindings();

#if defined(ERHE_SPIRV)
    auto compile_glslang     (const Shader_stage& shader) -> std::shared_ptr<glslang::TShader>;
    auto link_glslang_program() -> bool;
#endif

    [[nodiscard]] auto compile     (const Shader_stage& shader) -> Gl_shader;
    [[nodiscard]] auto post_compile(const Shader_stage& shader, Gl_shader& gl_shader) -> bool;

    static constexpr int state_init                       = 0;
    static constexpr int state_shader_compilation_started = 1;
    static constexpr int state_program_link_started       = 2;
    static constexpr int state_ready                      = 3;
    static constexpr int state_fail                       = 4;

    friend class Shader_stages;
    friend class Reloadable_shader_stages;
    Device&                                           m_graphics_device;
    Gl_program                                          m_handle;
    Shader_stages_create_info                           m_create_info;
    std::vector<Gl_shader>                              m_prelink_shaders;
    int                                                 m_state{state_init};
    Shader_resource                                     m_default_uniform_block;
    std::map<std::string, Shader_resource, std::less<>> m_resources;
    std::map<unsigned int, std::string>                 m_final_sources;
    std::vector<std::filesystem::path>                  m_paths;

#if defined(ERHE_SPIRV)
    std::vector<std::shared_ptr<glslang::TShader>>               m_glslang_shaders;
    std::unordered_map<::EShLanguage, std::vector<unsigned int>> m_spirv_shaders;
    std::shared_ptr<glslang::TProgram>                           m_glslang_program;
#endif
};

class Shader_stages
{
public:
    Shader_stages(Device& device, Shader_stages_prototype&& prototype);
    Shader_stages(Device& device, const std::string& non_functional_name);
    Shader_stages(Shader_stages&& old);
    Shader_stages& operator=(Shader_stages&& old);

    // Reloads program by consuming prototype
    void reload    (Shader_stages_prototype&& prototype);
    void invalidate();

    [[nodiscard]] auto name    () const -> const std::string&;
    [[nodiscard]] auto gl_name () const -> unsigned int;
    [[nodiscard]] auto is_valid() const -> bool;

private:
    Device&              m_device;
    Gl_program             m_handle;
    std::string            m_name;
    bool                   m_is_valid{false};
    std::vector<Gl_shader> m_attached_shaders;
};

class Reloadable_shader_stages
{
public:
    Reloadable_shader_stages(Device& device, const std::string& non_functional_name);
    Reloadable_shader_stages(Device& device, Shader_stages_prototype&& prototype);
    Reloadable_shader_stages(Device& device, const Shader_stages_create_info& create_info);
    Reloadable_shader_stages(Reloadable_shader_stages&&);
    Reloadable_shader_stages& operator=(Reloadable_shader_stages&&);

    Shader_stages_create_info create_info;
    Shader_stages             shader_stages;

private:
    [[nodiscard]] auto make_prototype(Device& graphics_device) -> Shader_stages_prototype;
};

class Shader_stages_hash
{
public:
    [[nodiscard]] auto operator()(const Shader_stages& program) const noexcept -> std::size_t
    {
        return static_cast<std::size_t>(program.gl_name());
    }
};

[[nodiscard]] auto operator==(const Shader_stages& lhs, const Shader_stages& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Shader_stages& lhs, const Shader_stages& rhs) noexcept -> bool;

class Shader_stages_tracker
{
public:
    void reset  ();
    void execute(const Shader_stages* state);

private:
    unsigned int m_last{0};
};

} // namespace erhe::graphics
