// Vertex_stream_packing: the backend-minimum hook that vertex position
// quantization needs (doc/erhe/vertex_position_quantization.md 1.1). The default
// packing must be exactly no constraint, so that building the hook is inert
// until a backend actually sets a minimum.

#include <gtest/gtest.h>

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_dataformat/vertex_format.hpp"

using erhe::dataformat::Format;
using erhe::dataformat::Vertex_attribute;
using erhe::dataformat::Vertex_attribute_usage;
using erhe::dataformat::Vertex_format;
using erhe::dataformat::Vertex_stream;
using erhe::dataformat::Vertex_stream_packing;

namespace {

// The Mesh_memory content stream-0 layouts as mesh_memory.cpp declares them
// (stream 0 is the one quantization touches), plus one fat stream.
[[nodiscard]] auto make_skinned_stream0() -> Vertex_stream
{
    return Vertex_stream{
        0,
        {
            { Format::format_32_vec3_float, Vertex_attribute_usage::position,      0},
            { Format::format_8_vec4_uint,   Vertex_attribute_usage::joint_indices, 0},
            { Format::format_8_vec4_unorm,  Vertex_attribute_usage::joint_weights, 0}
        }
    };
}

[[nodiscard]] auto make_not_skinned_stream0() -> Vertex_stream
{
    return Vertex_stream{
        0,
        {
            { Format::format_32_vec3_float, Vertex_attribute_usage::position, 0}
        }
    };
}

[[nodiscard]] auto make_quantized_skinned_stream0() -> Vertex_stream
{
    return Vertex_stream{
        0,
        {
            { Format::format_16_vec3_snorm, Vertex_attribute_usage::position,      0},
            { Format::format_8_vec4_uint,   Vertex_attribute_usage::joint_indices, 0},
            { Format::format_8_vec4_unorm,  Vertex_attribute_usage::joint_weights, 0}
        }
    };
}

[[nodiscard]] auto make_quantized_not_skinned_stream0() -> Vertex_stream
{
    return Vertex_stream{
        0,
        {
            { Format::format_16_vec3_snorm, Vertex_attribute_usage::position, 0}
        }
    };
}

[[nodiscard]] auto make_fat_stream() -> Vertex_stream
{
    return Vertex_stream{
        1,
        {
            { Format::format_32_vec3_float, Vertex_attribute_usage::normal,    0},
            { Format::format_32_vec4_float, Vertex_attribute_usage::tangent,   0},
            { Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 0},
            { Format::format_8_vec2_unorm,  Vertex_attribute_usage::custom,    1},
            { Format::format_16_vec2_uint,  Vertex_attribute_usage::custom,    2}
        }
    };
}

} // namespace

TEST(VertexStreamPacking, default_packing_reproduces_constructor_layout)
{
    // repack() with the default (all-1) packing must be a no-op on every stream:
    // same stride, same max_alignment, same attribute offsets. That is what makes
    // building the hook safe before any backend sets a minimum.
    const Vertex_stream streams[] = {
        make_skinned_stream0(),
        make_not_skinned_stream0(),
        make_fat_stream()
    };
    for (const Vertex_stream& original : streams) {
        Vertex_stream repacked = original;
        repacked.repack(Vertex_stream_packing{});
        EXPECT_EQ(repacked.stride,        original.stride);
        EXPECT_EQ(repacked.max_alignment, original.max_alignment);
        ASSERT_EQ(repacked.attributes.size(), original.attributes.size());
        for (std::size_t i = 0; i < original.attributes.size(); ++i) {
            EXPECT_EQ(repacked.attributes[i].offset, original.attributes[i].offset);
        }
        EXPECT_EQ(repacked, original);
    }
}

TEST(VertexStreamPacking, default_packing_matches_emplace_back_plus_finalize)
{
    // The other construction path: emplace_back() x N + finalize_stride(). repack()
    // always finalizes, so it must agree with the finalized stream - and the
    // trailing pad is the one place it can differ from an *un*finalized one.
    Vertex_stream built{0};
    built.emplace_back(Format::format_32_vec3_float, Vertex_attribute_usage::position, 0);
    built.emplace_back(Format::format_8_vec2_unorm,  Vertex_attribute_usage::custom,   1);
    const std::size_t unfinalized_stride = built.stride;
    built.finalize_stride();

    Vertex_stream repacked = built;
    repacked.repack(Vertex_stream_packing{});
    EXPECT_EQ(repacked.stride, built.stride);
    EXPECT_EQ(repacked, built);

    // ...and this is the case the header comment warns about: 14 unfinalized,
    // 16 once the trailing pad to max_alignment is applied.
    EXPECT_EQ(unfinalized_stride, 14u);
    EXPECT_EQ(built.stride,       16u);
}

TEST(VertexStreamPacking, default_packing_gives_todays_content_strides)
{
    // The float3 position layouts, spelled out: these are the numbers the
    // quantization plan is measured against.
    EXPECT_EQ(make_not_skinned_stream0().stride, 12u);
    EXPECT_EQ(make_skinned_stream0().stride,     20u);
}

TEST(VertexStreamPacking, quantized_stream0_packs_without_padding)
{
    // With a snorm16x3 position the strides become 6 and 14, and the skinned
    // joint attributes move to offsets 6 and 10. Neither stride is a multiple of
    // 4 - which is exactly why the packing hook exists.
    EXPECT_EQ(make_quantized_not_skinned_stream0().stride, 6u);

    const Vertex_stream skinned = make_quantized_skinned_stream0();
    EXPECT_EQ(skinned.stride,               14u);
    EXPECT_EQ(skinned.attributes[0].offset,  0u);
    EXPECT_EQ(skinned.attributes[1].offset,  6u);
    EXPECT_EQ(skinned.attributes[2].offset, 10u);
}

TEST(VertexStreamPacking, stride_alignment_pads_quantized_stream0)
{
    // A backend that requires a stride multiple of 4 (Metal) pads 6 -> 8 and
    // 14 -> 16, without moving any attribute.
    const Vertex_stream_packing metal{.min_attribute_alignment = 1, .min_stride_alignment = 4};

    Vertex_stream not_skinned = make_quantized_not_skinned_stream0();
    not_skinned.repack(metal);
    EXPECT_EQ(not_skinned.stride, 8u);

    Vertex_stream skinned = make_quantized_skinned_stream0();
    skinned.repack(metal);
    EXPECT_EQ(skinned.stride,               16u);
    EXPECT_EQ(skinned.attributes[1].offset,  6u);
    EXPECT_EQ(skinned.attributes[2].offset, 10u);
}

TEST(VertexStreamPacking, attribute_alignment_moves_offsets_and_stride)
{
    // A backend that also constrains attribute offsets to 4 pushes the joint
    // attributes off 6 / 10 and onto 8 / 12.
    const Vertex_stream_packing strict{.min_attribute_alignment = 4, .min_stride_alignment = 1};

    Vertex_stream skinned = make_quantized_skinned_stream0();
    skinned.repack(strict);
    EXPECT_EQ(skinned.attributes[0].offset,  0u);
    EXPECT_EQ(skinned.attributes[1].offset,  8u);
    EXPECT_EQ(skinned.attributes[2].offset, 12u);
    EXPECT_EQ(skinned.stride,               16u);
}

TEST(VertexStreamPacking, repack_is_idempotent)
{
    const Vertex_stream_packing metal{.min_attribute_alignment = 1, .min_stride_alignment = 4};
    Vertex_stream stream = make_quantized_skinned_stream0();
    stream.repack(metal);
    const Vertex_stream once = stream;
    stream.repack(metal);
    EXPECT_EQ(stream, once);
}

TEST(VertexStreamPacking, format_repack_covers_every_stream)
{
    Vertex_format format{make_not_skinned_stream0(), make_fat_stream()};
    const Vertex_format original = format;
    format.repack(Vertex_stream_packing{});
    EXPECT_EQ(format, original);
    EXPECT_EQ(format.get_hash(), original.get_hash());
}
