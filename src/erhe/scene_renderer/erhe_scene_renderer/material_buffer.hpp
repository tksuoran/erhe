#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace erhe::graphics {
    class Sampler;
    class Texture;
    class Texture_heap;
}
namespace erhe::primitive {
    class Material;
}

namespace erhe::scene_renderer {

class Program_interface;
class Shader_resources;

class Material_struct
{
public:
    std::size_t roughness;                  // vec2
    std::size_t metallic;                   // float
    std::size_t reflectance;                // float

    std::size_t base_color;                 // vec4
    std::size_t emissive;                   // vec4

    std::size_t base_color_texture;         // uvec2
    std::size_t metallic_roughness_texture; // uvec2

    std::size_t normal_texture;             // uvec2
    std::size_t occlusion_texture;          // uvec2

    std::size_t emissive_texture;           // uvec2
    std::size_t opacity;                    // float
    std::size_t normal_texture_scale;       // float
    std::size_t alpha_cutoff;               // float

    std::size_t base_color_rotation_scale;         // uvec4
    std::size_t metallic_roughness_rotation_scale; // uvec4
    std::size_t normal_rotation_scale;             // uvec4
    std::size_t occlusion_rotation_scale;          // uvec4
    std::size_t emissive_rotation_scale;           // uvec4

    std::size_t base_color_offset;                 // uvec2
    std::size_t metallic_roughness_offset;         // uvec2

    std::size_t normal_offset;                     // uvec2
    std::size_t occlusion_offset;                  // uvec2

    std::size_t emissive_offset;                   // uvec2
    std::size_t occlusion_texture_strength;        // float

    std::size_t ior;                               // float
    std::size_t transmission;                      // float
    // Bxdf_model as uint, for shaders that select the BxDF at runtime (the
    // ray tracer); the raster path keeps its compile-time variant axis.
    std::size_t bxdf_model;                        // uint

    // The normal texture decode (`texel * scale + bias`). Two vec4s, both
    // vec4-aligned, keeping the struct size a multiple of 16 bytes.
    std::size_t normal_texture_decode_scale;       // vec4
    std::size_t normal_texture_decode_bias;        // vec4

    // Which channel of the slot's texture each scalar input reads, as the
    // component index the shader indexes the sampled vec4 with:
    // (metallic, roughness, occlusion, opacity). One uvec4, vec4-aligned,
    // so the struct size stays a multiple of 16 bytes.
    std::size_t texture_channels;                  // uvec4
};

// The texture half of one material record, resolved: the exact texture and
// sampler the heap allocation is made from, and the packed rotation / scale /
// offset the record carries.
class Material_texture_record_inputs
{
public:
    const erhe::graphics::Texture* texture          {nullptr};
    const erhe::graphics::Sampler* sampler          {nullptr};
    float                          rotation_scale[4]{0.0f, 0.0f, 0.0f, 0.0f};
    float                          offset        [2]{0.0f, 0.0f};
};

// Everything one material record is written from, and nothing else.
//
// The record writer and the content hash (doc/draw_list_material_set_plan.md
// D10) both read this struct and only this struct, so the hash covers exactly
// the bytes the writer reads by construction rather than by a comment asking
// two lists to be kept in step. A field that dirties the buffer is a field
// that appears here; a material field that does not - the shader-variant axes
// (blending mode, double_sided, normalmap encoding, the texgen modes,
// use_aniso_control) - reaches the shader by another route and is the draw
// list's identity hash to notice.
//
// Value-initialization zeroes padding as well as members, which is what makes
// hashing the whole object well defined.
class Material_record_inputs
{
public:
    glm::vec2 roughness                 {0.0f, 0.0f};
    float     metallic                  {0.0f};
    float     reflectance               {0.0f};
    glm::vec3 base_color                {0.0f, 0.0f, 0.0f};
    float     opacity                   {0.0f};
    glm::vec3 emissive                  {0.0f, 0.0f, 0.0f};
    float     normal_texture_scale      {0.0f};
    float     alpha_cutoff              {0.0f};
    float     occlusion_texture_strength{0.0f};
    float     ior                       {0.0f};
    float     transmission              {0.0f};
    uint32_t  bxdf_model                {0};
    glm::vec4 normal_texture_decode_scale{0.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 normal_texture_decode_bias {0.0f, 0.0f, 0.0f, 0.0f};
    glm::uvec4 texture_channels          {0u, 0u, 0u, 0u};

    Material_texture_record_inputs base_color_texture        {};
    Material_texture_record_inputs metallic_roughness_texture{};
    Material_texture_record_inputs normal_texture            {};
    Material_texture_record_inputs occlusion_texture         {};
    Material_texture_record_inputs emissive_texture          {};
};

// GPU samplers for the slot sampler states (erhe::primitive::
// Material_sampler_state): one Sampler per distinct state, created on
// first use and kept for the life of the cache, so a state always resolves
// to the same pointer (the texture heap keys its allocations by it).
class Material_sampler_cache
{
public:
    explicit Material_sampler_cache(erhe::graphics::Device& graphics_device);

    [[nodiscard]] auto get(const erhe::primitive::Material_sampler_state& state) -> const erhe::graphics::Sampler&;

private:
    erhe::graphics::Device& m_graphics_device;
    std::vector<std::pair<erhe::primitive::Material_sampler_state, std::unique_ptr<erhe::graphics::Sampler>>> m_samplers;
};

// Resolves a material to its record inputs. Texture references are resolved
// here, so a re-baked editor Graph_texture yields a different Texture pointer
// and therefore both a different record and a different content hash; the
// slot sampler states resolve through the cache the same way.
[[nodiscard]] auto gather_material_record_inputs(
    const erhe::primitive::Material& material,
    Material_sampler_cache&          sampler_cache
) -> Material_record_inputs;

// The record inputs a primitive with no material of its own renders with:
// erhe::primitive::Material_values at its defaults - roughness 0.5,
// metallic 0, opaque, no textures - with UsdPreviewSurface's unbound
// diffuseColor 0.18 grey as the base color. This is what slot
// Material_set::default_material_slot_index carries, so an unbound mesh is
// lit like any other surface instead of reading a zeroed record (black) or
// whichever material happened to hold slot 0.
[[nodiscard]] auto get_default_material_record_inputs() -> Material_record_inputs;

// The same record with a white base color, for an unbound primitive whose
// source authored vertex colors: the shader multiplies the base color by the
// vertex color, so white makes the vertex color the albedo. This is what slot
// Material_set::vertex_colored_default_material_slot_index carries.
[[nodiscard]] auto get_vertex_colored_default_material_record_inputs() -> Material_record_inputs;

class Material_interface
{
public:
    Material_interface(erhe::graphics::Device& graphics_device, int max_material_count);

    erhe::graphics::Shader_resource material_block;
    erhe::graphics::Shader_resource material_struct;
    Material_struct                 offsets;
    std::size_t                     max_material_count;
};

// Writes material records. Owned by a Material_set, which owns the storage it
// writes into and the texture heap the handles come from; it is not a
// Ring_buffer_client, because the records persist across frames and are
// rewritten only when their inputs change.
class Material_buffer
{
public:
    Material_buffer(erhe::graphics::Device& graphics_device, Material_interface& material_interface);

    // Slot-table-driven record writer (doc/draw_list_material_set_plan.md D2).
    // Writes one record per entry of slot_materials, in slot order, into
    // storage the caller owns; a null entry is a hole and is zero-filled. It
    // assigns no slot and writes nothing on the Material - the slot IS the
    // index into slot_materials, issued by the Material_set that owns this
    // buffer.
    void write_records(
        std::span<std::byte>                              gpu_data,
        erhe::graphics::Texture_heap&                     texture_heap,
        std::span<const erhe::primitive::Material* const> slot_materials
    );

    // Hash of everything write_records() reads for this material (D10). Both
    // go through gather_material_record_inputs(), so neither can drift from
    // the other.
    [[nodiscard]] auto get_content_hash    (const erhe::primitive::Material* material) const -> uint64_t;
    [[nodiscard]] auto get_record_byte_count() const -> std::size_t;

private:
    void write_record(
        std::span<std::byte>          gpu_data,
        std::size_t                   write_offset,
        const Material_record_inputs& inputs,
        erhe::graphics::Texture_heap& texture_heap
    );

    erhe::graphics::Device& m_graphics_device;
    Material_interface&     m_material_interface;

    // Mutable: get_content_hash is a read of the material, and a state seen
    // for the first time there creates its sampler like write_records would.
    mutable Material_sampler_cache m_sampler_cache;
};

} // namespace erhe::scene_renderer
