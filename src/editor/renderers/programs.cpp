#include "renderers/programs.hpp"
#include "graphics/gl_context_provider.hpp"
#include "graphics/shader_monitor.hpp"
#include "log.hpp"
#include "renderers/program_interface.hpp"

#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/sampler.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

namespace editor {

using erhe::graphics::Shader_stages;

Programs::Programs()
    : erhe::components::Component{c_name}
{
}

Programs::~Programs() = default;

void Programs::connect()
{
    require<Gl_context_provider>();

    m_program_interface = require<Program_interface>();
    m_shader_monitor    = require<Shader_monitor>();
}

void Programs::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    erhe::log::Indenter indenter;

    Scoped_gl_context gl_context{Component::get<Gl_context_provider>()};

    nearest_sampler = std::make_unique<erhe::graphics::Sampler>(
        gl::Texture_min_filter::nearest,
        gl::Texture_mag_filter::nearest
    );

    linear_sampler = std::make_unique<erhe::graphics::Sampler>(
        gl::Texture_min_filter::linear,
        gl::Texture_mag_filter::linear
    );

    default_uniform_block   = std::make_unique<erhe::graphics::Shader_resource>();
    shadow_sampler_location = default_uniform_block->add_sampler(
        "s_shadow",
        gl::Uniform_type::sampler_2d_array
    )->location();

    m_shader_path = std::filesystem::path("res") / std::filesystem::path("shaders");

    basic      = make_program("basic");
    brush      = make_program("brush");
    // Not available on Dell laptop.
    //standard   = make_program("standard", {}, {{gl::Shader_type::fragment_shader, "GL_NV_fragment_shader_barycentric"}});
    standard   = make_program("standard");
    edge_lines = make_program("edge_lines");
    wide_lines = make_program("wide_lines");
    points     = make_program("points");
    depth      = make_program("depth");
    id         = make_program("id");
    tool       = make_program("tool");
}

auto Programs::make_program(std::string_view name)
-> std::unique_ptr<erhe::graphics::Shader_stages>
{
    const std::vector<std::string> no_defines;
    return make_program(name, no_defines);
}

auto Programs::make_program(
    std::string_view name,
    std::string_view define
)
-> std::unique_ptr<erhe::graphics::Shader_stages>
{
    std::vector<std::string> defines;
    defines.push_back(std::string(define));
    return make_program(name, defines);
}

auto Programs::make_program(
    std::string_view                                            name,
    const std::vector<std::string>&                             defines,
    const std::vector<std::pair<gl::Shader_type, std::string>>& extensions
)
-> std::unique_ptr<erhe::graphics::Shader_stages>
{
    ERHE_PROFILE_FUNCTION

    log_programs.trace("Programs::make_program({})\n", name);
    log_programs.trace("current directory is {}\n", std::filesystem::current_path().string());

    const std::filesystem::path vs_path = m_shader_path / std::filesystem::path(std::string(name) + ".vert");
    const std::filesystem::path gs_path = m_shader_path / std::filesystem::path(std::string(name) + ".geom");
    const std::filesystem::path fs_path = m_shader_path / std::filesystem::path(std::string(name) + ".frag");

    const bool vs_exists = std::filesystem::exists(vs_path);
    const bool gs_exists = std::filesystem::exists(gs_path);
    const bool fs_exists = std::filesystem::exists(fs_path);

    const auto& shader_resources = *m_program_interface->shader_resources.get();

    Shader_stages::Create_info create_info{
        name,
        default_uniform_block.get(),
        &shader_resources.attribute_mappings,
        &shader_resources.fragment_outputs
    };
    create_info.add_interface_block(&shader_resources.material_block);
    create_info.add_interface_block(&shader_resources.light_block);
    create_info.add_interface_block(&shader_resources.camera_block);
    create_info.add_interface_block(&shader_resources.primitive_block);
    create_info.struct_types.push_back(&shader_resources.material_struct);
    create_info.struct_types.push_back(&shader_resources.light_struct);
    create_info.struct_types.push_back(&shader_resources.camera_struct);
    create_info.struct_types.push_back(&shader_resources.primitive_struct);
    for (auto j : defines)
    {
        create_info.defines.emplace_back(j, "1");
    }
    if (vs_exists)
    {
        create_info.shaders.emplace_back(gl::Shader_type::vertex_shader,   vs_path);
    }
    if (gs_exists)
    {
        create_info.shaders.emplace_back(gl::Shader_type::geometry_shader, gs_path);
    }
    if (fs_exists)
    {
        create_info.shaders.emplace_back(gl::Shader_type::fragment_shader, fs_path);
    }
    create_info.extensions = extensions;

    Shader_stages::Prototype prototype(create_info);
    VERIFY(prototype.is_valid());
    auto p = std::make_unique<Shader_stages>(std::move(prototype));

    if (m_shader_monitor)
    {
        m_shader_monitor->add(create_info, p.get());
    }

    return p;
}

}
