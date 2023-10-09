#pragma once

#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/gl_objects.hpp"

#include <filesystem>
#include <map>
#include <string>

namespace glslang {
    class TShader;
};

namespace erhe::graphics
{

class Fragment_outputs;
class Shader_resource;
class Vertex_attribute_mappings;
class Gl_shader;

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
    Shader_stage(gl::Shader_type type, const std::string_view source);
    Shader_stage(gl::Shader_type type, const std::filesystem::path path);

    gl::Shader_type       type;
    std::string           source;
    std::filesystem::path path;
};

class Shader_stages_create_info
{
public:
    // Adds #version, #extensions, #defines, fragment outputs, uniform blocks, samplers,
    // and source (possibly read from file).
    [[nodiscard]] auto final_source           (Instance& graphics_instance, const Shader_stage& shader) const -> std::string;
    [[nodiscard]] auto attributes_source      () const -> std::string;
    [[nodiscard]] auto fragment_outputs_source() const -> std::string;
    [[nodiscard]] auto struct_types_source    () const -> std::string;
    [[nodiscard]] auto interface_blocks_source() const -> std::string;
    [[nodiscard]] auto interface_source       () const -> std::string;

    void add_interface_block(gsl::not_null<const Shader_resource*> uniform_block);

    std::string                                      name;
    std::vector<std::string>                         pragmas                  {};
    std::vector<std::pair<std::string, std::string>> defines                  {};
    std::vector<Shader_stage_extension>              extensions               {};
    std::vector<const Shader_resource*>              struct_types             {};
    std::vector<const Shader_resource*>              interface_blocks         {};
    const Vertex_attribute_mappings*                 vertex_attribute_mappings{nullptr};
    const Fragment_outputs*                          fragment_outputs         {nullptr};
    const Shader_resource*                           default_uniform_block    {nullptr}; // contains sampler uniforms
    std::vector<Shader_stage>                        shaders                  {};
    bool                                             dump_reflection          {false};
    bool                                             dump_interface           {false};
    bool                                             dump_final_source        {false};
    bool                                             build                    {false};
};

class Shader_stages_prototype final
{
public:
    Shader_stages_prototype(
        Instance&                   graphics_instance,
        Shader_stages_create_info&& create_info
    );
    Shader_stages_prototype(
        Instance&                        graphics_instance,
        const Shader_stages_create_info& create_info
    );
    ~Shader_stages_prototype() noexcept = default;
    Shader_stages_prototype (const Shader_stages_prototype&) = delete;
    void operator=          (const Shader_stages_prototype&) = delete;
    Shader_stages_prototype (Shader_stages_prototype&&) = default;

    [[nodiscard]] auto name       () const -> const std::string&;
    [[nodiscard]] auto create_info() const -> const Shader_stages_create_info&;
    [[nodiscard]] auto is_valid   () -> bool;

    auto link_program   () -> bool;
    void compile_shaders();
    void dump_reflection() const;

private:
    void post_link();

    [[nodiscard]] auto compile        (const Shader_stage& shader) -> Gl_shader;
    [[nodiscard]] auto compile_glslang(const Shader_stage& shader) -> std::shared_ptr<glslang::TShader>;
    [[nodiscard]] auto post_compile   (const Shader_stage& shader, Gl_shader& gl_shader) -> bool;

    friend class Shader_stages;

    static constexpr int state_init                       = 0;
    static constexpr int state_shader_compilation_started = 1;
    static constexpr int state_program_link_started       = 2;
    static constexpr int state_ready                      = 3;
    static constexpr int state_fail                       = 4;

    Instance&                 m_graphics_instance;
    Shader_stages_create_info m_create_info;
    Gl_program                m_handle;
    std::vector<Gl_shader>    m_prelink_shaders;
    int                       m_state{state_init};
    Shader_resource           m_default_uniform_block;
    std::map<std::string, Shader_resource, std::less<>> m_resources;
};

class Shader_stages
{
public:
    explicit Shader_stages(Shader_stages_prototype&& prototype);
    explicit Shader_stages(const std::string& non_functional_name);

    // Reloads program by consuming prototype
    void reload    (Shader_stages_prototype&& prototype);
    void invalidate();

    [[nodiscard]] auto name    () const -> const std::string&;
    [[nodiscard]] auto gl_name () const -> unsigned int;
    [[nodiscard]] auto is_valid() const -> bool;

private:
    std::string            m_name;
    Gl_program             m_handle;
    bool                   m_is_valid{false};
    std::vector<Gl_shader> m_attached_shaders;
};

class Reloadable_shader_stages
{
public:
    explicit Reloadable_shader_stages(
        const std::string& non_functional_name
    );
    explicit Reloadable_shader_stages(
        Shader_stages_prototype&& prototype
    );
    Reloadable_shader_stages(
        Instance&                        graphics_instance,
        const Shader_stages_create_info& create_info
    );
    Reloadable_shader_stages(Reloadable_shader_stages&&);
    Reloadable_shader_stages& operator=(Reloadable_shader_stages&&);

    Shader_stages_create_info create_info;
    Shader_stages             shader_stages;

private:
    [[nodiscard]] auto make_prototype(Instance& graphics_instance) -> Shader_stages_prototype;
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
