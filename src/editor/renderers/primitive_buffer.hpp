#pragma once

#include "renderers/enums.hpp"

#include "erhe/application/renderers/multi_buffer.hpp"

#include "erhe/graphics/shader_resource.hpp"

#include <vector>

namespace erhe::scene
{
    class Mesh;
    class Mesh_layer;
    class Item_filter;
}

namespace editor
{

class Primitive_struct
{
public:
    std::size_t world_from_node;            // mat4 16 * 4 bytes
    std::size_t world_from_node_cofactor;   // mat4 16 * 4 bytes
    std::size_t color;                      // vec4  4 * 4 bytes - id_offset / wire frame color
    std::size_t material_index;             // uint  1 * 4 bytes
    std::size_t size;                       // uint  1 * 4 bytes - point size / line width
    std::size_t extra2;                     // uint  1 * 4 bytes
    std::size_t extra3;                     // uint  1 * 4 bytes
};

class Primitive_interface
{
public:
    explicit Primitive_interface(std::size_t max_primitive_count);

    erhe::graphics::Shader_resource primitive_block;
    erhe::graphics::Shader_resource primitive_struct;
    Primitive_struct                offsets;
    std::size_t                     max_primitive_count;
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

class Primitive_buffer
    : public erhe::application::Multi_buffer
{
public:
    explicit Primitive_buffer(Primitive_interface* primitive_interface);

    using Mesh_layer_collection = std::vector<const erhe::scene::Mesh_layer*>;

    auto update(
        const gsl::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes,
        const erhe::scene::Item_filter&                            filter,
        const Primitive_interface_settings&                        settings,
        bool                                                       use_id_ranges = false
    ) -> erhe::application::Buffer_range;

    class Id_range
    {
    public:
        uint32_t                           offset         {0};
        uint32_t                           length         {0};
        std::shared_ptr<erhe::scene::Mesh> mesh           {};
        std::size_t                        primitive_index{0};
    };

    void reset_id_ranges();
    [[nodiscard]] auto id_offset() const -> uint32_t;
    [[nodiscard]] auto id_ranges() const -> const std::vector<Id_range>&;

private:
    Primitive_interface*  m_primitive_interface{nullptr};
    uint32_t              m_id_offset{0};
    std::vector<Id_range> m_id_ranges;
};

} // namespace editor
