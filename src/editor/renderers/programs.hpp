#pragma once

#include "erhe/components/component.hpp"

#include <filesystem>
#include <string_view>
#include <vector>


namespace erhe::graphics
{

class Sampler;
class Shader_resource;
class Shader_stages;
class Shader_monitor;

} // namespace erhe::graphics

namespace editor {

class Program_interface;

class Programs
    : public erhe::components::Component
{
public:
    static constexpr const char* c_name = "Programs";
    Programs      ();
    ~Programs     () override;
    Programs      (const Programs&) = delete;
    void operator=(const Programs&) = delete;
    Programs      (Programs&&)      = delete;
    void operator=(Programs&&)      = delete;

    // Implements Component
    void connect             () override;
    void initialize_component() override;

private:
    auto make_program(std::string_view                name,
                      const std::vector<std::string>& defines)
    -> std::unique_ptr<erhe::graphics::Shader_stages>;

    auto make_program(std::string_view name,
                      std::string_view define)
    -> std::unique_ptr<erhe::graphics::Shader_stages>;

    auto make_program(std::string_view name)
    -> std::unique_ptr<erhe::graphics::Shader_stages>;

public:
    std::unique_ptr<erhe::graphics::Shader_resource> default_uniform_block; // containing sampler uniforms
    int                                              shadow_sampler_location{0};
    std::unique_ptr<erhe::graphics::Sampler>         nearest_sampler;
    std::unique_ptr<erhe::graphics::Sampler>         linear_sampler;

    std::unique_ptr<erhe::graphics::Shader_stages> basic;
    std::unique_ptr<erhe::graphics::Shader_stages> standard;    // standard material, polygon fill
    std::unique_ptr<erhe::graphics::Shader_stages> edge_lines;
    std::unique_ptr<erhe::graphics::Shader_stages> wide_lines;
    std::unique_ptr<erhe::graphics::Shader_stages> points;
    std::unique_ptr<erhe::graphics::Shader_stages> depth;
    std::unique_ptr<erhe::graphics::Shader_stages> id;
    std::unique_ptr<erhe::graphics::Shader_stages> tool;

private:
    std::shared_ptr<Program_interface>              m_program_interface;
    std::shared_ptr<erhe::graphics::Shader_monitor> m_shader_monitor;
    std::filesystem::path                           m_shader_path;
};

}
