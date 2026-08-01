#pragma once

#include "erhe_utility/debug_label.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace erhe::graphics {

class Buffer;
class Command_buffer;
class Device;

// GPU ray tracing acceleration structures (Vulkan VK_KHR_acceleration_structure;
// the API shape maps onto Metal MTLPrimitiveAccelerationStructureDescriptor /
// MTLInstanceAccelerationStructureDescriptor). Only functional when
// Device_info::use_ray_query is true; on other backends construction succeeds
// but build() is a no-op, so callers must gate the feature on use_ray_query.

enum class Acceleration_structure_type : unsigned int {
    bottom_level = 0, // triangle geometry
    top_level    = 1  // instances of bottom level structures
};

class Acceleration_structure_triangles
{
public:
    // Positions are 3 x float32 read at vertex_byte_offset + i * vertex_byte_stride.
    // The vertex buffer needs Buffer_usage::acceleration_structure_build_input |
    // shader_device_address.
    const Buffer* vertex_buffer     {nullptr};
    std::size_t   vertex_byte_offset{0};
    std::size_t   vertex_byte_stride{0};
    std::size_t   vertex_count      {0};

    // Indices are a uint32 triangle list (index_count is a multiple of 3).
    // Same buffer usage requirements as the vertex buffer.
    const Buffer* index_buffer      {nullptr};
    std::size_t   index_byte_offset {0};
    std::size_t   index_count       {0};

    // Opaque geometry commits candidate hits without any-hit confirmation.
    bool          opaque            {true};
};

class Acceleration_structure;

class Acceleration_structure_instance
{
public:
    glm::mat4                     transform            {1.0f}; // object to world
    uint32_t                      instance_custom_index{0};    // low 24 bits, readable in shaders
    uint32_t                      mask                 {0xffu}; // low 8 bits, ANDed against the ray mask
    const Acceleration_structure* bottom_level         {nullptr};
    // When true (default, the historical behavior) the instance sets
    // VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR, making the
    // gl_RayFlagsCull*FacingTrianglesEXT ray flags no-ops against it. Set
    // false where facing culling matters (e.g. lightmap shadow rays that
    // must ignore back-facing self-hits).
    bool                          disable_facing_cull  {true};
};

class Acceleration_structure_create_info
{
public:
    Acceleration_structure_type type{Acceleration_structure_type::bottom_level};

    // bottom_level only: the geometry list is fixed at creation time.
    std::vector<Acceleration_structure_triangles> triangle_geometries{};

    // top_level only: instance capacity; build() accepts at most this many.
    uint32_t                    max_instance_count{0};

    erhe::utility::Debug_label  debug_label{};
};

class Acceleration_structure_impl;

class Acceleration_structure final
{
public:
    Acceleration_structure(Device& device, const Acceleration_structure_create_info& create_info);
    ~Acceleration_structure() noexcept;
    Acceleration_structure(const Acceleration_structure&) = delete;
    void operator=         (const Acceleration_structure&) = delete;
    Acceleration_structure(Acceleration_structure&& other) noexcept;
    auto operator=         (Acceleration_structure&& other) noexcept -> Acceleration_structure&;

    // bottom_level: records the GPU build into the command buffer. The vertex /
    // index buffer contents must be resident before the recorded commands
    // execute. The build ends with a barrier making the structure visible to
    // subsequent builds (top level reading this bottom level) and ray queries.
    void build(Command_buffer& command_buffer);

    // top_level: writes the instance array (host visible staging owned by the
    // structure) and records the GPU build. instances.size() must not exceed
    // max_instance_count. Rebuilding a structure that a still-in-flight frame
    // reads is a data race; keep one top level structure per frame-in-flight
    // slot.
    void build(Command_buffer& command_buffer, std::span<const Acceleration_structure_instance> instances);

    [[nodiscard]] auto get_type       () const -> Acceleration_structure_type;
    [[nodiscard]] auto get_debug_label() const -> erhe::utility::Debug_label;
    [[nodiscard]] auto get_impl       () -> Acceleration_structure_impl&;
    [[nodiscard]] auto get_impl       () const -> const Acceleration_structure_impl&;

private:
    std::unique_ptr<Acceleration_structure_impl> m_impl;
};

} // namespace erhe::graphics
