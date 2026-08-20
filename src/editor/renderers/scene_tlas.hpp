#pragma once

#include "erhe_graphics/acceleration_structure.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/shader_resource.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace erhe::graphics {
    class Command_buffer;
    class Compute_command_encoder;
    class Device;
    class Ring_buffer_client;
}
namespace erhe::primitive {
    class Buffer_mesh;
    class Primitive;
}
namespace erhe::scene {
    class Mesh_layer;
}
namespace erhe::scene_renderer {
    class Mesh_memory;
}

namespace editor {

// Shared GPU acceleration structure builder for the ray-query consumers
// (Ray_trace_renderer, DDGI probe tracing - doc/ddgi-plan.md phase 1).
//
// Owns three things the consumers used to each own a copy of:
//  - a bottom level structure cache, one entry per unique Buffer_mesh,
//    built lazily and read in place from the Mesh_memory vertex / index
//    pools (non-skinned meshes only),
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
        uint32_t position_stride_uints;
        uint32_t material_index;
        uint32_t flags;                 // bit 0: transmissive material
        // Pads the struct to a multiple of 16 so the std140-rounded
        // Shader_resource size matches sizeof (the layout VERIFYs in the
        // constructor compare the two).
        uint32_t reserved0;
        uint32_t reserved1;
    };
    static_assert(sizeof(Instance_record_data) == 48);

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
        erhe::graphics::Device&            graphics_device,
        erhe::scene_renderer::Mesh_memory& mesh_memory,
        unsigned int                       instance_record_binding_point,
        const std::string&                 debug_label
    );
    ~Scene_tlas() noexcept;

    // Shader_resource declarations to hand to Shader_stages_create_info.
    [[nodiscard]] auto get_instance_struct() -> erhe::graphics::Shader_resource&;
    [[nodiscard]] auto get_instance_block () -> erhe::graphics::Shader_resource&;

    // Instance count of the most recent update() (diagnostics / MCP).
    [[nodiscard]] auto get_instance_count() const -> std::size_t;

    // Cached bottom level structure count, for memory reporting
    // (doc/reloadable-asset-loads.md). Each entry pins its Primitive, and
    // through it the GPU vertex / index ranges, so this is the figure that
    // must drop when imported content is released.
    [[nodiscard]] auto get_blas_count() const -> std::size_t;

    // Builds any missing bottom level structures into command_buffer,
    // gathers the visible non-skinned content mesh primitives into this
    // frame's top level structure, and uploads their records. Must be
    // called outside a render pass. Returns an invalid Frame when there is
    // nothing to trace against.
    [[nodiscard]] auto update(
        erhe::graphics::Command_buffer&  command_buffer,
        const erhe::scene::Mesh_layer&   content_layer
    ) -> Frame;

    void bind_instance_records(erhe::graphics::Compute_command_encoder& encoder, const Frame& frame);

private:
    class Blas_entry
    {
    public:
        // Pins the GPU vertex/index ranges the structure references.
        std::shared_ptr<erhe::primitive::Primitive>             primitive;
        std::unique_ptr<erhe::graphics::Acceleration_structure> acceleration_structure;
        bool                                                    built{false};
    };

    // Drops cached bottom level structures whose primitive nothing else refers to.
//
    // Without this the cache is an unbounded pin: each entry holds a
    // shared_ptr<Primitive>, and through it the GPU vertex / index ranges, so
    // content removed from the scene (an undone glTF import) can never give its
    // memory back (doc/reloadable-asset-loads.md).
//
    // The refcount is tested on render_shape, not on the Primitive: Primitive's
    // copy constructor is defaulted and render_shape is a shared_ptr, so two
    // distinct Primitive objects can produce the same Buffer_mesh key, and a
    // use_count of 1 on the entry's Primitive could still have a live mesh
    // referring to the same shape. use_count() == 1 on the shape means the cache
    // is the last owner.
//
    // Erasing synchronously is safe: ~Acceleration_structure defers the device
    // destroy to frame completion, and each top level structure is built and
    // consumed inside a single update(), so no submitted frame still names an
    // evicted structure. The key cannot dangle either - it points into a by-value
    // member of the shape the entry itself keeps alive until the erase.
    void evict_unreferenced_blas();

    [[nodiscard]] auto get_or_create_blas(
        erhe::graphics::Command_buffer&                    command_buffer,
        const std::shared_ptr<erhe::primitive::Primitive>& primitive,
        const erhe::primitive::Buffer_mesh&                buffer_mesh
    ) -> erhe::graphics::Acceleration_structure*;

    erhe::graphics::Device&                             m_graphics_device;
    erhe::scene_renderer::Mesh_memory&                  m_mesh_memory;
    std::string                                         m_debug_label;
    erhe::graphics::Shader_resource                     m_instance_struct;
    erhe::graphics::Shader_resource                     m_instance_block;
    std::unique_ptr<erhe::graphics::Ring_buffer_client> m_instance_record_buffer;

    // Bottom level structure per unique Buffer_mesh. Entries are never
    // evicted (milestone limitation; stale entries for edited/deleted
    // geometry only cost memory, they drop out of the top level rebuild).
    std::unordered_map<const erhe::primitive::Buffer_mesh*, Blas_entry> m_blas_cache;

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
    std::vector<Instance_record_data>                            m_instance_records;
};

} // namespace editor
