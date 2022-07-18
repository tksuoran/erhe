#pragma once

#include "task_queue.hpp"

#include "erhe/components/components.hpp"
#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/toolkit/filesystem.hpp"

#include <string_view>
#include <vector>

namespace erhe::graphics
{
    class Sampler;
    class Shader_resource;
    class Shader_stages;
}

namespace erhe::application
{
    class Shader_monitor;
}

namespace editor {

class Program_interface;

enum class Shader_stages_variant : int
{
    standard,
    debug_depth,
    debug_normal,
    debug_tangent,
    debug_bitangent,
    debug_texcoord,
    debug_vertex_color_rgb,
    debug_vertex_color_alpha,
    debug_misc
};

static constexpr const char* c_shader_stages_variant_strings[] =
{
    "Standard",
    "Debug Depth",
    "Debug Normal",
    "Debug Tangent",
    "Debug Bitangent",
    "Debug TexCoord",
    "Debug Vertex Color RGB",
    "Debug Vertex Color Alpha",
    "Debug Miscellaneous"
};

class Programs
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_label{"Programs"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Programs      ();
    ~Programs     () noexcept override;
    Programs      (const Programs&) = delete;
    void operator=(const Programs&) = delete;
    Programs      (Programs&&)      = delete;
    void operator=(Programs&&)      = delete;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    static constexpr std::size_t s_texture_unit_count = 15; // for non bindless textures

    // Public members
    std::unique_ptr<erhe::graphics::Shader_resource> shadow_map_default_uniform_block; // for non-bindless textures
    std::unique_ptr<erhe::graphics::Shader_resource> textured_default_uniform_block;   // for non-bindless textures
    int                                              shadow_texture_unit{0};
    int                                              base_texture_unit{1};
    std::unique_ptr<erhe::graphics::Sampler>         nearest_sampler;
    std::unique_ptr<erhe::graphics::Sampler>         linear_sampler;
    std::unique_ptr<erhe::graphics::Sampler>         linear_mipmap_linear_sampler;

    std::unique_ptr<erhe::graphics::Shader_stages> brush;
    std::unique_ptr<erhe::graphics::Shader_stages> standard;
    std::unique_ptr<erhe::graphics::Shader_stages> textured;
    std::unique_ptr<erhe::graphics::Shader_stages> wide_lines_draw_color;
    std::unique_ptr<erhe::graphics::Shader_stages> wide_lines_vertex_color;
    std::unique_ptr<erhe::graphics::Shader_stages> points;
    std::unique_ptr<erhe::graphics::Shader_stages> depth;
    std::unique_ptr<erhe::graphics::Shader_stages> id;
    std::unique_ptr<erhe::graphics::Shader_stages> tool;
    std::unique_ptr<erhe::graphics::Shader_stages> debug_depth;
    std::unique_ptr<erhe::graphics::Shader_stages> debug_normal;
    std::unique_ptr<erhe::graphics::Shader_stages> debug_tangent;
    std::unique_ptr<erhe::graphics::Shader_stages> debug_bitangent;
    std::unique_ptr<erhe::graphics::Shader_stages> debug_texcoord;
    std::unique_ptr<erhe::graphics::Shader_stages> debug_vertex_color_rgb;
    std::unique_ptr<erhe::graphics::Shader_stages> debug_vertex_color_alpha;
    std::unique_ptr<erhe::graphics::Shader_stages> debug_misc;

private:
    void queue(
        std::unique_ptr<erhe::graphics::Shader_stages>& program,
        erhe::graphics::Shader_stages::Create_info      create_info
    );

    [[nodiscard]] auto make_program(erhe::graphics::Shader_stages::Create_info create_info) -> std::unique_ptr<erhe::graphics::Shader_stages>;

    // Component dependencies
    std::shared_ptr<Program_interface>                 m_program_interface;
    std::shared_ptr<erhe::application::Shader_monitor> m_shader_monitor;
    fs::path m_shader_path;

    std::unique_ptr<ITask_queue> m_queue;
};

}
