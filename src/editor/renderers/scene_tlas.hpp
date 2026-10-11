#pragma once

#include "erhe_graphics/acceleration_structure.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/shader_resource.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace erhe::graphics {
    class Command_buffer;
    class Compute_command_encoder;
    class Device;
    class Ring_buffer_client;
}

namespace editor {

class Ray_trace_slot;

// The render phase half of a ray-query consumer's acceleration structures
// (Ray_trace_renderer, Ddgi_renderer, Radiance_cascades_renderer). Each
// consumer owns one; what they trace is the scene root's Ray_trace_scene,
// whose slot for the frame (prepared in M2) update() reads:
//  - the bottom level structures live in the slot's shared
//    Ray_trace_geometry objects, one per unique geometry for every
//    consumer; update() creates and builds the missing ones,
//  - one top level structure per frame-in-flight slot (rebuilding a
//    structure a still-in-flight frame reads is a data race),
//  - the per-instance record SSBO the shaders read through buffer device
//    addresses to fetch attributes and the material index.
//
// The Shader_resource declarations for the record struct / block are owned
// here too, so every consumer's GLSL sees the same layout; only the SSBO
// binding point is per-consumer (constructor argument).
class Scene_tlas
{
public:
    // CPU mirror of the std430 Instance_record the shaders read, indexed by
    // instance_custom_index. The layout is verified against the generated
    // Shader_resource offsets in the constructor.
    class Instance_record_data
    {
    public:
        uint64_t index_address;         // device address of the first triangle index
        uint64_t vertex_address;        // device address of the stream-1 vertex range start
        uint64_t position_address;      // device address of the stream-0 vertex range start (position-fetch fallback)
        uint32_t vertex_stride_uints;
        // BYTE granular, not uints: a quantized stream 0 has a 6 or 14 byte
        // stride, which uint granularity cannot express. The old guard that
        // skipped such instances dropped them from the TLAS entirely.
        uint32_t position_stride_bytes;
        uint32_t material_index;
        uint32_t flags;                 // bit 0: transmissive material
        // erhe::dataformat::Vertex_position_encoding of the stream-0 position.
        // Carried in the record rather than as a shader define: none of these
        // shaders is compiled through Shader_key, so none receives
        // ERHE_VERTEX_POSITION_ENCODING - and a record-carried selector also
        // survives a future per-primitive encoding.
        uint32_t position_encoding;
        // Pads the scalars to a multiple of 16 so the vec4s below land on their
        // own alignment and the std140-rounded Shader_resource size matches
        // sizeof (the layout VERIFYs in the constructor compare the two).
        uint32_t reserved0;
        // Dequantization affine for the fallback fetch. The BLAS carries the same
        // affine as a build transform, so object space is the dequantized one and
        // the fetched positions have to be dequantized to match.
        glm::vec4 position_scale;
        glm::vec4 position_offset;
    };
    static_assert(sizeof(Instance_record_data) == 80);

    // Instance mask bits, mirrored in the ray query shaders: bit 0 = every
    // instance, bit 1 = non-transmissive instances only (shadow rays trace
    // with the opaque mask so glass does not cast shadows).
    static constexpr uint32_t c_instance_mask_all    = 0x01u;
    static constexpr uint32_t c_instance_mask_opaque = 0x02u;

    // One frame's build result. The instance record range is acquired by
    // update() and must be bound (bind_instance_records) and then released
    // by the caller, like every other ring buffer range.
    class Frame
    {
    public:
        erhe::graphics::Acceleration_structure* acceleration_structure{nullptr};
        erhe::graphics::Ring_buffer_range       instance_records      {};
        std::size_t                             instance_count        {0};

        [[nodiscard]] auto is_valid() const -> bool { return acceleration_structure != nullptr; }
    };

    Scene_tlas(
        erhe::graphics::Device& graphics_device,
        unsigned int            instance_record_binding_point,
        const std::string&      debug_label
    );
    ~Scene_tlas() noexcept;

    // Shader_resource declarations to hand to Shader_stages_create_info.
    [[nodiscard]] auto get_instance_struct() -> erhe::graphics::Shader_resource&;
    [[nodiscard]] auto get_instance_block () -> erhe::graphics::Shader_resource&;

    // Instance count of the most recent update() (diagnostics / MCP).
    [[nodiscard]] auto get_instance_count() const -> std::size_t;

    // Builds the slot's missing bottom level structures into command_buffer,
    // builds this frame's top level structure from the slot's instances, and
    // uploads the slot's records. Render phase; must be called outside a
    // render pass. The records' material_index values are slots in the scene
    // root's FORWARD material set (doc/erhe/draw_list_material_set.md D5),
    // which the tracing dispatch binds.
    [[nodiscard]] auto update(erhe::graphics::Command_buffer& command_buffer, const Ray_trace_slot& slot) -> Frame;

    void bind_instance_records(erhe::graphics::Compute_command_encoder& encoder, const Frame& frame);

private:
    erhe::graphics::Device&                             m_graphics_device;
    std::string                                         m_debug_label;
    erhe::graphics::Shader_resource                     m_instance_struct;
    erhe::graphics::Shader_resource                     m_instance_block;
    std::unique_ptr<erhe::graphics::Ring_buffer_client> m_instance_record_buffer;

    // One top level structure per frame-in-flight slot. Slot count must be
    // >= the backend's frames in flight (Vulkan: 2).
    static constexpr std::size_t s_tlas_slot_count = 4;
    class Tlas_slot
    {
    public:
        std::unique_ptr<erhe::graphics::Acceleration_structure> acceleration_structure;
        uint32_t                                                capacity{0};
    };
    std::array<Tlas_slot, s_tlas_slot_count> m_tlas_slots;

    // Per-frame scratch, cleared at point of use (capacity kept).
    std::vector<erhe::graphics::Acceleration_structure_instance> m_instances;
};

} // namespace editor
