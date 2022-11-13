#pragma once

#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/gl_objects.hpp"

#include <filesystem>
#include <map>
#include <optional>

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

// Shader program
class Shader_stages
{
public:

    // Contains all necessary information for creating a shader program
    class Create_info
    {
    public:
        class Shader_stage
        {
        public:
            // user-provided constructors are needed for vector::emplace_back()
            // Sadly, this disables using designated initializers
            Shader_stage(
                gl::Shader_type        type,
                const std::string_view source
            )
                : type  {type}
                , source{source}
            {
            }

            Shader_stage(
                const gl::Shader_type       type,
                const std::filesystem::path path
            )
                : type{type}
                , path{path}
            {
            }

            gl::Shader_type       type;
            std::string           source;
            std::filesystem::path path;
        };

        // Adds #version, #extensions, #defines, fragment outputs, uniform blocks, samplers,
        // and source (possibly read from file).
        [[nodiscard]] auto final_source           (const Shader_stage& shader) const -> std::string;
        [[nodiscard]] auto attributes_source      () const -> std::string;
        [[nodiscard]] auto fragment_outputs_source() const -> std::string;
        [[nodiscard]] auto struct_types_source    () const -> std::string;
        [[nodiscard]] auto interface_blocks_source() const -> std::string;
        [[nodiscard]] auto interface_source       () const -> std::string;

        void add_interface_block(gsl::not_null<const Shader_resource*> uniform_block);

        std::string                                      name;

        std::vector<std::string>                         pragmas                       {};
        std::vector<std::pair<std::string, std::string>> defines                       {};
        std::vector<Shader_stage_extension>              extensions                    {};
        std::vector<const Shader_resource*>              interface_blocks              {};
        std::vector<const Shader_resource*>              struct_types                  {};
        const Vertex_attribute_mappings*                 vertex_attribute_mappings     {nullptr};
        const Fragment_outputs*                          fragment_outputs              {nullptr};
        const Shader_resource*                           default_uniform_block         {nullptr}; // contains sampler uniforms
        std::vector<std::string>                         transform_feedback_varyings   {};
        gl::Transform_feedback_buffer_mode               transform_feedback_buffer_mode{gl::Transform_feedback_buffer_mode::separate_attribs};
        std::vector<Shader_stage>                        shaders                       {};
        bool                                             dump_reflection               {false};
        bool                                             dump_interface                {false};
        bool                                             dump_final_source             {false};
    };

    class Prototype final
    {
    public:
        explicit Prototype(const Create_info& create_info);
        ~Prototype        () noexcept = default;
        Prototype         (const Prototype&) = delete;
        void operator=    (const Prototype&) = delete;

        [[nodiscard]] auto name       () const -> const std::string&;
        [[nodiscard]] auto create_info() const -> const Create_info&;
        [[nodiscard]] auto is_valid   () -> bool;

        auto link_program() -> bool;

        void compile_shaders();
        void dump_reflection() const;

    private:
        void post_link();

        [[nodiscard]] auto compile(
            const Create_info::Shader_stage& shader
        ) -> Gl_shader;

        [[nodiscard]] auto post_compile(
            const Shader_stages::Create_info::Shader_stage& shader,
            Gl_shader&                                      gl_shader
        ) -> bool;

        friend class Shader_stages;

        static constexpr int state_init                       = 0;
        static constexpr int state_shader_compilation_started = 1;
        static constexpr int state_program_link_started       = 2;
        static constexpr int state_ready                      = 3;
        static constexpr int state_fail                       = 4;

        Create_info            m_create_info;
        Gl_program             m_handle;
        std::vector<Gl_shader> m_shaders;
        int                    m_state{state_init};
        Shader_resource        m_default_uniform_block;
        std::map<std::string, Shader_resource, std::less<>> m_resources;
    };

    // Creates Shader_stages by consuming prototype
    explicit Shader_stages(Prototype&& prototype);

    // Reloads program by consuming prototype
    void reload(Prototype&& prototype);

    [[nodiscard]] auto name() const -> const std::string&
    {
        return m_name;
    }

    [[nodiscard]] auto gl_name() const -> unsigned int;

private:
    [[nodiscard]] static auto format(const std::string& source) -> std::string;

    std::string            m_name;
    Gl_program             m_handle;
    std::vector<Gl_shader> m_attached_shaders;
};

class Shader_stages_hash
{
public:
    [[nodiscard]] auto operator()(const Shader_stages& program) const noexcept -> size_t
    {
        return static_cast<size_t>(program.gl_name());
    }
};

[[nodiscard]] auto operator==(
    const Shader_stages& lhs,
    const Shader_stages& rhs
) noexcept -> bool;

[[nodiscard]] auto operator!=(
    const Shader_stages& lhs,
    const Shader_stages& rhs
) noexcept -> bool;


class Shader_stages_tracker
{
public:
    void reset  ();
    void execute(const Shader_stages* state);

private:
    unsigned int m_last{0};
};

} // namespace erhe::graphics
