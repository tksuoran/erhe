#pragma once

#include "erhe_graphics/shader_resource.hpp"
#include "erhe_renderer/gpu_ring_buffer.hpp"
#include "erhe_primitive/enums.hpp"

#include <array>
#include <vector>

namespace erhe {
    class Item_filter;
}
namespace erhe::scene {
    class Mesh;
    class Mesh_layer;
    class Node;
}

namespace erhe::scene_renderer {

class Primitive_struct
{
public:
    std::size_t world_from_node;    // mat4 16 * 4 bytes
    std::size_t normal_transform;   // mat4 16 * 4 bytes
    std::size_t color;              // vec4  4 * 4 bytes - id_offset / wire frame color
    std::size_t material_index;     // uint  1 * 4 bytes
    std::size_t size;               // float 1 * 4 bytes - point size / line width
    std::size_t skinning_factor;    // float 1 * 4 bytes
    std::size_t base_joint_index;   // uint  1 * 4 bytes
};

class Primitive_interface
{
public:
    explicit Primitive_interface(erhe::graphics::Instance& graphics_instance);

    erhe::graphics::Shader_resource primitive_block;
    erhe::graphics::Shader_resource primitive_struct;
    Primitive_struct                offsets;
    std::size_t                     max_primitive_count;
};

enum class Primitive_color_source : unsigned int {
    id_offset = 0,
    mesh_wireframe_color,
    constant_color,
};

static constexpr std::array<std::string_view, 3> c_primitive_color_source_strings = {
    "ID Offset",
    "Mesh Wireframe color",
    "Constant Color"
};

static constexpr std::array<const char*, 3> c_primitive_color_source_strings_data{
    std::apply(
        [](auto&&... s) {
            return std::array{s.data()...};
        },
        c_primitive_color_source_strings
    )
};

// Primitive_color_source primitive_color_source  {Primitive_color_source::constant_color};
// glm::vec4              primitive_constant_color{1.0f, 1.0f, 1.0f, 1.0f};

enum class Primitive_size_source : unsigned int
{
    mesh_point_size = 0,
    mesh_line_width,
    constant_size
};

class Primitive_interface_settings
{
public:
    static constexpr std::array<std::string_view, 3> c_primitive_color_source_strings =
    {
        "ID Offset",
        "Mesh Wireframe color",
        "Constant Color"
    };

    static constexpr std::array<const char*, 3> c_primitive_color_source_strings_data{
        std::apply(
            [](auto&&... s) {
                return std::array{s.data()...};
            },
            c_primitive_color_source_strings
        )
    };

    Primitive_color_source color_source  {Primitive_color_source::constant_color};
    glm::vec4              constant_color{1.0f, 1.0f, 1.0f, 1.0f};
    Primitive_size_source  size_source   {Primitive_size_source::constant_size};
    float                  constant_size {1.0f};
};

class Primitive_buffer : public erhe::renderer::GPU_ring_buffer
{
public:
    Primitive_buffer(erhe::graphics::Instance& graphics_instance, Primitive_interface& primitive_interface);

    using Mesh_layer_collection = std::vector<const erhe::scene::Mesh_layer*>;

    auto update(
        const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes,
        erhe::primitive::Primitive_mode                            primitive_mode,
        const erhe::Item_filter&                                   filter,
        const Primitive_interface_settings&                        settings,
        std::size_t&                                               out_primitive_count,
        bool                                                       use_id_ranges = false
    ) -> erhe::renderer::Buffer_range;

    auto update(
        const std::span<const std::shared_ptr<erhe::scene::Node>>& nodes,
        const Primitive_interface_settings&                        primitive_settings
    ) -> erhe::renderer::Buffer_range;

    class Id_range
    {
    public:
        uint32_t           offset         {0};
        uint32_t           length         {0};
        erhe::scene::Mesh* mesh           {nullptr};
        std::size_t        primitive_index{0};
    };

    void reset_id_ranges();
    [[nodiscard]] auto id_offset() const -> uint32_t;
    [[nodiscard]] auto id_ranges() const -> const std::vector<Id_range>&;

private:
    Primitive_interface&  m_primitive_interface;
    uint32_t              m_id_offset{0};
    std::vector<Id_range> m_id_ranges;
};

} // namespace erhe::scene_renderer
