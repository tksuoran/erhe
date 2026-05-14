// Regression test for quest_review A7: compute_vertex_format_key must
// distinguish formats that share the same (usage, usage_index) bitmask
// but differ in stream layout, stride, or element format. Before A7
// the key was a 32-bit bitmask and ignored those axes, so two formats
// that should have been distinct (and lived in separate Format_pools)
// were merged into the same key.

#include <gtest/gtest.h>

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_dataformat/vertex_format.hpp"

using erhe::dataformat::Format;
using erhe::dataformat::Vertex_attribute;
using erhe::dataformat::Vertex_attribute_usage;
using erhe::dataformat::Vertex_format;
using erhe::dataformat::Vertex_stream;
using erhe::dataformat::compute_vertex_format_key;

namespace {

[[nodiscard]] auto make_single_stream_position_format(std::size_t binding) -> Vertex_format
{
    Vertex_stream stream{binding};
    stream.emplace_back(Format::format_32_vec3_float, Vertex_attribute_usage::position, 0);
    stream.finalize_stride();
    return Vertex_format{stream};
}

} // namespace

TEST(VertexFormatKey, distinguishes_position_only_in_different_bindings)
{
    // Two formats with (position, usage_index=0) but on different
    // Vertex_stream::binding values (0 vs 1). Pre-A7 the key was a
    // uint32 bitmask of (usage, usage_index) slots and ignored
    // bindings, so these collided in Mesh_memory's format pool map.
    // The fix folds Vertex_stream::binding into the key.
    const Vertex_format a = make_single_stream_position_format(0);
    const Vertex_format b = make_single_stream_position_format(1);

    EXPECT_NE(compute_vertex_format_key(a), compute_vertex_format_key(b));
}

TEST(VertexFormatKey, distinguishes_same_attributes_split_across_streams)
{
    // Single-stream layout: position + normal interleaved at binding 0.
    Vertex_stream single{0};
    single.emplace_back(Format::format_32_vec3_float, Vertex_attribute_usage::position, 0);
    single.emplace_back(Format::format_32_vec3_float, Vertex_attribute_usage::normal,   0);
    single.finalize_stride();

    // Two-stream layout: position on binding 0, normal on binding 1.
    Vertex_stream s0{0};
    s0.emplace_back(Format::format_32_vec3_float, Vertex_attribute_usage::position, 0);
    s0.finalize_stride();
    Vertex_stream s1{1};
    s1.emplace_back(Format::format_32_vec3_float, Vertex_attribute_usage::normal,   0);
    s1.finalize_stride();

    const Vertex_format a{single};
    const Vertex_format b{s0, s1};

    EXPECT_NE(compute_vertex_format_key(a), compute_vertex_format_key(b));
}

TEST(VertexFormatKey, distinguishes_position_with_different_element_format)
{
    Vertex_stream s_float{0};
    s_float.emplace_back(Format::format_32_vec3_float, Vertex_attribute_usage::position, 0);
    s_float.finalize_stride();

    Vertex_stream s_half{0};
    s_half.emplace_back(Format::format_16_vec4_float, Vertex_attribute_usage::position, 0);
    s_half.finalize_stride();

    const Vertex_format a{s_float};
    const Vertex_format b{s_half};

    EXPECT_NE(compute_vertex_format_key(a), compute_vertex_format_key(b));
}

TEST(VertexFormatKey, equal_formats_produce_equal_keys)
{
    const Vertex_format a = make_single_stream_position_format(0);
    const Vertex_format b = make_single_stream_position_format(0);

    EXPECT_EQ(compute_vertex_format_key(a), compute_vertex_format_key(b));
}
