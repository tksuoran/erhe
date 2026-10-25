// get_vertex_format_signature(): the canonical layout text the
// ERHE_gpu_vertex_streams glTF extension stores and compares
// (doc/gltf_extensions/ERHE_gpu_vertex_streams.md "Signature").

#include <gtest/gtest.h>

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_dataformat/vertex_format.hpp"

using erhe::dataformat::Format;
using erhe::dataformat::Vertex_attribute_usage;
using erhe::dataformat::Vertex_format;
using erhe::dataformat::Vertex_stream;
using erhe::dataformat::Vertex_stream_packing;

namespace {

[[nodiscard]] auto make_optimized_like_format(const Format position_format) -> Vertex_format
{
    Vertex_format format{
        {
            {
                0,
                {
                    {position_format, Vertex_attribute_usage::position, 0}
                }
            },
            {
                1,
                {
                    {Format::format_16_vec4_sint,  Vertex_attribute_usage::tangent,   0},
                    {Format::format_16_vec2_unorm, Vertex_attribute_usage::tex_coord, 0},
                    {Format::format_8_vec4_unorm,  Vertex_attribute_usage::color,     0}
                }
            }
        }
    };
    format.repack(Vertex_stream_packing{.min_attribute_alignment = 1, .min_stride_alignment = 4});
    return format;
}

} // namespace

TEST(Vertex_format_signature, names_streams_attributes_formats_and_offsets)
{
    const Vertex_format format = make_optimized_like_format(Format::format_16_vec3_snorm);
    EXPECT_EQ(
        erhe::dataformat::get_vertex_format_signature(format),
        "s0=8:position.0=16_vec3_snorm@0|s1=16:tangent.0=16_vec4_sint@0,tex_coord.0=16_vec2_unorm@8,color.0=8_vec4_unorm@12"
    );
}

TEST(Vertex_format_signature, differs_when_the_layout_differs)
{
    const std::string quantized = erhe::dataformat::get_vertex_format_signature(make_optimized_like_format(Format::format_16_vec3_snorm));
    const std::string floats    = erhe::dataformat::get_vertex_format_signature(make_optimized_like_format(Format::format_32_vec3_float));
    EXPECT_NE(quantized, floats);
    EXPECT_EQ(quantized, erhe::dataformat::get_vertex_format_signature(make_optimized_like_format(Format::format_16_vec3_snorm)));
}
