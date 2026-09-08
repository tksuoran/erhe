#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/buffer_pool.hpp"
#include "erhe_scene_renderer/generated/mesh_memory_config.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"

#include "erhe_scene/mesh.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_primitive/buffer_writer.hpp"
#include "erhe_primitive/primitive.hpp"

#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>

namespace erhe::scene_renderer {

using Format                 = erhe::dataformat::Format;
using Vertex_attribute_usage = erhe::dataformat::Vertex_attribute_usage;

namespace {

// Storage format of the stream-0 position for the two OPTIMIZED vertex formats.
// The content (base) formats always store float3: they are the always-present,
// always-renderable variant that in-place GPU edits write through, and a float
// position can express any value - no AABB clamp, ever (meshoptimizer doc,
// requirement 9). Quantization, when requested and supported, lives only in the
// optimized variant (requirement 10).
//
// No acceleration-structure gate: every BLAS source is pinned to the original
// variant (scene TLAS, lightmap baker), which is float3 by construction, so a
// quantized optimized format can never reach an acceleration structure build.
// get_blas_position_input() still answers per Buffer_mesh and returns early on a
// passthrough format.
[[nodiscard]] auto choose_optimized_position_format(
    const Mesh_memory_config&     mesh_memory_config,
    const erhe::graphics::Device& graphics_device
) -> erhe::dataformat::Format
{
    if (!mesh_memory_config.quantize_vertex_positions) {
        return erhe::dataformat::Format::format_32_vec3_float;
    }
    const erhe::graphics::Device_info& device_info = graphics_device.get_info();
    if (!device_info.use_16_vec3_snorm_vertex_buffer) {
        return erhe::dataformat::Format::format_32_vec3_float;
    }
    return erhe::dataformat::Format::format_16_vec3_snorm;
}

} // anonymous namespace

auto get_blas_position_input(
    const erhe::graphics::Device&       graphics_device,
    const Mesh_memory&                  mesh_memory,
    const erhe::primitive::Buffer_mesh& buffer_mesh
) -> Blas_position_input
{
    const erhe::dataformat::Vertex_format& vertex_format =
        mesh_memory.get_vertex_input(buffer_mesh.vertex_input_key).vertex_format;
    const erhe::dataformat::Attribute_stream position =
        vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::position, 0);
    if (position.attribute == nullptr) {
        // No position at all: nothing to build from. Distinct from "the device
        // cannot use this format", which is what has_position reports.
        return Blas_position_input{.has_position = false};
    }

    Blas_position_input result{};
    result.vertex_format = position.attribute->format;
    if (erhe::dataformat::get_vertex_position_encoding(&vertex_format) == erhe::dataformat::Vertex_position_encoding::passthrough) {
        return result;
    }

    // Quantized. Vulkan does not guarantee the 3-component 16-bit snorm format as
    // acceleration structure build input - the desktop AMD 890M rejects it - but it
    // DOES mandate the 4-component one. Stream 0 is padded to a stride of 8 (or 16
    // skinned), which is a whole number of int16 lanes, so the very same buffer can
    // be read as snorm16x4 in place: an acceleration structure build takes xyz from
    // the position format and ignores any further component. No second copy of the
    // positions is needed, and the build transform below dequantizes either way.
    //
    // Reading it as snorm16x4 needs the whole 8 byte read to stay inside the vertex,
    // which is why the position has to sit at offset 0 (both BLAS builders assume
    // that anyway, see the doc) and the stride has to be at least 8. The stride
    // minimum below is the backend's own (Vulkan 1, Metal 12).
    const erhe::graphics::Device_info& device_info = graphics_device.get_info();
    const erhe::dataformat::Vertex_stream* const stream = position.stream;
    const std::size_t stride = (stream != nullptr) ? stream->stride : 0;
    const bool stride_ok =
        (stream != nullptr) &&
        (stride >= device_info.min_acceleration_structure_vertex_stride);
    const bool vec4_in_place_fits =
        stride_ok &&
        (position.attribute->offset == 0) &&
        (stride >= 8) &&
        ((stride % 2) == 0);
    if (device_info.use_16_vec3_snorm_acceleration_structure_vertex_buffer && stride_ok) {
        result.supported = true;
    } else if (device_info.use_16_vec4_snorm_acceleration_structure_vertex_buffer && vec4_in_place_fits) {
        result.vertex_format = erhe::dataformat::Format::format_16_vec4_snorm;
        result.supported     = true;
    } else {
        result.supported = false;
    }

    // The same affine the primitive record and the instance records carry, as a
    // build transform: translate(center) * scale(half_extent).
    const Position_quantization quantization = get_position_quantization(buffer_mesh.bounding_box);
    result.transform = glm::mat4{
        glm::vec4{quantization.scale.x, 0.0f, 0.0f, 0.0f},
        glm::vec4{0.0f, quantization.scale.y, 0.0f, 0.0f},
        glm::vec4{0.0f, 0.0f, quantization.scale.z, 0.0f},
        glm::vec4{quantization.offset.x, quantization.offset.y, quantization.offset.z, 1.0f}
    };
    return result;
}

Mesh_memory::Mesh_memory(
    const Mesh_memory_config& mesh_memory_config,
    erhe::graphics::Device&   graphics_device
)
    : optimized_position_format{choose_optimized_position_format(mesh_memory_config, graphics_device)}
    , vertex_format_empty{}
    , vertex_format_skinned{
        erhe::dataformat::Vertex_format{
            {
                0,
                {
                    { Format::format_32_vec3_float, Vertex_attribute_usage::position,      0},
                    { Format::format_8_vec4_uint,   Vertex_attribute_usage::joint_indices, 0},
                    { Format::format_8_vec4_unorm,  Vertex_attribute_usage::joint_weights, 0}
                }
            },
            {
                1,
                {
                    { Format::format_32_vec3_float, Vertex_attribute_usage::normal,    erhe::dataformat::normal_attribute},
                    { Format::format_32_vec4_float, Vertex_attribute_usage::tangent,   0},
                    { Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 0},
                    { Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 1},
                    { Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 2}, // lightmap UVs
                    { Format::format_32_vec4_float, Vertex_attribute_usage::color,     0},
                }
            },
            {
                2,
                {
                    { Format::format_32_vec3_float, Vertex_attribute_usage::normal, erhe::dataformat::normal_attribute_smooth}, // wireframe bias requires smooth normal attribute
                    { Format::format_8_vec2_unorm,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_aniso_control},
                    { Format::format_16_vec2_uint,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_valency_edge_count},
                    { Format::format_8_vec4_unorm,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_id}
                }
            }
        }
    }
    , vertex_format_not_skinned{
        erhe::dataformat::Vertex_format{
            {
                0,
                {
                    { Format::format_32_vec3_float, Vertex_attribute_usage::position, 0}
                }
            },
            {
                1,
                {
                    { Format::format_32_vec3_float, Vertex_attribute_usage::normal,    erhe::dataformat::normal_attribute},
                    { Format::format_32_vec4_float, Vertex_attribute_usage::tangent,   0},
                    { Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 0},
                    { Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 1},
                    { Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 2}, // lightmap UVs
                    { Format::format_32_vec4_float, Vertex_attribute_usage::color,     0},
                }
            },
            {
                2,
                {
                    { Format::format_32_vec3_float, Vertex_attribute_usage::normal, erhe::dataformat::normal_attribute_smooth},
                    { Format::format_8_vec2_unorm,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_aniso_control},
                    { Format::format_16_vec2_uint,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_valency_edge_count},
                    { Format::format_8_vec4_unorm,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_id}
                }
            }
        }
    }
    , vertex_format_not_skinned_wireframe{
        erhe::dataformat::Vertex_format{
            {
                0,
                {
                    { Format::format_32_vec3_float, Vertex_attribute_usage::position, 0}
                }
            },
            {
                1,
                {
                    { Format::format_32_vec3_float, Vertex_attribute_usage::normal,    erhe::dataformat::normal_attribute},
                    { Format::format_32_vec4_float, Vertex_attribute_usage::tangent,   0},
                    { Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 0},
                    { Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 1},
                    { Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 2}, // lightmap UVs
                    { Format::format_32_vec4_float, Vertex_attribute_usage::color,     0},
                }
            },
            {
                2,
                {
                    { Format::format_32_vec3_float, Vertex_attribute_usage::normal, erhe::dataformat::normal_attribute_smooth},
                    { Format::format_8_vec2_unorm,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_aniso_control},
                    { Format::format_16_vec2_uint,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_valency_edge_count},
                    { Format::format_8_vec4_unorm,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_id}
                }
            },
            {
                3,
                {
                    { Format::format_32_scalar_uint, Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_wireframe}
                }
            }
        }
    }
    , vertex_format_skinned_wireframe{
        erhe::dataformat::Vertex_format{
            {
                0,
                {
                    { Format::format_32_vec3_float, Vertex_attribute_usage::position,      0},
                    { Format::format_8_vec4_uint,   Vertex_attribute_usage::joint_indices, 0},
                    { Format::format_8_vec4_unorm,  Vertex_attribute_usage::joint_weights, 0}
                }
            },
            {
                1,
                {
                    { Format::format_32_vec3_float, Vertex_attribute_usage::normal,    erhe::dataformat::normal_attribute},
                    { Format::format_32_vec4_float, Vertex_attribute_usage::tangent,   0},
                    { Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 0},
                    { Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 1},
                    { Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 2}, // lightmap UVs
                    { Format::format_32_vec4_float, Vertex_attribute_usage::color,     0},
                }
            },
            {
                2,
                {
                    { Format::format_32_vec3_float, Vertex_attribute_usage::normal, erhe::dataformat::normal_attribute_smooth},
                    { Format::format_8_vec2_unorm,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_aniso_control},
                    { Format::format_16_vec2_uint,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_valency_edge_count},
                    { Format::format_8_vec4_unorm,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_id}
                }
            },
            {
                3,
                {
                    { Format::format_32_scalar_uint, Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_wireframe}
                }
            }
        }
    }
    , vertex_format_edge_line{
        erhe::dataformat::Vertex_format{
            {
                0,
                {
                    { Format::format_32_vec4_float, Vertex_attribute_usage::position, 0},
                    { Format::format_32_vec4_float, Vertex_attribute_usage::normal,   erhe::dataformat::normal_attribute_smooth}
                }
            }
        }
    }
    , vertex_format_edge_line_joints{
        erhe::dataformat::Vertex_format{
            {
                0,
                {
                    { Format::format_32_vec4_uint,  Vertex_attribute_usage::joint_indices, 0},
                    { Format::format_32_vec4_float, Vertex_attribute_usage::joint_weights, 0}
                }
            }
        }
    }
    , m_mesh_memory_config   {mesh_memory_config}
    , m_graphics_device      {graphics_device}
    , m_buffer_transfer_queue{graphics_device}
    , m_loader_transfer_queue{graphics_device}
    , m_loader_sink          {*this}
    , m_alive_token          {std::make_shared<int>(0)}
{
    // Say which position encoding the optimized variant runs on, and why - a
    // silently declined quantization request would otherwise look like the
    // feature simply not working. The content (base) formats are always float3.
    if (mesh_memory_config.quantize_vertex_positions && (optimized_position_format != erhe::dataformat::Format::format_16_vec3_snorm)) {
        const erhe::graphics::Device_info& device_info = graphics_device.get_info();
        log_startup->warn(
            "Vertex position quantization requested but declined: the device reports "
            "format_16_vec3_snorm as vertex buffer input = {}. "
            "Optimized-variant positions stay unquantized (float3).",
            device_info.use_16_vec3_snorm_vertex_buffer
        );
    } else {
        log_startup->info(
            "Vertex position storage: base variant float3, optimized variant {}",
            (optimized_position_format == erhe::dataformat::Format::format_16_vec3_snorm)
                ? "snorm16x3 quantized into the primitive AABB"
                : "float3"
        );
    }

    // Apply the backend packing minimums before anything reads a stride. Both
    // default to 1 (no constraint), in which case this reproduces the layout the
    // constructors above already produced, byte for byte. It has to happen here
    // rather than at first use: Buffer_pool captures the stride when it creates a
    // pool, and every byte_offset / stride computation depends on it, so a stride
    // changed after an allocation would silently mismatch the pool.
    const erhe::graphics::Device_info& device_info = graphics_device.get_info();
    // Same shape as the position-quantization note above: a declined weight
    // encoding would otherwise be invisible.
    if (!device_info.use_16_vec3_unorm_vertex_buffer) {
        log_startup->warn(
            "The device does not report unorm16x3 as a vertex buffer format: optimized-variant "
            "skinning weights stay at the content format's unorm8x4 (passthrough encoding)."
        );
    }
    // Mesh_memory requires a stride that is a multiple of 4 on top of whatever the
    // backend asks for, for two reasons of its own:
    //
    // - Buffer_pool aligns each allocation to its own stream stride, and the
    //   lockstep invariant needs byte_offset / stride to advance identically across
    //   streams with different strides. That holds when every stream pads by whole
    //   elements, which it does - but only a 4-byte-multiple stride ALSO keeps the
    //   range start 4-byte aligned.
    // - The ray-tracing instance records hand the stream-0 range address to a GLSL
    //   buffer reference declared buffer_reference_align = 4 and index it as a uint
    //   array. A range starting at 2 mod 4 would read every position 2 bytes off.
    //
    // The content formats are all-float, so every stride is already a multiple of
    // 4 and this is inert for them; a quantized optimized format's stream 0
    // becomes 8 / 16 rather than 6 / 14.
    const erhe::dataformat::Vertex_stream_packing packing{
        .min_attribute_alignment = device_info.min_vertex_attribute_alignment,
        .min_stride_alignment    = std::max(device_info.min_vertex_stream_stride_alignment, std::size_t{4})
    };
    // The optimized content formats are the content formats with a per-attribute
    // rule applied: some attributes are DROPPED outright, and some are stored in
    // a more compact FORMAT. Derived here rather than written out again so an
    // edit to the source format cannot leave them behind. Done BEFORE the repack
    // below, so offsets and strides are recomputed for the changed attribute
    // list. See doc/meshoptimizer-attribute-encodings-plan.md.
    //
    // Why each dropped attribute is dropped:
    //  - custom_attribute_id is per FACET, and welding merges corners across
    //    facets, so no single value describes a merged vertex. Dropping it also
    //    takes it out of the bitwise weld compare (leaving it in merges nothing
    //    at all) and makes the variant unusable for ID rendering by construction
    //    rather than by convention.
    //  - the whole normal usage: normal_attribute_smooth is read by no fill
    //    shader (the smooth normal the wide-line compute pass uses comes from
    //    the separate edge line format), and normal_attribute is folded into the
    //    TBN quaternion that replaces the tangent attribute.
    //  - custom_attribute_valency_edge_count feeds a fragment debug path, which
    //    renders from the base variant.
    // Only the normal drop is visible to Shader_key, which handles it by
    // reporting normal and tangent presence from the TBN encoding; the other two
    // have no presence axis at all, so removing them cannot collide two distinct
    // shader variants onto one key.
    const auto is_dropped = [](const erhe::dataformat::Vertex_attribute& attribute) -> bool
    {
        using usage = erhe::dataformat::Vertex_attribute_usage;
        const bool is_facet_id = (attribute.usage_type  == usage::custom) &&
                                 (attribute.usage_index == erhe::dataformat::custom_attribute_id);
        const bool is_normal   = (attribute.usage_type  == usage::normal);
        const bool is_valency  = (attribute.usage_type  == usage::custom) &&
                                 (attribute.usage_index == erhe::dataformat::custom_attribute_valency_edge_count);
        return is_facet_id || is_normal || is_valency;
    };

    // Storage format substitutions. An empty result means "keep the content
    // format". Position quantization is the ONLY place vertex positions are
    // quantized - the base formats stay float3 so in-place GPU edits can express
    // any position.
    const auto substituted_format = [this, &device_info](
        const erhe::dataformat::Vertex_attribute& attribute
    ) -> std::optional<erhe::dataformat::Format>
    {
        using usage = erhe::dataformat::Vertex_attribute_usage;
        if ((attribute.usage_type == usage::position) && (attribute.usage_index == 0)) {
            return optimized_position_format;
        }
        // Vertex colors are LDR, so unorm8 per channel: -12 bytes. vec4 in GLSL
        // either way and hardware normalized, so no decode and no shader axis.
        if (attribute.usage_type == usage::color) {
            return erhe::dataformat::Format::format_8_vec4_unorm;
        }
        // Texcoords are unorm16: -4 bytes each. Channels 0 and 1 can carry
        // tiling UVs, so they are additionally normalized into the primitive's
        // own per-channel UV range (get_texcoord_quantization(), decoded in
        // erhe_vertex_texcoord.glsl). Channel 2 is the lightmap set, in [0, 1]
        // by construction, so it needs no affine and no side data - which is
        // exactly why it must NOT pick one up: the atlas region composition
        // (primitive.lightmap_scale_offset) composes with it unchanged.
        if (attribute.usage_type == usage::tex_coord) {
            return erhe::dataformat::Format::format_16_vec2_unorm;
        }
        // The tangent attribute becomes the whole tangent frame, as a
        // quaternion: -20 bytes once the normal drop above is counted. Signed
        // rather than snorm so the component index and handedness bit in the w
        // lane stay exactly readable in GLSL.
        if (attribute.usage_type == usage::tangent) {
            return erhe::dataformat::Format::format_16_vec4_sint;
        }
        // Skinning weights sum to one, so the smallest is redundant: store three
        // and let the shader recover the fourth. Stride-neutral here (stream 0 of
        // the skinned format is 16 bytes either way), so this buys precision -
        // 16 bits per weight instead of 8, and exact normalization - rather than
        // memory. The joint indices are permuted so the dropped influence is
        // always the last.
        //
        // Only on a device that can source a vertex attribute from unorm16x3;
        // Vulkan does not guarantee VK_FORMAT_R16G16B16_UNORM as a vertex buffer
        // format. Keeping the content format instead leaves the encoding at
        // passthrough, which is what both the shader and the index permutation
        // key off, so the fallback needs nothing else.
        if (attribute.usage_type == usage::joint_weights) {
            if (!device_info.use_16_vec3_unorm_vertex_buffer) {
                return {};
            }
            return erhe::dataformat::Format::format_16_vec3_unorm;
        }
        return {};
    };

    const auto make_optimized_format = [&is_dropped, &substituted_format](
        const erhe::dataformat::Vertex_format& source
    ) -> erhe::dataformat::Vertex_format
    {
        erhe::dataformat::Vertex_format optimized = source;
        for (erhe::dataformat::Vertex_stream& stream : optimized.streams) {
            std::vector<erhe::dataformat::Vertex_attribute>& attributes = stream.attributes;
            attributes.erase(
                std::remove_if(attributes.begin(), attributes.end(), is_dropped),
                attributes.end()
            );
            // take_optimizable_snapshot() requires the optimized and content
            // formats to have equal stream counts, and an empty stream would
            // allocate a zero stride the pools cannot serve - so no stream may
            // consist entirely of dropped attributes. Stream 2 is down to
            // aniso_control alone; keep it that way rather than removing it.
            ERHE_VERIFY(!attributes.empty());
            for (erhe::dataformat::Vertex_attribute& attribute : attributes) {
                const std::optional<erhe::dataformat::Format> format = substituted_format(attribute);
                if (format.has_value()) {
                    attribute.format = format.value();
                }
            }
        }
        return optimized;
    };
    vertex_format_not_skinned_optimized = make_optimized_format(vertex_format_not_skinned);
    vertex_format_skinned_optimized     = make_optimized_format(vertex_format_skinned);

    for (erhe::dataformat::Vertex_format* format : get_all_vertex_formats()) {
        format->repack(packing);
        // Buffer_pool documents that it relies on this but cannot check it: it takes
        // any Vertex_stream, and is_compatible() is pointer identity. Check it here,
        // where the contract is actually established.
        for (const erhe::dataformat::Vertex_stream& stream : format->streams) {
            ERHE_VERIFY((stream.stride % 4) == 0);
        }
        get_vertex_input_from_vertex_format(*format);
    }
    // Every format is now registered; get_vertex_input_from_vertex_format()
    // must never take its miss path again (workers read the vector unlocked).
    m_vertex_input_entries_frozen = true;
}

auto Mesh_memory::get_all_vertex_formats() -> std::vector<erhe::dataformat::Vertex_format*>
{
    return {
        &vertex_format_empty,
        &vertex_format_skinned,
        &vertex_format_not_skinned,
        &vertex_format_not_skinned_wireframe,
        &vertex_format_skinned_wireframe,
        &vertex_format_not_skinned_optimized,
        &vertex_format_skinned_optimized,
        &vertex_format_edge_line,
        &vertex_format_edge_line_joints
    };
}

Mesh_memory::~Mesh_memory() noexcept = default;

constexpr std::size_t kilo = 1024;
constexpr std::size_t mega = 1024 * kilo;

// Pool selection rule: one Buffer_pool per Vertex_stream INSTANCE address
// (NOT per Vertex_stream byte layout). The Vertex_format objects whose
// streams we pass here -- vertex_format_skinned, vertex_format_not_skinned,
// vertex_format_edge_line, vertex_format_edge_line_joints, etc. -- are
// stable members of this Mesh_memory and own their Vertex_stream
// instances for the lifetime of the renderer, so pointer identity is a
// safe discriminator.
//
// Two distinct Vertex_formats whose stream layouts happen to be byte-
// for-byte identical (vertex_format_skinned.streams[1] and
// vertex_format_not_skinned.streams[1] both encode normal/tangent/
// texcoord/color the same way) still allocate from DIFFERENT pools.
// This duplicates a small amount of buffer memory but is required for
// correctness:
//
//   The forward renderer's multi-draw indexed indirect commands carry a
//   single `vertexOffset` scalar per draw, applied uniformly to every
//   binding. That scalar is computed from stream 0 only
//   (Buffer_mesh::base_vertex()). For the read to land on the right
//   bytes in every binding, per-mesh `byte_offset_K / stride_K` must be
//   identical for all streams K of that mesh.
//
//   If two formats with different stream-0 strides shared a pool for
//   stream 1, the shared pool would advance for BOTH formats' meshes
//   while each format's private stream-0 pool would only advance for
//   that format's meshes. The invariant would then fail for the
//   second-allocated format's meshes -- stream 0 reads at the correct
//   byte but stream 1 reads from some other mesh's data. The visible
//   symptom is correct vertex positions with garbage normals / tangents /
//   tex_coords / colors. See buffer_pool.hpp for the long-form
//   explanation.
//
// is_compatible() does the pointer check; the lookup below relies on it.
auto Mesh_memory::allocate_vertex_buffer_range(
    const erhe::dataformat::Vertex_stream& vertex_stream,
    const std::size_t                      vertex_count
) -> erhe::primitive::Buffer_sink_allocation
{
    log_mesh_memory->trace(
        "Mesh_memory::allocate_vertex_buffer_range(vertex_stream = {}, vertex_count = {}) pool_id = {}",
        vertex_stream.to_string(),
        vertex_count,
        m_vertex_pools.size()
    );
    for (Buffer_pool& pool : m_vertex_pools) {
        if (pool.is_compatible(vertex_stream)) {
            return pool.allocate(vertex_count);
        }
    }
    log_mesh_memory->trace(
        "Creating new vertex buffer pool for stream {}",
        vertex_stream.to_string()
    );
    // Lockstep block sizing: the indirect draw applies one vertexOffset
    // (computed from stream 0) to every binding, so all stream pools of a
    // Vertex_format must run out of block space at the same cumulative
    // element count -- otherwise one pool rolls over to a new block while
    // the others keep filling the old one, and byte_offset_K / stride_K
    // diverges across streams (see the lockstep invariant comment above).
    // Blocks are therefore sized in ELEMENTS, identical for every stream of
    // the owning format: elements_per_block derived from the configured byte
    // size and the format's fattest stream, then converted to bytes with
    // this stream's own stride.
    std::size_t block_size_bytes = static_cast<std::size_t>(m_mesh_memory_config.vertex_pool_block_size_mb) * mega;
    for (const erhe::dataformat::Vertex_format* format : get_all_vertex_formats()) {
        const bool owns_stream = std::any_of(
            format->streams.begin(), format->streams.end(),
            [&vertex_stream](const erhe::dataformat::Vertex_stream& stream) { return &stream == &vertex_stream; }
        );
        if (!owns_stream) {
            continue;
        }
        std::size_t max_stride = 0;
        for (const erhe::dataformat::Vertex_stream& stream : format->streams) {
            max_stride = std::max(max_stride, stream.stride);
        }
        if ((max_stride > 0) && (vertex_stream.stride > 0)) {
            const std::size_t elements_per_block = block_size_bytes / max_stride;
            block_size_bytes = elements_per_block * vertex_stream.stride;
        }
        break;
    }
    // GPU ray tracing reads vertex/index pools in place as acceleration
    // structure build input, which additionally requires the device-address
    // usage. Only valid when the bufferDeviceAddress feature was enabled
    // (VUID-VkBufferCreateInfo-usage-04ddd), hence gated on use_ray_query.
    const erhe::graphics::Buffer_usage ray_trace_usage = m_graphics_device.get_info().use_ray_query
        ? (erhe::graphics::Buffer_usage::acceleration_structure_build_input | erhe::graphics::Buffer_usage::shader_device_address)
        : erhe::graphics::Buffer_usage::none;
    Buffer_pool& new_pool = m_vertex_pools.emplace_back(
        m_graphics_device,
        m_vertex_pools.size(),
        vertex_stream,
        Buffer_pool_block_create_info{
            // `storage` is required because Content_wide_line_compute_renderer
            // reads vertex data as an SSBO in its compute pre-pass; without
            // it Vulkan validation fires VUID-VkWriteDescriptorSet-descriptorType-00331
            // ("buffer was created with VK_BUFFER_USAGE_VERTEX_BUFFER_BIT but
            // descriptorType is VK_DESCRIPTOR_TYPE_STORAGE_BUFFER").
            .usage                              = erhe::graphics::Buffer_usage::vertex | erhe::graphics::Buffer_usage::storage | ray_trace_usage,
            .required_memory_property_bit_mask  = erhe::graphics::Memory_property_flag_bit_mask::device_local,
            .preferred_memory_property_bit_mask = erhe::graphics::Memory_property_flag_bit_mask::none,
            .block_size_bytes                   = block_size_bytes,
            .max_blocks                         = static_cast<std::size_t>(m_mesh_memory_config.max_buffers_per_pool),
            .debug_label_prefix                 = fmt::format("Mesh vertex pool {}", m_vertex_pools.size())
        }
    );
    return new_pool.allocate(vertex_count);
}

void Mesh_memory::enqueue_vertex_data_to(
    erhe::graphics::Buffer_transfer_queue& queue,
    const erhe::primitive::Buffer_range&   buffer_range,
    std::vector<uint8_t>&&                 data
)
{
    log_mesh_memory->trace(
        "Enqueue vertex data for pool_id = {}, buffer_id = {}, byte_offset = {}, byte_count = {}",
        buffer_range.pool_id,
        buffer_range.buffer_id,
        buffer_range.byte_offset,
        data.size()
    );
    const Buffer_pool&      buffer_pool = m_vertex_pools.at(buffer_range.pool_id);
    erhe::graphics::Buffer* buffer      = buffer_pool.get_buffer(buffer_range.buffer_id);
    static_cast<void>(queue.enqueue(buffer, buffer_range.byte_offset, std::move(data)));
}

void Mesh_memory::enqueue_vertex_data(const erhe::primitive::Buffer_range& buffer_range, std::vector<uint8_t>&& data)
{
    enqueue_vertex_data_to(m_buffer_transfer_queue, buffer_range, std::move(data));
}

void Mesh_memory::vertex_writer_ready_to(
    erhe::graphics::Buffer_transfer_queue& queue,
    erhe::primitive::Vertex_buffer_writer& writer
)
{
    log_mesh_memory->trace(
        "Vertex buffer writer ready for pool_id = {}, buffer_id = {}, byte_offset = {}, byte_count = {}",
        writer.buffer_range.pool_id,
        writer.buffer_range.buffer_id,
        writer.start_offset(),
        writer.vertex_data.size()
    );
    const Buffer_pool&      buffer_pool = m_vertex_pools.at(writer.buffer_range.pool_id);
    erhe::graphics::Buffer* buffer      = buffer_pool.get_buffer(writer.buffer_range.buffer_id);
    static_cast<void>(queue.enqueue(buffer, writer.start_offset(), std::move(writer.vertex_data)));
}

void Mesh_memory::vertex_writer_ready(erhe::primitive::Vertex_buffer_writer& writer)
{
    vertex_writer_ready_to(m_buffer_transfer_queue, writer);
}

auto Mesh_memory::allocate_index_buffer_range(
    const erhe::dataformat::Format index_format,
    const std::size_t              index_count
) -> erhe::primitive::Buffer_sink_allocation
{
    log_mesh_memory->trace(
        "Mesh_memory::allocate_index_buffer_range(index_format = {}, index_count = {}) pool_id = {}",
        erhe::dataformat::c_str(index_format),
        index_count,
        m_index_pools.size()
    );
    for (Buffer_pool& pool : m_index_pools) {
        if (pool.is_compatible(index_format)) {
            return pool.allocate(index_count);
        }
    }
    // See the vertex pool above: build-input + device-address usage for GPU
    // ray tracing, gated on the feature being enabled.
    const erhe::graphics::Buffer_usage ray_trace_usage = m_graphics_device.get_info().use_ray_query
        ? (erhe::graphics::Buffer_usage::acceleration_structure_build_input | erhe::graphics::Buffer_usage::shader_device_address)
        : erhe::graphics::Buffer_usage::none;
    Buffer_pool& new_pool = m_index_pools.emplace_back(
        m_graphics_device,
        m_index_pools.size(),
        index_format,
        Buffer_pool_block_create_info{
            .usage                              = erhe::graphics::Buffer_usage::index | ray_trace_usage,
            .required_memory_property_bit_mask  = erhe::graphics::Memory_property_flag_bit_mask::device_local,
            .preferred_memory_property_bit_mask = erhe::graphics::Memory_property_flag_bit_mask::none,
            .block_size_bytes                   = static_cast<std::size_t>(m_mesh_memory_config.index_pool_block_size_mb) * mega,
            .max_blocks                         = static_cast<std::size_t>(m_mesh_memory_config.max_buffers_per_pool),
            .debug_label_prefix                 = fmt::format("Mesh index pool {}", m_index_pools.size())
        }
    );
    return new_pool.allocate(index_count);
}

void Mesh_memory::enqueue_index_data_to(
    erhe::graphics::Buffer_transfer_queue& queue,
    const erhe::primitive::Buffer_range&   buffer_range,
    std::vector<uint8_t>&&                 data
)
{
    log_mesh_memory->trace(
        "Enqueue index data for pool_id = {}, buffer_id = {}, byte_offset = {}, byte_count = {}",
        buffer_range.pool_id,
        buffer_range.buffer_id,
        buffer_range.byte_offset,
        data.size()
    );
    const Buffer_pool&      buffer_pool = m_index_pools.at(buffer_range.pool_id);
    erhe::graphics::Buffer* buffer      = buffer_pool.get_buffer(buffer_range.buffer_id);
    static_cast<void>(queue.enqueue(buffer, buffer_range.byte_offset, std::move(data)));
}

void Mesh_memory::enqueue_index_data(const erhe::primitive::Buffer_range& buffer_range, std::vector<uint8_t>&& data)
{
    enqueue_index_data_to(m_buffer_transfer_queue, buffer_range, std::move(data));
}

void Mesh_memory::index_writer_ready_to(
    erhe::graphics::Buffer_transfer_queue& queue,
    erhe::primitive::Index_buffer_writer&  writer
)
{
    log_mesh_memory->trace(
        "Index buffer writer ready for pool_id = {}, buffer_id = {}, byte_offset = {}, byte_count = {}",
        writer.buffer_range.pool_id,
        writer.buffer_range.buffer_id,
        writer.start_offset(),
        writer.index_data.size()
    );
    const Buffer_pool&      buffer_pool = m_index_pools.at(writer.buffer_range.pool_id);
    erhe::graphics::Buffer* buffer      = buffer_pool.get_buffer(writer.buffer_range.buffer_id);
    static_cast<void>(queue.enqueue(buffer, writer.start_offset(), std::move(writer.index_data)));
}

void Mesh_memory::index_writer_ready(erhe::primitive::Index_buffer_writer& writer)
{
    index_writer_ready_to(m_buffer_transfer_queue, writer);
}

// --- Loader_buffer_sink ---------------------------------------------------
//
// Allocation is identical (the pools are shared); only the enqueue target
// differs. See Mesh_memory_queue.

Mesh_memory::Loader_buffer_sink::Loader_buffer_sink(Mesh_memory& mesh_memory)
    : m_mesh_memory{mesh_memory}
{
}

auto Mesh_memory::Loader_buffer_sink::allocate_vertex_buffer_range(
    const erhe::dataformat::Vertex_stream& vertex_stream,
    const std::size_t                      vertex_count
) -> erhe::primitive::Buffer_sink_allocation
{
    return m_mesh_memory.allocate_vertex_buffer_range(vertex_stream, vertex_count);
}

void Mesh_memory::Loader_buffer_sink::enqueue_vertex_data(const erhe::primitive::Buffer_range& buffer_range, std::vector<uint8_t>&& data)
{
    m_mesh_memory.enqueue_vertex_data_to(m_mesh_memory.m_loader_transfer_queue, buffer_range, std::move(data));
}

void Mesh_memory::Loader_buffer_sink::vertex_writer_ready(erhe::primitive::Vertex_buffer_writer& writer)
{
    m_mesh_memory.vertex_writer_ready_to(m_mesh_memory.m_loader_transfer_queue, writer);
}

auto Mesh_memory::Loader_buffer_sink::allocate_index_buffer_range(
    const erhe::dataformat::Format index_format,
    const std::size_t              index_count
) -> erhe::primitive::Buffer_sink_allocation
{
    return m_mesh_memory.allocate_index_buffer_range(index_format, index_count);
}

void Mesh_memory::Loader_buffer_sink::enqueue_index_data(const erhe::primitive::Buffer_range& buffer_range, std::vector<uint8_t>&& data)
{
    m_mesh_memory.enqueue_index_data_to(m_mesh_memory.m_loader_transfer_queue, buffer_range, std::move(data));
}

void Mesh_memory::Loader_buffer_sink::index_writer_ready(erhe::primitive::Index_buffer_writer& writer)
{
    m_mesh_memory.index_writer_ready_to(m_mesh_memory.m_loader_transfer_queue, writer);
}

auto Mesh_memory::get_empty_vertex_input() -> const Vertex_input_entry&
{
    return m_vertex_input_entries.front();
}

auto Mesh_memory::get_vertex_input_from_vertex_format(const erhe::dataformat::Vertex_format& vertex_format) -> const Vertex_input_entry&
{
    size_t end = m_vertex_input_entries.size();
    for (size_t i = 0; i < end; ++i) {
        const Vertex_input_entry& entry = m_vertex_input_entries[i];
        if (entry.vertex_format == vertex_format) {
            return entry;
        }
    }

    // Workers reach this function while the main thread could in principle
    // grow the vector; the read path is lock-free only because every format
    // is pre-registered by the constructor. Enforce both halves: the miss
    // path runs only on the constructing thread, and never after the
    // constructor froze the set.
    ERHE_VERIFY(std::this_thread::get_id() == m_owner_thread_id);
    ERHE_VERIFY(!m_vertex_input_entries_frozen);

    m_vertex_input_entries.emplace_back(
        end,
        std::make_unique<erhe::graphics::Vertex_input_state>(
            m_graphics_device,
            erhe::graphics::Vertex_input_state_data::make(vertex_format)
        ),
        vertex_format
    );

    return m_vertex_input_entries.back();
}

auto Mesh_memory::get_vertex_input(size_t vertex_input_key) const -> const Vertex_input_entry&
{
    return m_vertex_input_entries.at(vertex_input_key);
}

auto Mesh_memory::get_vertex_buffer(const erhe::primitive::Buffer_range& buffer_range) -> erhe::graphics::Buffer*
{
    const Buffer_pool& buffer_pool = m_vertex_pools.at(buffer_range.pool_id);
    return buffer_pool.get_buffer(buffer_range.buffer_id);
}

auto Mesh_memory::get_vertex_buffer(const Pool_buffer_identity& buffer_identity) -> erhe::graphics::Buffer*
{
    const Buffer_pool& buffer_pool = m_vertex_pools.at(buffer_identity.pool_id);
    return buffer_pool.get_buffer(buffer_identity.buffer_id);
}

auto Mesh_memory::get_index_buffer(const erhe::primitive::Buffer_range& buffer_range) -> erhe::graphics::Buffer*
{
    const Buffer_pool& buffer_pool = m_index_pools.at(buffer_range.pool_id);
    return buffer_pool.get_buffer(buffer_range.buffer_id);
}

auto Mesh_memory::get_index_buffer(const Pool_buffer_identity& buffer_identity) -> erhe::graphics::Buffer*
{
    const Buffer_pool& buffer_pool = m_index_pools.at(buffer_identity.pool_id);
    return buffer_pool.get_buffer(buffer_identity.buffer_id);
}

auto Mesh_memory::get_vertex_stream(const Pool_buffer_identity& buffer_identity) -> erhe::dataformat::Vertex_stream
{
    const Buffer_pool& buffer_pool = m_vertex_pools.at(buffer_identity.pool_id);
    return buffer_pool.get_vertex_stream();
}

auto Mesh_memory::get_index_format(const Pool_buffer_identity& buffer_identity) -> erhe::dataformat::Format
{
    const Buffer_pool& buffer_pool = m_index_pools.at(buffer_identity.pool_id);
    return buffer_pool.get_index_format();
}

auto Mesh_memory::make_primitive_buffer_info(const Mesh_memory_queue queue) -> erhe::primitive::Buffer_info
{
    erhe::primitive::Vertex_buffer_sink& vertex_sink = (queue == Mesh_memory_queue::loader)
        ? static_cast<erhe::primitive::Vertex_buffer_sink&>(m_loader_sink)
        : static_cast<erhe::primitive::Vertex_buffer_sink&>(*this);
    erhe::primitive::Index_buffer_sink& index_sink = (queue == Mesh_memory_queue::loader)
        ? static_cast<erhe::primitive::Index_buffer_sink&>(m_loader_sink)
        : static_cast<erhe::primitive::Index_buffer_sink&>(*this);
    return erhe::primitive::Buffer_info{
        .index_type                = erhe::dataformat::Format::format_32_scalar_uint,
        .vertex_format             = vertex_format_not_skinned,
        .vertex_buffer_sink        = vertex_sink,
        .index_buffer_sink         = index_sink,
        .vertex_input_key          = get_vertex_input_from_vertex_format(vertex_format_not_skinned).key,
        .edge_line_vertex_stream   = &vertex_format_edge_line       .streams.front(),
        .edge_line_joint_stream    = &vertex_format_edge_line_joints.streams.front(),
        .expanded_vertex_format    = &vertex_format_not_skinned_wireframe,
        .expanded_vertex_input_key = get_vertex_input_from_vertex_format(vertex_format_not_skinned_wireframe).key,
        // Read from the owner's live config, so a Settings-window toggle
        // reaches every build made after it.
        .optimize_meshes            = m_mesh_memory_config.optimize_meshes,
        .optimized_vertex_format    = &vertex_format_not_skinned_optimized,
        .optimized_vertex_input_key = get_vertex_input_from_vertex_format(vertex_format_not_skinned_optimized).key
    };
}

auto Mesh_memory::make_skinned_primitive_buffer_info(const Mesh_memory_queue queue) -> erhe::primitive::Buffer_info
{
    erhe::primitive::Vertex_buffer_sink& vertex_sink = (queue == Mesh_memory_queue::loader)
        ? static_cast<erhe::primitive::Vertex_buffer_sink&>(m_loader_sink)
        : static_cast<erhe::primitive::Vertex_buffer_sink&>(*this);
    erhe::primitive::Index_buffer_sink& index_sink = (queue == Mesh_memory_queue::loader)
        ? static_cast<erhe::primitive::Index_buffer_sink&>(m_loader_sink)
        : static_cast<erhe::primitive::Index_buffer_sink&>(*this);
    return erhe::primitive::Buffer_info{
        .index_type                = erhe::dataformat::Format::format_32_scalar_uint,
        .vertex_format             = vertex_format_skinned,
        .vertex_buffer_sink        = vertex_sink,
        .index_buffer_sink         = index_sink,
        .vertex_input_key          = get_vertex_input_from_vertex_format(vertex_format_skinned).key,
        .edge_line_vertex_stream   = &vertex_format_edge_line       .streams.front(),
        .edge_line_joint_stream    = &vertex_format_edge_line_joints.streams.front(),
        .expanded_vertex_format    = &vertex_format_skinned_wireframe,
        .expanded_vertex_input_key = get_vertex_input_from_vertex_format(vertex_format_skinned_wireframe).key,
        // Read from the owner's live config, so a Settings-window toggle
        // reaches every build made after it.
        .optimize_meshes            = m_mesh_memory_config.optimize_meshes,
        .optimized_vertex_format    = &vertex_format_skinned_optimized,
        .optimized_vertex_input_key = get_vertex_input_from_vertex_format(vertex_format_skinned_optimized).key
    };
}

auto Mesh_memory::get_pool_statistics() const -> std::vector<Mesh_memory::Pool_statistics>
{
    std::vector<Pool_statistics> result;
    result.reserve(m_vertex_pools.size() + m_index_pools.size());
    for (const Buffer_pool& pool : m_vertex_pools) {
        result.push_back(Pool_statistics{
            .label         = pool.get_debug_label(),
            .is_index_pool = false,
            .statistics    = pool.get_statistics()
        });
    }
    for (const Buffer_pool& pool : m_index_pools) {
        result.push_back(Pool_statistics{
            .label         = pool.get_debug_label(),
            .is_index_pool = true,
            .statistics    = pool.get_statistics()
        });
    }
    return result;
}

auto Mesh_memory::get_loader_transfer_queue() -> erhe::graphics::Buffer_transfer_queue&
{
    return m_loader_transfer_queue;
}

auto Mesh_memory::flush_budgeted(erhe::graphics::Command_buffer& command_buffer, const std::size_t max_byte_count) -> std::size_t
{
    // ONLY the loader queue. The interactive queue keeps its full-drain
    // contract in flush() below - see Mesh_memory_queue.
    const std::size_t recorded = m_loader_transfer_queue.flush_budgeted(command_buffer, max_byte_count);
    if (recorded > 0) {
        log_mesh_memory->trace(
            "Mesh_memory::flush_budgeted(): recorded {} of {} budgeted bytes, watermark now {}",
            recorded,
            max_byte_count,
            m_loader_transfer_queue.get_watermark()
        );
    }
    return recorded;
}

void Mesh_memory::apply_ready_pending_frees()
{
    const erhe::graphics::Buffer_transfer_queue::Ticket watermark = m_loader_transfer_queue.get_watermark();

    std::vector<Pending_free> ready;
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_pending_free_mutex};
        for (auto it = m_pending_frees.begin(); it != m_pending_frees.end(); ) {
            if (it->loader_ticket <= watermark) {
                ready.push_back(std::move(*it));
                it = m_pending_frees.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (ready.empty()) {
        return;
    }
    // All pools of a vertex format must see identical alloc / free histories
    // (buffer_mesh.hpp lockstep invariant): apply each batch under the
    // allocation mutex so no builder's multi-stream allocation transaction
    // interleaves with it.
    const std::lock_guard<std::mutex> lock{erhe::primitive::buffer_mesh_allocation_mutex()};
    for (const Pending_free& pending : ready) {
        Buffer_pool::apply_retired(pending.ranges);
    }
}

void Mesh_memory::flush(erhe::graphics::Command_buffer& command_buffer)
{
    log_mesh_memory->trace("Mesh_memory::flush()");
    m_buffer_transfer_queue.flush(command_buffer);

    // Free gate (doc/async-asset-loading-plan.md 2.5): batches whose frame
    // has completed but whose loader watermark had not caught up yet.
    apply_ready_pending_frees();

    // Frame-safe frees (see Pool_block): every range retired so far had its
    // last GPU use recorded no later than the current frame (draws are only
    // recorded on this thread, and a Buffer_mesh is destroyed only after
    // its last draw was recorded), so freeing when the current frame has
    // completed cannot pull memory out from under an in-flight frame.
    std::vector<Retired_range> retired;
    for (Buffer_pool& pool : m_vertex_pools) {
        pool.collect_retired(retired);
    }
    for (Buffer_pool& pool : m_index_pools) {
        pool.collect_retired(retired);
    }
    if (retired.empty()) {
        return;
    }
    std::size_t retired_byte_count = 0;
    for (const Retired_range& range : retired) {
        retired_byte_count += range.byte_count;
    }
    log_mesh_memory->trace(
        "Mesh_memory::flush(): {} retired ranges ({} bytes) queued for free after frame {}",
        retired.size(),
        retired_byte_count,
        m_graphics_device.get_frame_index()
    );
    // Second gate, on top of frame completion: the pools are SHARED between
    // the interactive and the loader queue, so a range retired by the
    // interactive path can still be the target of a queued loader write. If
    // it were freed now, it could be re-allocated and re-enqueued while that
    // older write is still pending, and the stale write would land last.
    // Taking the loader queue's ticket high-water mark here bounds every
    // write that could possibly target these ranges: they were all enqueued
    // before this point.
    const erhe::graphics::Buffer_transfer_queue::Ticket loader_ticket = m_loader_transfer_queue.get_last_ticket();
    m_graphics_device.add_completion_handler(
        [this, alive = std::weak_ptr<int>{m_alive_token}, retired = std::move(retired), loader_ticket]() mutable
        {
            if (alive.expired()) {
                return; // Mesh_memory (and its pools) already destroyed
            }
            const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_pending_free_mutex};
            m_pending_frees.push_back(Pending_free{.loader_ticket = loader_ticket, .ranges = std::move(retired)});
        }
    );
}

///////////////////////////////////////////////////////////////////////////////

// Solid wireframe is drawn from the Buffer_mesh's expanded vertex stream(s),
// which use a distinct vertex input key (the format carries the extra wireframe
// attribute). All other modes use the normal vertex stream(s). The index buffer
// is shared between the two (the expanded indices live in the same index buffer).
// Also used by Draw_list_scene classification (draw_list_scene.cpp).
auto bucket_vertex_input_key(
    const erhe::primitive::Buffer_mesh&   buffer_mesh,
    const erhe::primitive::Primitive_mode primitive_mode
) -> std::size_t
{
    return (primitive_mode == erhe::primitive::Primitive_mode::solid_wireframe)
        ? buffer_mesh.expanded_vertex_input_key
        : buffer_mesh.vertex_input_key;
}

auto bucket_vertex_ranges(
    const erhe::primitive::Buffer_mesh&   buffer_mesh,
    const erhe::primitive::Primitive_mode primitive_mode
) -> const std::vector<erhe::primitive::Buffer_range>&
{
    return (primitive_mode == erhe::primitive::Primitive_mode::solid_wireframe)
        ? buffer_mesh.expanded_vertex_buffer_ranges
        : buffer_mesh.vertex_buffer_ranges;
}

Render_bucket::Render_bucket() = default;

Render_bucket::Render_bucket(
    erhe::scene::Mesh&                    mesh,
    const std::size_t                     mesh_primitive_index,
    const erhe::primitive::Buffer_mesh&   buffer_mesh,
    const Shader_key&                     shader_key,
    const uint64_t                        shader_key_hash,
    const bool                            negative_determinant,
    const bool                            double_sided,
    const erhe::primitive::Primitive_mode primitive_mode
)
    : shader_key          {shader_key}
    , shader_key_hash     {shader_key_hash}
    , negative_determinant{negative_determinant}
    , double_sided        {double_sided}
    , primitive_mode      {primitive_mode}
{
    buffer_set.vertex_input_key = bucket_vertex_input_key(buffer_mesh, primitive_mode);
    buffer_set.index_buffer     = Pool_buffer_identity{buffer_mesh.index_buffer_range.pool_id, buffer_mesh.index_buffer_range.buffer_id};

    for (const erhe::primitive::Buffer_range& vr : bucket_vertex_ranges(buffer_mesh, primitive_mode)) {
        buffer_set.vertex_buffers.emplace_back(vr.pool_id, vr.buffer_id);
    }

    const bool done = accept(mesh, mesh_primitive_index, buffer_mesh, shader_key_hash, negative_determinant, double_sided);
    ERHE_VERIFY(done);
}

Render_bucket::~Render_bucket() noexcept = default;

auto Render_bucket::accept(
    erhe::scene::Mesh&                  mesh,
    const std::size_t                   mesh_primitive_index,
    const erhe::primitive::Buffer_mesh& buffer_mesh,
    const uint64_t                      primitive_shader_key_hash,
    const bool                          primitive_negative_determinant,
    const bool                          primitive_double_sided
) -> bool
{
    if (primitive_negative_determinant != negative_determinant) {
        return false;
    }
    if (primitive_double_sided != double_sided) {
        return false;
    }
    const std::vector<erhe::primitive::Buffer_range>& vertex_ranges = bucket_vertex_ranges(buffer_mesh, primitive_mode);
    if (bucket_vertex_input_key(buffer_mesh, primitive_mode) != buffer_set.vertex_input_key) {
        return false;
    }
    if (Pool_buffer_identity{buffer_mesh.index_buffer_range.pool_id, buffer_mesh.index_buffer_range.buffer_id} != buffer_set.index_buffer) {
        return false;
    }
    if (vertex_ranges.size() != buffer_set.vertex_buffers.size()) {
        return false;
    }
    if (primitive_shader_key_hash != shader_key_hash) {
        return false;
    }
    for (size_t i = 0; i < buffer_set.vertex_buffers.size(); ++i) {
        if (Pool_buffer_identity{vertex_ranges[i].pool_id, vertex_ranges[i].buffer_id} != buffer_set.vertex_buffers[i]) {
            return false;
        }
    }
    entries.emplace_back(&mesh, static_cast<uint16_t>(mesh_primitive_index), &buffer_mesh);
    return true;
}

void bucket_primitives(
    std::vector<Render_bucket>&                                buckets,
    const uint32_t                                             boolean_mask_force_enable,
    const uint32_t                                             boolean_mask_force_disable,
    const Mesh_memory&                                         mesh_memory,
    const Shader_key&                                          environment_shader_key,
    const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes,
    const erhe::Item_filter&                                   filter,
    const erhe::primitive::Primitive_mode                      primitive_mode,
    const Blending_mode_policy                                 blending_mode_policy,
    const erhe::primitive::Mesh_variant                        variant_preference,
    const erhe::Item_filter&                                   shader_debug_filter,
    const bool                                                 exclude_unlit_primitives
)
{
    ERHE_PROFILE_FUNCTION();

    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : meshes) {
        const auto primitives = mesh->get_primitives();
        if (!filter(mesh->get_flag_bits())) {
            // log_draw->warn("filtered away {} filter: {}", mesh->describe(2), filter.describe());
            // static_cast<void>(filter(mesh->get_flag_bits()));
            continue;
        }

        // Mirrored world transforms reverse apparent triangle winding;
        // partition buckets by the flag so renderers can select a
        // front-face-flipped pipeline variant per bucket. The flag is
        // maintained on the Mesh item by Mesh::handle_node_transform_update().
        const bool mesh_negative_determinant = (mesh->get_flag_bits() & erhe::Item_flags::negative_determinant) != 0u;

        for (size_t i = 0, count = primitives.size(); i < count; ++i) {
            const erhe::scene::Mesh_primitive& mesh_primitive = primitives[i];
            const erhe::primitive::Primitive* primitive = mesh_primitive.primitive.get();
            if (primitive == nullptr) {
                continue;
            }
            // The variant choice is made once, here, and travels with the bucket
            // entry to the record and draw fill sites.
            if (primitive->render_shape == nullptr) {
                continue;
            }
            const erhe::primitive::Buffer_mesh* buffer_mesh =
                primitive->get_resolved_renderable_mesh(variant_preference, primitive_mode).second;
            if (buffer_mesh == nullptr) {
                continue;
            }
            if (buffer_mesh->index_range(primitive_mode).index_count == 0) {
                continue;
            }

            erhe::primitive::Material* material = mesh_primitive.material.get();

            // Unlit primitives are excluded from the shadow pass (see the
            // parameter comment): they are backdrop geometry, not occluders.
            if (
                exclude_unlit_primitives &&
                (material != nullptr) &&
                (material->get_bxdf_model() == erhe::primitive::Bxdf_model::unlit)
            ) {
                continue;
            }

            const Vertex_input_entry& vertex_input_entry = mesh_memory.get_vertex_input(buffer_mesh->vertex_input_key);

            Shader_key shader_key = environment_shader_key.derive(
                material,
                &vertex_input_entry.vertex_format,
                static_cast<bool>(mesh->skin)
            );
            // The shader-debug override visualization is a content-inspection aid; do not recolor
            // tool / brush / controller / rendertarget meshes. Drop the SHADER_DEBUG axis for any mesh
            // the caller's shader_debug_filter rejects, so it falls back to its normal shader variant.
            if (
                (environment_shader_key.get(Shader_int::SHADER_DEBUG) != 0u) &&
                !shader_debug_filter(mesh->get_flag_bits())
            ) {
                shader_key.set(Shader_int::SHADER_DEBUG, 0u);
            }
            shader_key.bool_mask |=  boolean_mask_force_enable;
            shader_key.bool_mask &= ~boolean_mask_force_disable;
            switch (blending_mode_policy) {
                case Blending_mode_policy::not_set: {
                    ERHE_FATAL("Blending_mode_policy::not_set");
                    break;
                }
                case Blending_mode_policy::opaque_primitives_only: {
                    if (shader_key.blending_mode != erhe::primitive::Material_blending_mode::opaque) {
                        continue;
                    }
                    break;
                }
                case Blending_mode_policy::translucent_primitives_only: {
                    if (shader_key.blending_mode == erhe::primitive::Material_blending_mode::opaque) {
                        continue;
                    }
                    break;
                }
                case Blending_mode_policy::allow_all: {
                    // NOP
                    break;
                }
                case Blending_mode_policy::override_with_base_render_pipeline: {
                    erhe::primitive::Material_blending_mode blend_override = environment_shader_key.blending_mode.has_value() 
                        ? environment_shader_key.blending_mode.value()
                        : erhe::primitive::Material_blending_mode::opaque;
                    if (shader_key.blending_mode.has_value()) {
                        if (shader_key.blending_mode.value() != blend_override) {
                            log_draw->warn(
                                "Overriding blending mode for {} from {} to {}",
                                mesh->describe(2),
                                erhe::primitive::c_str(shader_key.blending_mode.value()),
                                erhe::primitive::c_str(blend_override)
                            );
                        }
                    }
                    shader_key.blending_mode = blend_override;
                    break;
                }
                default: {
                    ERHE_FATAL("Bad Blending_mode_policy");
                    break;
                }
            }
            const uint64_t shader_key_hash = shader_key.get_hash();

            // glTF material.doubleSided or USD UsdGeomGprim.doubleSided,
            // through the one helper the draw lists ask as well.
            const bool primitive_double_sided = erhe::scene::is_double_sided(*mesh.get(), mesh_primitive);

            bool done = false;
            for (Render_bucket& b : buckets) {
                if (b.accept(*mesh.get(), i, *buffer_mesh, shader_key_hash, mesh_negative_determinant, primitive_double_sided)) {
                    done = true;
                    break;
                }
            }
            if (done) {
                continue;
            }

            buckets.emplace_back(*mesh.get(), i, *buffer_mesh, shader_key, shader_key_hash, mesh_negative_determinant, primitive_double_sided, primitive_mode);
        }
    }
}


}
