#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_scene_renderer/generated/primitive_color_source.hpp"

#include <glm/glm.hpp>

#include <array>
#include <span>
#include <vector>

namespace erhe {
    class Item_filter;
}
namespace erhe::primitive {
    class Buffer_mesh;
}
namespace erhe::scene {
    class Mesh;
    class Mesh_layer;
    class Mesh_primitive_ref;
    class Xformable; using Node = Xformable;
}

namespace erhe::scene_renderer {

class Draw_list;
class Draw_list_scene;
class Material_set;
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
    std::size_t position_scale;     // vec4  4 * 4 bytes - xyz = object space AABB half extent, w unused
    std::size_t position_offset;    // vec4  4 * 4 bytes - xyz = object space AABB center,      w unused
                                    // Vertex position dequantization affine: decoded = a_position * scale + offset.
                                    // Always the primitive's real AABB - it is the *decode* that is a no-op while
                                    // positions are stored as float3, because a passthrough shader never reads these.
    std::size_t texcoord_scale;     // vec4  4 * 4 bytes - xy = channel 0 UV extent, zw = channel 1 UV extent
    std::size_t texcoord_offset;    // vec4  4 * 4 bytes - xy = channel 0 UV min,    zw = channel 1 UV min
                                    // Texcoord dequantization affine, same contract as the position one:
                                    // decoded = a_texcoord_N * scale + offset. Channel 2 (lightmap) is never
                                    // affine-encoded and does not appear here.
};

// Dequantization affine for one primitive, derived from its object space AABB.
// Encoder and decoder must agree exactly, so both go through this:
//   scale  = max(0.5 * (max - min), epsilon)  per axis
//   offset = 0.5 * (max + min)
// A degenerate axis encodes to exactly 0 and decodes back to the center, so the
// epsilon never contributes there. An AABB that is invalid - i.e. never had a
// point included - yields the identity instead.
class Position_quantization
{
public:
    glm::vec4 scale {1.0f, 1.0f, 1.0f, 0.0f};
    glm::vec4 offset{0.0f, 0.0f, 0.0f, 0.0f};
};

[[nodiscard]] auto get_position_quantization(const erhe::math::Aabb& bounding_box) -> Position_quantization;

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
};

class Primitive_buffer : public erhe::graphics::Ring_buffer_client
{
public:
    Primitive_buffer(erhe::graphics::Device& graphics_device, Primitive_interface& primitive_interface);

    using Mesh_layer_collection = std::vector<const erhe::scene::Mesh_layer*>;

    // material_source is the set this pass binds - the owning Scene_root's
    // FORWARD set for the bucket path (doc/draw_list_material_set_plan.md D5),
    // so a record's material_index and the buffer bound when it is drawn agree.
    // Null writes slot 0 for every primitive, which is what Id_renderer wants:
    // its shader reads only the id and it binds no material set at all.
    auto update(
        const Render_bucket&                bucket,
        const Material_set*                 material_source,
        erhe::primitive::Primitive_mode     primitive_mode,
        const Primitive_interface_settings& settings,
        bool                                use_id_ranges = false
    ) -> erhe::graphics::Ring_buffer_range;

    // Draw-list overload (doc/draw_list_renderer_requirements.md R8/R8a; the
    // material slot it resolves comes from the draw list's own Material_set):
    // writes one primitive record per entry in [begin, end) of draw_list that
    // passes filter (evaluated on the entry's mirrored flag bits), in entry
    // order. Draw_indirect_buffer::update(Draw_list, ...) with the same
    // arguments emits exactly the matching draw commands. Records are copied
    // from draw_list.primitive_records (doc/draw_list_performance_improvements.md)
    // with only the pass-dependent color / size patched; settings that need
    // per-mesh evaluation fall back to write_primitive() via draw_list_scene.
    // No id ranges.
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
        const Material_set*                 material_source,
        uint16_t                            mesh_primitive_index,
        // The variant the bucket chose. base_vertex, the index ranges and the
        // quantization AABB all have to come from the mesh that is drawn.
        const erhe::primitive::Buffer_mesh& buffer_mesh,
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
