#include "renderers/programs.hpp"

#include "editor_log.hpp"

#include "renderers/program_interface.hpp"
#include "renderers/material_buffer.hpp"
#include "renderers/light_buffer.hpp"
#include "renderers/camera_buffer.hpp"
#include "renderers/primitive_buffer.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/graphics/shader_monitor.hpp"
#include "erhe/gl/command_info.hpp"
#include "erhe/graphics/instance.hpp"
#include "erhe/graphics/sampler.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

namespace editor {

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
            gl::Texture_min_filter::nearest,
            gl::Texture_mag_filter::nearest
        );

        linear_sampler = std::make_unique<erhe::graphics::Sampler>(
            gl::Texture_min_filter::linear,
            gl::Texture_mag_filter::linear
        );

        linear_mipmap_linear_sampler = std::make_unique<erhe::graphics::Sampler>(
            gl::Texture_min_filter::linear_mipmap_linear,
            gl::Texture_mag_filter::linear
        );

        if (!erhe::graphics::Instance::info.use_bindless_texture) {
            shadow_map_default_uniform_block = std::make_unique<erhe::graphics::Shader_resource>();
            textured_default_uniform_block   = std::make_unique<erhe::graphics::Shader_resource>();

            shadow_map_default_uniform_block->add_sampler(
                "s_shadow",
                gl::Uniform_type::sampler_2d_array,
                shadow_texture_unit
            );
            textured_default_uniform_block->add_sampler(
                "s_texture",
                gl::Uniform_type::sampler_2d,
                base_texture_unit,
                s_texture_unit_count
            );
        }
        const auto* shadow_default_uniform_block = erhe::graphics::Instance::info.use_bindless_texture
            ? nullptr
            : shadow_map_default_uniform_block.get();
        const auto* base_texture_default_uniform_block = erhe::graphics::Instance::info.use_bindless_texture
            ? nullptr
            : textured_default_uniform_block.get();

        m_shader_path = std::filesystem::path("res") / std::filesystem::path("shaders");

        // Not available on Dell laptop.
        //standard      = make_program("standard", {}, {{gl::Shader_type::fragment_shader, "GL_NV_fragment_shader_barycentric"}});

        using CI = erhe::graphics::Shader_stages::Create_info;

        std::vector<Program_prototype> prototypes;

        prototypes.emplace_back(&standard                , make_prototype(CI{ .name = "standard"                , .default_uniform_block = shadow_default_uniform_block, .dump_interface = true } ));
        prototypes.emplace_back(&anisotropic_slope       , make_prototype(CI{ .name = "anisotropic_slope"       , .default_uniform_block = shadow_default_uniform_block} ));
        prototypes.emplace_back(&anisotropic_engine_ready, make_prototype(CI{ .name = "anisotropic_engine_ready", .default_uniform_block = shadow_default_uniform_block} ));
        prototypes.emplace_back(&circular_brushed_metal  , make_prototype(CI{ .name = "circular_brushed_metal"  , .default_uniform_block = shadow_default_uniform_block} ));
        prototypes.emplace_back(&brdf_slice              , make_prototype(CI{ .name = "brdf_slice"              , .default_uniform_block = shadow_default_uniform_block} ));
        prototypes.emplace_back(&brush                   , make_prototype(CI{ .name = "brush"                   , .default_uniform_block = shadow_default_uniform_block} ));
        prototypes.emplace_back(&textured                , make_prototype(CI{ .name = "textured"                , .default_uniform_block = base_texture_default_uniform_block } ));
        prototypes.emplace_back(&sky                     , make_prototype(CI{ .name = "sky"                     , .default_uniform_block = base_texture_default_uniform_block } ));
        prototypes.emplace_back(&wide_lines_draw_color   , make_prototype(CI{ .name = "wide_lines"              , .defines = { std::pair<std::string, std::string>{"ERHE_USE_DRAW_COLOR",   "1"}}}));
        prototypes.emplace_back(&wide_lines_vertex_color , make_prototype(CI{ .name = "wide_lines"              , .defines = { std::pair<std::string, std::string>{"ERHE_USE_VERTEX_COLOR", "1"}}}));
        prototypes.emplace_back(&points                  , make_prototype(CI{ .name = "points" } ));
        prototypes.emplace_back(&depth                   , make_prototype(CI{ .name = "depth"  } ));
        prototypes.emplace_back(&id                      , make_prototype(CI{ .name = "id"     } ));
        prototypes.emplace_back(&tool                    , make_prototype(CI{ .name = "tool"   } ));
        prototypes.emplace_back(&debug_depth             , make_prototype(CI{ .name = "visualize_depth", .default_uniform_block = shadow_default_uniform_block } ));
        prototypes.emplace_back(&debug_normal            , make_prototype(CI{ .name = "standard_debug", .defines = { std::pair<std::string, std::string>{"ERHE_DEBUG_NORMAL",             "1"}}, .default_uniform_block = shadow_default_uniform_block } ));
        prototypes.emplace_back(&debug_tangent           , make_prototype(CI{ .name = "standard_debug", .defines = { std::pair<std::string, std::string>{"ERHE_DEBUG_TANGENT",            "1"}}, .default_uniform_block = shadow_default_uniform_block } ));
        prototypes.emplace_back(&debug_bitangent         , make_prototype(CI{ .name = "standard_debug", .defines = { std::pair<std::string, std::string>{"ERHE_DEBUG_BITANGENT",          "1"}}, .default_uniform_block = shadow_default_uniform_block } ));
        prototypes.emplace_back(&debug_texcoord          , make_prototype(CI{ .name = "standard_debug", .defines = { std::pair<std::string, std::string>{"ERHE_DEBUG_TEXCOORD",           "1"}}, .default_uniform_block = shadow_default_uniform_block } ));
        prototypes.emplace_back(&debug_vertex_color_rgb  , make_prototype(CI{ .name = "standard_debug", .defines = { std::pair<std::string, std::string>{"ERHE_DEBUG_VERTEX_COLOR_RGB",   "1"}}, .default_uniform_block = shadow_default_uniform_block } ));
        prototypes.emplace_back(&debug_vertex_color_alpha, make_prototype(CI{ .name = "standard_debug", .defines = { std::pair<std::string, std::string>{"ERHE_DEBUG_VERTEX_COLOR_ALPHA", "1"}}, .default_uniform_block = shadow_default_uniform_block } ));
        prototypes.emplace_back(&debug_omega_o           , make_prototype(CI{ .name = "standard_debug", .defines = { std::pair<std::string, std::string>{"ERHE_DEBUG_OMEGA_O",            "1"}}, .default_uniform_block = shadow_default_uniform_block } ));
        prototypes.emplace_back(&debug_omega_i           , make_prototype(CI{ .name = "standard_debug", .defines = { std::pair<std::string, std::string>{"ERHE_DEBUG_OMEGA_I",            "1"}}, .default_uniform_block = shadow_default_uniform_block } ));
        prototypes.emplace_back(&debug_omega_g           , make_prototype(CI{ .name = "standard_debug", .defines = { std::pair<std::string, std::string>{"ERHE_DEBUG_OMEGA_G",            "1"}}, .default_uniform_block = shadow_default_uniform_block } ));
        prototypes.emplace_back(&debug_misc              , make_prototype(CI{ .name = "standard_debug", .defines = { std::pair<std::string, std::string>{"ERHE_DEBUG_MISC",               "1"}}, .default_uniform_block = shadow_default_uniform_block } ));

        // Compile shaders
        {
            ERHE_PROFILE_SCOPE("compile shaders");

            for (auto& entry : prototypes) {
                entry.prototype->compile_shaders();
            }
        }

        // Link programs
        {
            ERHE_PROFILE_SCOPE("link programs");

            for (auto& entry : prototypes) {
                if (!entry.prototype->link_program()) {
                    entry.prototype.reset();
                }
            }
        }

        {
            ERHE_PROFILE_SCOPE("post link");

            for (auto& entry : prototypes) {
                if (entry.prototype) {
                    *entry.program = make_program(*entry.prototype.get());
                }
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
    require<Program_interface>();
}

void Programs::initialize_component()
{
    m_impl = std::make_unique<Programs_impl>();
}

auto Programs_impl::make_prototype(
    erhe::graphics::Shader_stages::Create_info create_info
) -> std::unique_ptr<erhe::graphics::Shader_stages::Prototype>
{
    ERHE_PROFILE_FUNCTION();

    SPDLOG_LOGGER_TRACE(log_programs, "Programs::make_program({})", create_info.name);
    SPDLOG_LOGGER_TRACE(log_programs, "current directory is {}", std::filesystem::current_path().string());

    const std::filesystem::path vs_path = m_shader_path / std::filesystem::path(create_info.name + ".vert");
    const std::filesystem::path gs_path = m_shader_path / std::filesystem::path(create_info.name + ".geom");
    const std::filesystem::path fs_path = m_shader_path / std::filesystem::path(create_info.name + ".frag");

    const bool vs_exists = std::filesystem::exists(vs_path);
    const bool gs_exists = std::filesystem::exists(gs_path);
    const bool fs_exists = std::filesystem::exists(fs_path);

    const auto& shader_resources = *g_program_interface->shader_resources.get();

    create_info.vertex_attribute_mappings = &shader_resources.attribute_mappings,
    create_info.fragment_outputs          = &shader_resources.fragment_outputs,
    create_info.add_interface_block(&shader_resources.material_interface.material_block);
    create_info.add_interface_block(&shader_resources.light_interface.light_block);
    create_info.add_interface_block(&shader_resources.light_interface.light_control_block);
    create_info.add_interface_block(&shader_resources.camera_interface.camera_block);
    create_info.add_interface_block(&shader_resources.primitive_interface.primitive_block);
    create_info.struct_types.push_back(&shader_resources.material_interface.material_struct);
    create_info.struct_types.push_back(&shader_resources.light_interface.light_struct);
    create_info.struct_types.push_back(&shader_resources.camera_interface.camera_struct);
    create_info.struct_types.push_back(&shader_resources.primitive_interface.primitive_struct);

    if (erhe::graphics::Instance::info.gl_version < 430) {
        ERHE_VERIFY(gl::is_extension_supported(gl::Extension::Extension_GL_ARB_shader_storage_buffer_object));
        create_info.extensions.push_back({gl::Shader_type::vertex_shader,   "GL_ARB_shader_storage_buffer_object"});
        create_info.extensions.push_back({gl::Shader_type::geometry_shader, "GL_ARB_shader_storage_buffer_object"});
        create_info.extensions.push_back({gl::Shader_type::fragment_shader, "GL_ARB_shader_storage_buffer_object"});
    }
    if (erhe::graphics::Instance::info.gl_version < 460) {
        ERHE_VERIFY(gl::is_extension_supported(gl::Extension::Extension_GL_ARB_shader_draw_parameters));
        create_info.extensions.push_back({gl::Shader_type::vertex_shader,   "GL_ARB_shader_draw_parameters"});
        create_info.extensions.push_back({gl::Shader_type::geometry_shader, "GL_ARB_shader_draw_parameters"});
        create_info.defines.push_back({"gl_DrawID", "gl_DrawIDARB"});
    }

    const auto& config = *erhe::application::g_configuration;
    // TODO if (config->shadow_renderer.enabled)
    {
        create_info.defines.emplace_back("ERHE_SHADOW_MAPS", "1");
    }
    if (config.graphics.simpler_shaders) {
        create_info.defines.emplace_back("ERHE_SIMPLER_SHADERS", "1");
    }

    if (erhe::graphics::Instance::info.use_bindless_texture) {
        create_info.defines.emplace_back("ERHE_BINDLESS_TEXTURE", "1");
        create_info.extensions.push_back({gl::Shader_type::fragment_shader, "GL_ARB_bindless_texture"});
    }

    //if (config->shader_monitor.enabled)
    //{
    //    create_info.pragmas.push_back("optimize(off)");
    //}

    if (vs_exists) {
        create_info.shaders.emplace_back(gl::Shader_type::vertex_shader,   vs_path);
    }
    if (gs_exists) {
        create_info.shaders.emplace_back(gl::Shader_type::geometry_shader, gs_path);
    }
    if (fs_exists) {
        create_info.shaders.emplace_back(gl::Shader_type::fragment_shader, fs_path);
    }

    return std::make_unique<Shader_stages::Prototype>(create_info);
}

auto Programs_impl::make_program(
    erhe::graphics::Shader_stages::Prototype& prototype
) -> std::unique_ptr<erhe::graphics::Shader_stages>
{
    ERHE_PROFILE_FUNCTION();

    if (!prototype.is_valid()) {
        log_programs->error("current directory is {}", std::filesystem::current_path().string());
        log_programs->error("Compiling shader program {} failed", prototype.name());
        return {};
    }

    auto p = std::make_unique<Shader_stages>(std::move(prototype));

    if (erhe::application::g_shader_monitor != nullptr) {
        erhe::application::g_shader_monitor->add(prototype.create_info(), p.get());
    }

    return p;
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
