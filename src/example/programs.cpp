#include "programs.hpp"
#include "mesh_memory.hpp"
#include "example_log.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/graphics/shader_monitor.hpp"
#include "erhe/gl/command_info.hpp"
#include "erhe/graphics/instance.hpp"
#include "erhe/graphics/sampler.hpp"
#include "erhe/renderer/camera_buffer.hpp"
#include "erhe/renderer/light_buffer.hpp"
#include "erhe/renderer/material_buffer.hpp"
#include "erhe/renderer/primitive_buffer.hpp"
#include "erhe/renderer/program_interface.hpp"
#include "erhe/toolkit/verify.hpp"

namespace example {

IPrograms::~IPrograms() noexcept = default;

class Programs_impl
    : public IPrograms
{
public:
    Programs_impl()
    {
        ERHE_VERIFY(g_programs == nullptr);
        g_programs = this;

        const erhe::application::Scoped_gl_context gl_context;

        nearest_sampler = std::make_unique<erhe::graphics::Sampler>(
            erhe::graphics::Sampler_create_info{
                .min_filter  = gl::Texture_min_filter::nearest,
                .mag_filter  = gl::Texture_mag_filter::nearest,
                .debug_label = "Programs nearest"
            }
        );

        linear_sampler = std::make_unique<erhe::graphics::Sampler>(
            erhe::graphics::Sampler_create_info{
                .min_filter  = gl::Texture_min_filter::linear,
                .mag_filter  = gl::Texture_mag_filter::linear,
                .debug_label = "Programs linear"
            }
        );

        linear_mipmap_linear_sampler = std::make_unique<erhe::graphics::Sampler>(
            erhe::graphics::Sampler_create_info{
                .min_filter  = gl::Texture_min_filter::linear_mipmap_linear,
                .mag_filter  = gl::Texture_mag_filter::linear,
                .debug_label = "Programs linear mipmap"
            }
        );

        if (!erhe::graphics::Instance::info.use_bindless_texture) {
            default_uniform_block = std::make_unique<erhe::graphics::Shader_resource>();

            default_uniform_block->add_sampler(
                "s_shadow",
                gl::Uniform_type::sampler_2d_array,
                shadow_texture_unit
            );
            default_uniform_block->add_sampler(
                "s_texture",
                gl::Uniform_type::sampler_2d,
                base_texture_unit,
                s_texture_unit_count
            );
        }
        const auto* p_default_uniform_block = erhe::graphics::Instance::info.use_bindless_texture
            ? nullptr
            : default_uniform_block.get();

        m_shader_path = std::filesystem::path("res") / std::filesystem::path("shaders");

        std::vector<Program_prototype> prototypes;

        prototypes.emplace_back(
            &standard,
            make_prototype(
                erhe::graphics::Shader_stages::Create_info{
                    .name                  = "standard",
                    .default_uniform_block = p_default_uniform_block,
                    .dump_interface        = true,
                    .dump_final_source     = true
                }
            )
        );
        //prototypes.emplace_back(&sky     , make_prototype(CI{ .name = "sky"     , .default_uniform_block = base_texture_default_uniform_block } ));

        for (auto& entry : prototypes) {
            entry.prototype->compile_shaders();
        }

        // Link programs
        for (auto& entry : prototypes) {
            if (!entry.prototype->link_program()) {
                entry.prototype.reset();
            }
        }

        for (auto& entry : prototypes) {
            if (entry.prototype) {
                *entry.program = make_program(*entry.prototype.get());
            }
        }

        g_programs = this;
    }

    ~Programs_impl() noexcept override
    {
        ERHE_VERIFY(g_programs == this);
        g_programs = nullptr;
    }

private:
    class Program_prototype
    {
    public:
        Program_prototype();

        Program_prototype(
            std::unique_ptr<erhe::graphics::Shader_stages>*             program,
            std::unique_ptr<erhe::graphics::Shader_stages::Prototype>&& prototype
        );

        std::unique_ptr<erhe::graphics::Shader_stages>*           program{nullptr};
        std::unique_ptr<erhe::graphics::Shader_stages::Prototype> prototype;
    };

    void queue(Program_prototype& program_prototype);

    [[nodiscard]] auto make_prototype(
        erhe::graphics::Shader_stages::Create_info create_info
    ) -> std::unique_ptr<erhe::graphics::Shader_stages::Prototype>;

    [[nodiscard]] auto make_program(
        erhe::graphics::Shader_stages::Prototype& prototype
    ) -> std::unique_ptr<erhe::graphics::Shader_stages>;

    std::filesystem::path m_shader_path;
};

using erhe::graphics::Shader_stages;

IPrograms* g_programs{nullptr};

Programs::Programs()
    : erhe::components::Component{c_type_name}
{
}

Programs::~Programs() noexcept
{
    ERHE_VERIFY(g_programs == nullptr);
}

void Programs::deinitialize_component()
{
    m_impl.reset();
}

void Programs::declare_required_components()
{
    require<erhe::application::Configuration      >();
    require<erhe::application::Gl_context_provider>();
    require<erhe::application::Shader_monitor     >();
    require<erhe::renderer::Program_interface     >();
    require<Mesh_memory>();
}

void Programs::initialize_component()
{
    m_impl = std::make_unique<Programs_impl>();
}

auto Programs_impl::make_prototype(
    erhe::graphics::Shader_stages::Create_info create_info
) -> std::unique_ptr<erhe::graphics::Shader_stages::Prototype>
{
    return erhe::renderer::g_program_interface->shader_resources->make_prototype(
        m_shader_path,
        create_info
    );
}

auto Programs_impl::make_program(
    erhe::graphics::Shader_stages::Prototype& prototype
) -> std::unique_ptr<erhe::graphics::Shader_stages>
{
    return erhe::renderer::g_program_interface->shader_resources->make_program(prototype);
}

Programs_impl::Program_prototype::Program_prototype() = default;

Programs_impl::Program_prototype::Program_prototype(
    std::unique_ptr<erhe::graphics::Shader_stages>*             program,
    std::unique_ptr<erhe::graphics::Shader_stages::Prototype>&& prototype
)
    : program  {program}
    , prototype{std::move(prototype)}
{
}

}
