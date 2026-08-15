#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_scene_renderer/generated/primitive_color_source.hpp"

#include <glm/glm.hpp>

#include <array>
#include <span>
#include <vector>

namespace erhe {
    class Item_filter;
}
namespace erhe::scene {
    class Mesh;
    class Mesh_layer;
    class Mesh_primitive_ref;
    class Node;
}

namespace erhe::scene_renderer {

class Draw_list;
class Draw_list_scene;
class Face_id_base_provider;
class Render_bucket;

class Primitive_struct
{
public:
    std::size_t world_from_node;    // mat4 16 * 4 bytes
    std::size_t normal_transform;   // mat4 16 * 4 bytes
    std::size_t color;              // vec4  4 * 4 bytes - id_offset / wire frame color
    std::size_t lightmap_scale_offset; // vec4 4 * 4 bytes - baked lightmap atlas region (uv2 * xy + zw); xy 0 = no lightmap
    std::size_t material_index;     // uint  1 * 4 bytes
    std::size_t size;               // float 1 * 4 bytes - point size / line width
    std::size_t skinning_factor;    // float 1 * 4 bytes
    std::size_t base_joint_index;   // uint  1 * 4 bytes
    std::size_t base_vertex;        // uint  1 * 4 bytes - first vertex of this primitive in the shared vertex pool;
                                    // the ID-render shader subtracts it from gl_VertexID so the packed triangle id
                                    // is the 0-based per-primitive facet index (not a pool-global vertex index).
};

class Primitive_interface
{
public:
    Primitive_interface(erhe::graphics::Device& graphics_device, int max_primitive_count);

    erhe::graphics::Shader_resource primitive_block;
    erhe::graphics::Shader_resource primitive_struct;
    Primitive_struct                offsets;
    std::size_t                     max_primitive_count;
};

using ::Primitive_color_source;

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

    Primitive_color_source color_source   {Primitive_color_source::constant_color};
    glm::vec4              constant_color0{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4              constant_color1{1.0f, 0.0f, 0.0f, 1.0f};
    Primitive_size_source  size_source    {Primitive_size_source::constant_size};
    float                  constant_size  {1.0f};
    // ID-buffer edge-line method: when non-null, the per-primitive face-id base
    // is written into primitive.color (as a raw float in .x) instead of the
    // color_source value. The lit polygon-fill variant does not read
    // primitive.color, so this is free; the EDGE_LINES_FROM_ID vertex shader
    // reads it back as the base to add to its facet id. Both this fill stamp and
    // the edge-id pre-pass read the same provider, so face ids match.
    const Face_id_base_provider* face_id_base_provider{nullptr};
};

class Primitive_buffer : public erhe::graphics::Ring_buffer_client
{
public:
    Primitive_buffer(erhe::graphics::Device& graphics_device, Primitive_interface& primitive_interface);

    using Mesh_layer_collection = std::vector<const erhe::scene::Mesh_layer*>;

    auto update(
        const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes,
        const erhe::Item_filter&                                   filter,
        erhe::primitive::Primitive_mode                            primitive_mode,
        const Primitive_interface_settings&                        settings,
        std::size_t&                                               out_primitive_count,
        bool                                                       use_id_ranges = false
    ) -> erhe::graphics::Ring_buffer_range;

    auto update(
        const Render_bucket&                bucket,
        erhe::primitive::Primitive_mode     primitive_mode,
        const Primitive_interface_settings& settings,
        bool                                use_id_ranges = false
    ) -> erhe::graphics::Ring_buffer_range;

    // Draw-list overload (doc/draw_list_renderer_requirements.md R8/R8a):
    // writes one primitive record per entry in [begin, end) of draw_list that
    // passes filter (evaluated on the entry's mirrored flag bits), in entry
    // order. Draw_indirect_buffer::update(Draw_list, ...) with the same
    // arguments emits exactly the matching draw commands. Per-frame inputs
    // (node transform, material / joint GPU slots, lightmap scale/offset)
    // are read live from the Mesh via draw_list_scene (R12b). No id ranges.
    auto update(
        const Draw_list&                    draw_list,
        std::size_t                         begin,
        std::size_t                         end,
        const Draw_list_scene&              draw_list_scene,
        const erhe::Item_filter&            filter,
        const Primitive_interface_settings& settings,
        std::size_t&                        out_primitive_count
    ) -> erhe::graphics::Ring_buffer_range;

    auto update(
        const std::span<const std::shared_ptr<erhe::scene::Node>>& nodes,
        const Primitive_interface_settings&                        primitive_settings
    ) -> erhe::graphics::Ring_buffer_range;

    class Id_range
    {
    public:
        uint32_t           offset                         {0};
        uint32_t           length                         {0};
        erhe::scene::Mesh* mesh                           {nullptr};
        std::size_t        index_of_gltf_primitive_in_mesh{0};
    };

    void reset_id_ranges();
    // Capacity of one primitive block (P3a chunking bound for draw lists).
    [[nodiscard]] auto get_max_primitive_count() const -> std::size_t { return m_primitive_interface.max_primitive_count; }
    [[nodiscard]] auto id_offset() const -> uint32_t;
    [[nodiscard]] auto id_ranges() const -> const std::vector<Id_range>&;

private:
    Primitive_interface&  m_primitive_interface;
    // Shared per-primitive record writer for both the Render_bucket and the
    // Draw_list overloads (single implementation, no drift). Advances
    // write_offset by one record and maintains m_id_offset / m_id_ranges.
    void write_primitive(
        erhe::scene::Mesh&                  mesh,
        uint16_t                            mesh_primitive_index,
        erhe::primitive::Primitive_mode     primitive_mode,
        const Primitive_interface_settings& settings,
        bool                                use_id_ranges,
        std::span<std::byte>                primitive_gpu_data,
        std::size_t&                        write_offset
    );

    uint32_t              m_id_offset{0};
    std::vector<Id_range> m_id_ranges;
};

} // namespace erhe::scene_renderer
