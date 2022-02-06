#pragma once

#include "erhe/components/components.hpp"
#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/toolkit/filesystem.hpp"

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
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Public members
    std::unique_ptr<erhe::graphics::Shader_resource> default_uniform_block; // containing sampler uniforms
    int                                              shadow_sampler_location{-1};
    int                                              gui_sampler_location   {-1};
    std::unique_ptr<erhe::graphics::Sampler>         nearest_sampler;
    std::unique_ptr<erhe::graphics::Sampler>         linear_sampler;
    std::unique_ptr<erhe::graphics::Sampler>         linear_mipmap_linear_sampler;

    std::unique_ptr<erhe::graphics::Shader_stages> brush;
    std::unique_ptr<erhe::graphics::Shader_stages> standard;
    std::unique_ptr<erhe::graphics::Shader_stages> textured;
    std::unique_ptr<erhe::graphics::Shader_stages> edge_lines;
    std::unique_ptr<erhe::graphics::Shader_stages> wide_lines;
    std::unique_ptr<erhe::graphics::Shader_stages> points;
    std::unique_ptr<erhe::graphics::Shader_stages> depth;
    std::unique_ptr<erhe::graphics::Shader_stages> id;
    std::unique_ptr<erhe::graphics::Shader_stages> tool;
    std::unique_ptr<erhe::graphics::Shader_stages> visualize_depth;

private:
    [[nodiscard]] auto make_program(
        std::string_view                                            name,
        const std::vector<std::string>&                             defines,
        const std::vector<std::pair<gl::Shader_type, std::string>>& extensions = {}
    ) -> std::unique_ptr<erhe::graphics::Shader_stages>;

    [[nodiscard]] auto make_program(
        const std::string_view name,
        const std::string_view define
    ) -> std::unique_ptr<erhe::graphics::Shader_stages>;

    [[nodiscard]] auto make_program(const std::string_view name) -> std::unique_ptr<erhe::graphics::Shader_stages>;

    // Component dependencies
    std::shared_ptr<Program_interface> m_program_interface;
    std::shared_ptr<Shader_monitor>    m_shader_monitor;
    bool                               m_dump_reflection{false};

    fs::path m_shader_path;
};

}
