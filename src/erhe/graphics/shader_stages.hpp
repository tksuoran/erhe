#pragma once

#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/gl_objects.hpp"

#include <filesystem>
#include <map>
#include <optional>

namespace erhe::graphics
{

class Fragment_outputs;
class Shader_monitor;
class Shader_resource;
class Vertex_attribute_mappings;
class Gl_shader;

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
            Shader_stage(const gl::Shader_type type, std::string_view source)
                : type  {type}
                , source{source}
            {
            }

            Shader_stage(const gl::Shader_type type, std::filesystem::path path)
                : type{type}
                , path{std::move(path)}
            {
            }

            gl::Shader_type       type;
            std::string           source;
            std::filesystem::path path;
        };

        Create_info(const std::string_view           name,
                    const Shader_resource*           default_uniform_block, // containing sampler uniforms
                    const Vertex_attribute_mappings* vertex_attribute_mappings,
                    const Fragment_outputs*          fragment_outputs);

        // Adds #version, #defines, fragment outputs, uniform blocks, samplers,
        // and source (possibly read from file).
        auto final_source(const Shader_stage& shader) const
        -> std::string;

        void add_interface_block(gsl::not_null<const Shader_resource*> uniform_block);

        std::string                                         name;

        // https://stackoverflow.com/questions/35525777/use-of-string-view-for-map-lookup
        std::vector<std::pair<std::string, std::string>>    defines;
        std::map<std::string, gsl::not_null<const Shader_resource*>, std::less<>>
                                                            interface_blocks;
        std::vector<const Shader_resource*>                 struct_types;
        const Vertex_attribute_mappings*                    vertex_attribute_mappings{nullptr};
        const Fragment_outputs*                             fragment_outputs         {nullptr};
        const Shader_resource*                              default_uniform_block    {nullptr}; // contains sampler uniforms
        std::vector<std::string>                            transform_feedback_varyings;
        gl::Transform_feedback_buffer_mode                  transform_feedback_buffer_mode{gl::Transform_feedback_buffer_mode::separate_attribs};
        std::vector<Shader_stage>                           shaders;
    };

    class Prototype final
    {
    public:
        explicit Prototype(const Create_info& create_info);
        ~Prototype        () = default;
        Prototype         (const Prototype&) = delete;
        void operator=    (const Prototype&) = delete;

        auto is_valid() const
        -> bool
        {
            return m_link_succeeded;
        }

        void dump_reflection() const;

    private:
        static auto try_compile_shader(const Shader_stages::Create_info&               create_info,
                                       const Shader_stages::Create_info::Shader_stage& shader)
        -> std::optional<Gl_shader>;

        friend class Shader_stages;

        std::string            m_name;
        Gl_program             m_handle;
        std::vector<Gl_shader> m_attached_shaders;
        bool                   m_link_succeeded{false};

        Shader_resource        m_default_uniform_block;
        std::map<std::string, Shader_resource, std::less<>> m_resources;
    };

    // Creates Shader_stages by consuming prototype
    explicit Shader_stages(Prototype&& prototype);

    // Reloads program by consuming prototype
    void reload(Prototype&& prototype);

    auto name() const
    -> const std::string&
    {
        return m_name;
    }

    auto gl_name() const
    -> unsigned int;

private:
    static auto format(const std::string& source)
    -> std::string;

    std::string            m_name;
    Gl_program             m_handle;
    std::vector<Gl_shader> m_attached_shaders;
};

class Shader_stages_hash
{
public:
    auto operator()(const Shader_stages& program) const noexcept
    -> size_t
    {
        return static_cast<size_t>(program.gl_name());
    }
};

auto operator==(const Shader_stages& lhs, const Shader_stages& rhs) noexcept
-> bool;

auto operator!=(const Shader_stages& lhs, const Shader_stages& rhs) noexcept
-> bool;


class Shader_stages_tracker
{
public:
    void reset()
    {
        gl::use_program(0);
        m_last = nullptr;
    }

    void execute(const Shader_stages* state)
    {
        if (m_last == state)
        {
            return;
        }
        unsigned int name = (state != nullptr) ? state->gl_name() : 0;
        gl::use_program(name);
        m_last = state;
    }

private:
    const Shader_stages* m_last{nullptr};
};

} // namespace erhe::graphics
