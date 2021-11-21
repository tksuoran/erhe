#pragma once

#include "erhe/components/component.hpp"
#include "erhe/gl/wrapper_enums.hpp"

#include <filesystem>
#include <string_view>
#include <vector>

namespace erhe::graphics
{

class Sampler;
class Shader_resource;
class Shader_stages;

} // namespace erhe::graphics

namespace editor {

class Program_interface;
class Shader_monitor;

class Programs
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name{"Programs"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Programs      ();
    ~Programs     () override;
    Programs      (const Programs&) = delete;
    void operator=(const Programs&) = delete;
    Programs      (Programs&&)      = delete;
    void operator=(Programs&&)      = delete;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

private:
    auto make_program(
        std::string_view                                            name,
        const std::vector<std::string>&                             defines,
        const std::vector<std::pair<gl::Shader_type, std::string>>& extensions = {}
    ) -> std::unique_ptr<erhe::graphics::Shader_stages>;

    auto make_program(
        std::string_view name,
        std::string_view define
    ) -> std::unique_ptr<erhe::graphics::Shader_stages>;

    auto make_program(std::string_view name)
        -> std::unique_ptr<erhe::graphics::Shader_stages>;

public:
    std::unique_ptr<erhe::graphics::Shader_resource> default_uniform_block; // containing sampler uniforms
    int                                              shadow_sampler_location{0};
    std::unique_ptr<erhe::graphics::Sampler>         nearest_sampler;
    std::unique_ptr<erhe::graphics::Sampler>         linear_sampler;

    std::unique_ptr<erhe::graphics::Shader_stages> basic;
    std::unique_ptr<erhe::graphics::Shader_stages> brush;
    std::unique_ptr<erhe::graphics::Shader_stages> standard;    // standard material, polygon fill
    std::unique_ptr<erhe::graphics::Shader_stages> edge_lines;
    std::unique_ptr<erhe::graphics::Shader_stages> wide_lines;
    std::unique_ptr<erhe::graphics::Shader_stages> points;
    std::unique_ptr<erhe::graphics::Shader_stages> depth;
    std::unique_ptr<erhe::graphics::Shader_stages> id;
    std::unique_ptr<erhe::graphics::Shader_stages> tool;

private:
    Program_interface*    m_program_interface{nullptr};
    Shader_monitor*       m_shader_monitor   {nullptr};
    std::filesystem::path m_shader_path;
};

}
