#pragma once

#include "erhe/components/components.hpp"
#include "erhe/graphics/fragment_outputs.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"

#include <memory>

namespace erhe::graphics
{

class Sampler;
class Shader_stages;

} // namespace erhe::graphics

namespace editor {

class Primitive_struct
{
public:
    size_t world_from_node; // mat4 16 * 4 bytes
    size_t color;           // vec4  4 * 4 bytes - id_offset / wire frame color
    size_t material_index;  // uint  1 * 4 bytes
    size_t size;            // uint  1 * 4 bytes - point size / line width
    size_t extra2;          // uint  1 * 4 bytes
    size_t extra3;          // uint  1 * 4 bytes
};

class Camera_struct
{
public:
    size_t world_from_node;      // mat4
    size_t world_from_clip;      // mat4
    size_t clip_from_world;      // mat4
    size_t viewport;             // vec4
    size_t fov;                  // vec4
    size_t clip_depth_direction; // float 1.0 = forward depth, -1.0 = reverse depth
    size_t view_depth_near;      // float
    size_t view_depth_far;       // float
    size_t exposure;             // float
};

class Light_struct
{
public:
    size_t texture_from_world; // mat4
    size_t position_and_inner_spot_cos;
    size_t direction_and_outer_spot_cos;
    size_t radiance_and_range;
};

class Light_block
{
public:
    size_t       shadow_texture;
    size_t       reserved_1;
    size_t       directional_light_count;
    size_t       spot_light_count;
    size_t       point_light_count;
    size_t       reserved_0;
    size_t       ambient_light;
    size_t       reserved_2;
    Light_struct light;
    size_t       light_struct;
};

class Material_struct
{
public:
    size_t roughness;    // vec2
    size_t metallic;     // float
    size_t transparency; // float
    size_t base_color;   // vec4
    size_t emissive;     // vec4
    size_t base_texture; // uvec2
    size_t reserved;     // uvec2
};

class Program_interface
    : public erhe::components::Component
{
public:
    static constexpr size_t           c_max_light_count{120};
    static constexpr std::string_view c_label{"Program_interface"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Program_interface ();
    ~Program_interface() noexcept override;
    Program_interface (const Program_interface&) = delete;
    void operator=    (const Program_interface&) = delete;
    Program_interface (Program_interface&&)      = delete;
    void operator=    (Program_interface&&)      = delete;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    class Shader_resources
    {
    public:
        Shader_resources();

        erhe::graphics::Vertex_attribute_mappings attribute_mappings;
        erhe::graphics::Fragment_outputs          fragment_outputs;

        Material_struct  material_block_offsets {};
        Light_block      light_block_offsets    {};
        Camera_struct    camera_block_offsets   {};
        Primitive_struct primitive_block_offsets{};

        erhe::graphics::Shader_resource material_block {"material",    0, erhe::graphics::Shader_resource::Type::uniform_block};
        erhe::graphics::Shader_resource light_block    {"light_block", 1, erhe::graphics::Shader_resource::Type::uniform_block};
        erhe::graphics::Shader_resource camera_block   {"camera",      2, erhe::graphics::Shader_resource::Type::uniform_block};
        erhe::graphics::Shader_resource primitive_block{"primitive",   3, erhe::graphics::Shader_resource::Type::shader_storage_block};

        erhe::graphics::Shader_resource material_struct {"Material"};
        erhe::graphics::Shader_resource light_struct    {"Light"};
        erhe::graphics::Shader_resource camera_struct   {"Camera"};
        erhe::graphics::Shader_resource primitive_struct{"Primitive"};
    };

    std::unique_ptr<Shader_resources> shader_resources;
};

}
