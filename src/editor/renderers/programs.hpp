#pragma once

#include "erhe/components/component.hpp"
#include "erhe/graphics/fragment_outputs.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/sampler.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"

namespace erhe::graphics
{

class Shader_stages;

} // namespace erhe::graphics

namespace editor {

struct Primitive_struct
{
    size_t world_from_node; // mat4 16 * 4 bytes
    size_t color;           // vec4  4 * 4 bytes - id_offset / wire frame color
    size_t material_index;  // uint  1 * 4 bytes
    size_t size;            // uint  1 * 4 bytes - point size / line width
    size_t extra2;          // uint  1 * 4 bytes
    size_t extra3;          // uint  1 * 4 bytes
};

struct Camera_struct
{
    size_t world_from_node; // mat4
    size_t world_from_clip; // mat4
    size_t clip_from_world; // mat4
    size_t viewport;        // vec4
    size_t exposure;
};

struct Light_struct
{
    size_t texture_from_world; // mat4
    size_t position_and_inner_spot_cos;
    size_t direction_and_outer_spot_cos;
    size_t radiance_and_range;
};

struct Light_block
{
    size_t       directional_light_count;
    size_t       spot_light_count;
    size_t       point_light_count;
    size_t       reserved;
    size_t       ambient_light;
    size_t       light_struct;
    Light_struct light;
};

struct Material_struct
{
    size_t metallic;     // float
    size_t roughness;    // float
    size_t anisotropy;   // float
    size_t transparency; // float
    size_t base_color;   // vec4
    size_t emissive;     // vec4
};

class Programs
    : public erhe::components::Component
{
public:
    Programs()
        : erhe::components::Component{"Programs"}
    {
    }

    virtual ~Programs() = default;

    void connect(std::shared_ptr<erhe::graphics::Shader_monitor> shader_monitor);

    void initialize_component() override;

private:
    auto make_program(const std::string&              name,
                      const std::vector<std::string>& defines)
    -> std::unique_ptr<erhe::graphics::Shader_stages>;

    auto make_program(const std::string& name,
                      const std::string& define)
    -> std::unique_ptr<erhe::graphics::Shader_stages>;

    auto make_program(const std::string& name)
    -> std::unique_ptr<erhe::graphics::Shader_stages>;

public:
    Material_struct  material_block_offsets {};
    Light_block      light_block_offsets    {};
    Camera_struct    camera_block_offsets   {};
    Primitive_struct primitive_block_offsets{};

    erhe::graphics::Shader_resource           material_block {"material",    0, erhe::graphics::Shader_resource::Type::uniform_block};
    erhe::graphics::Shader_resource           light_block    {"light_block", 1, erhe::graphics::Shader_resource::Type::uniform_block};
    erhe::graphics::Shader_resource           camera_block   {"camera",      2, erhe::graphics::Shader_resource::Type::uniform_block};
    erhe::graphics::Shader_resource           primitive_block{"primitive",   3, erhe::graphics::Shader_resource::Type::shader_storage_block};

    erhe::graphics::Shader_resource           material_struct {"Material"};
    erhe::graphics::Shader_resource           light_struct    {"Light"};
    erhe::graphics::Shader_resource           camera_struct   {"Camera"};
    erhe::graphics::Shader_resource           primitive_struct{"Primitive"};

    erhe::graphics::Vertex_attribute_mappings attribute_mappings;
    erhe::graphics::Fragment_outputs          fragment_outputs;

    erhe::graphics::Shader_resource           default_uniform_block; // containing sampler uniforms
    int                                       shadow_sampler_location{0};

    std::unique_ptr<erhe::graphics::Sampler>  nearest_sampler;
    std::unique_ptr<erhe::graphics::Sampler>  linear_sampler;

    std::unique_ptr<erhe::graphics::Shader_stages> basic;
    std::unique_ptr<erhe::graphics::Shader_stages> standard;    // standard material, polygon fill
    std::unique_ptr<erhe::graphics::Shader_stages> edge_lines;
    std::unique_ptr<erhe::graphics::Shader_stages> wide_lines;
    std::unique_ptr<erhe::graphics::Shader_stages> points;
    std::unique_ptr<erhe::graphics::Shader_stages> depth;
    std::unique_ptr<erhe::graphics::Shader_stages> id;
    std::unique_ptr<erhe::graphics::Shader_stages> tool;

private:
    std::shared_ptr<erhe::graphics::Shader_monitor> m_shader_monitor;
    std::filesystem::path                           m_shader_path;
};

}
