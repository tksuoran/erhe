#include "erhe_primitive/mesh_optimizer.hpp"

#include <geogram/basic/numeric.h>
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_dataformat/vertex_format.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

// Contract tests for optimize_triangle_soup(). The passes themselves are
// meshoptimizer's; what has to hold here is that the two remaps it hands back
// really describe the transformation, because Element_mappings for the
// optimized variant are composed through them - a wrong remap silently
// mispaints vertices and mispicks facets rather than failing.

namespace {

using erhe::dataformat::Format;
using erhe::dataformat::Vertex_attribute_usage;
using erhe::dataformat::Vertex_format;
using erhe::dataformat::Vertex_stream;
using erhe::primitive::Mesh_optimize_options;
using erhe::primitive::Mesh_optimize_result;
using erhe::primitive::Element_mappings;
using erhe::primitive::Triangle_soup;

class Test_vertex
{
public:
    float position[3]{0.0f, 0.0f, 0.0f};
    float texcoord[2]{0.0f, 0.0f};
};

[[nodiscard]] auto make_vertex_format() -> Vertex_format
{
    return Vertex_format{
        Vertex_stream{
            0,
            {
                {Format::format_32_vec3_float, Vertex_attribute_usage::position },
                {Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord}
            }
        }
    };
}

[[nodiscard]] auto make_soup(const std::vector<Test_vertex>& vertices, const std::vector<uint32_t>& indices) -> Triangle_soup
{
    Triangle_soup soup{};
    soup.vertex_format = make_vertex_format();
    soup.index_data    = indices;
    const std::size_t stride = soup.vertex_format.streams.front().stride;
    // The weld compares every byte of the vertex including padding, so the
    // buffer has to start zeroed - see the note in the plan's risk list.
    soup.vertex_data.assign(vertices.size() * stride, uint8_t{0});
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        std::memcpy(soup.vertex_data.data() + i * stride + 0,  &vertices[i].position[0], sizeof(float) * 3);
        std::memcpy(soup.vertex_data.data() + i * stride + 12, &vertices[i].texcoord[0], sizeof(float) * 2);
    }
    return soup;
}

// A grid of quads, each split into two triangles, with every triangle emitting
// its own three vertices. Neighbouring triangles therefore carry bitwise-equal
// duplicates that the weld must merge, and the triangle order is deliberately
// cache-hostile (column major) so the reordering passes have work to do.
[[nodiscard]] auto make_unwelded_grid(const int size) -> Triangle_soup
{
    std::vector<Test_vertex> vertices;
    std::vector<uint32_t>    indices;
    for (int x = 0; x < size; ++x) {
        for (int y = 0; y < size; ++y) {
            const float fx = static_cast<float>(x);
            const float fy = static_cast<float>(y);
            const Test_vertex corners[4] = {
                {{fx,        fy,        0.0f}, {fx,        fy       }},
                {{fx + 1.0f, fy,        0.0f}, {fx + 1.0f, fy       }},
                {{fx + 1.0f, fy + 1.0f, 0.0f}, {fx + 1.0f, fy + 1.0f}},
                {{fx,        fy + 1.0f, 0.0f}, {fx,        fy + 1.0f}}
            };
            const int triples[2][3] = {{0, 1, 2}, {0, 2, 3}};
            for (const int (&triple)[3] : triples) {
                for (const int corner : triple) {
                    indices.push_back(static_cast<uint32_t>(vertices.size()));
                    vertices.push_back(corners[corner]);
                }
            }
        }
    }
    return make_soup(vertices, indices);
}

[[nodiscard]] auto vertex_bytes(const Triangle_soup& soup, const std::size_t vertex) -> std::vector<uint8_t>
{
    const std::size_t stride = soup.vertex_format.streams.front().stride;
    const uint8_t* const base = soup.vertex_data.data() + vertex * stride;
    return std::vector<uint8_t>{base, base + stride};
}

} // anonymous namespace

TEST(Mesh_optimizer, welds_duplicate_vertices)
{
    const Triangle_soup soup = make_unwelded_grid(8);
    ASSERT_EQ(soup.get_vertex_count(), 8u * 8u * 6u); // one vertex per corner, nothing shared

    const Mesh_optimize_result result = optimize_triangle_soup(soup, Mesh_optimize_options{});
    ASSERT_NE(result.triangle_soup, nullptr);

    // A size x size grid of welded quads has (size+1)^2 distinct corners.
    EXPECT_EQ(result.triangle_soup->get_vertex_count(), 9u * 9u);
    EXPECT_EQ(result.statistics.vertex_count_before, soup.get_vertex_count());
    EXPECT_EQ(result.statistics.vertex_count_after, result.triangle_soup->get_vertex_count());
    // Triangles are only reordered, never added or dropped.
    EXPECT_EQ(result.triangle_soup->index_data.size(), soup.index_data.size());
}

TEST(Mesh_optimizer, the_forward_vertex_remap_lands_on_equal_vertex_data)
{
    const Triangle_soup soup = make_unwelded_grid(8);
    const Mesh_optimize_result result = optimize_triangle_soup(soup, Mesh_optimize_options{});
    ASSERT_NE(result.triangle_soup, nullptr);
    ASSERT_EQ(result.vertex_remap.size(), soup.get_vertex_count());

    for (std::size_t vertex = 0; vertex < soup.get_vertex_count(); ++vertex) {
        const uint32_t mapped = result.vertex_remap[vertex];
        ASSERT_NE(mapped, Mesh_optimize_result::no_vertex) << "vertex " << vertex << " is referenced, so it must map";
        ASSERT_LT(mapped, result.triangle_soup->get_vertex_count());
        EXPECT_EQ(vertex_bytes(soup, vertex), vertex_bytes(*result.triangle_soup, mapped))
            << "source vertex " << vertex << " -> output " << mapped;
    }
}

TEST(Mesh_optimizer, the_triangle_permutation_identifies_the_source_triangle)
{
    const Triangle_soup soup = make_unwelded_grid(8);
    const Mesh_optimize_result result = optimize_triangle_soup(soup, Mesh_optimize_options{});
    ASSERT_NE(result.triangle_soup, nullptr);

    const std::size_t triangle_count = soup.index_data.size() / 3;
    ASSERT_EQ(result.triangle_permutation.size(), triangle_count);

    // Every source triangle is named exactly once: the permutation is a
    // bijection, which is what lets triangle_to_mesh_facet compose through it.
    std::vector<int> seen(triangle_count, 0);
    for (const uint32_t source_triangle : result.triangle_permutation) {
        ASSERT_LT(source_triangle, triangle_count);
        ++seen[source_triangle];
    }
    for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
        EXPECT_EQ(seen[triangle], 1) << "source triangle " << triangle;
    }

    // And it names the RIGHT one: each output triangle's three vertices must
    // carry the same data, in the same winding order, as the source triangle it
    // claims to come from.
    for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
        const std::size_t source_triangle = result.triangle_permutation[triangle];
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const uint32_t output_vertex = result.triangle_soup->index_data[triangle * 3 + corner];
            const uint32_t source_vertex = soup.index_data[source_triangle * 3 + corner];
            EXPECT_EQ(vertex_bytes(*result.triangle_soup, output_vertex), vertex_bytes(soup, source_vertex))
                << "output triangle " << triangle << " corner " << corner;
        }
    }
}

TEST(Mesh_optimizer, the_remaps_agree_with_each_other)
{
    // The two tables have to describe ONE transformation: mapping a source
    // triangle's corner through the vertex remap must give the very index the
    // optimized soup uses for that corner. This is the composition
    // Element_mappings relies on.
    const Triangle_soup soup = make_unwelded_grid(8);
    const Mesh_optimize_result result = optimize_triangle_soup(soup, Mesh_optimize_options{});
    ASSERT_NE(result.triangle_soup, nullptr);

    for (std::size_t triangle = 0; triangle < result.triangle_permutation.size(); ++triangle) {
        const std::size_t source_triangle = result.triangle_permutation[triangle];
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const uint32_t source_vertex = soup.index_data[source_triangle * 3 + corner];
            EXPECT_EQ(result.vertex_remap[source_vertex], result.triangle_soup->index_data[triangle * 3 + corner])
                << "output triangle " << triangle << " corner " << corner;
        }
    }
}

TEST(Mesh_optimizer, reports_before_and_after_statistics)
{
    const Triangle_soup soup = make_unwelded_grid(16);
    const Mesh_optimize_result result = optimize_triangle_soup(soup, Mesh_optimize_options{});
    ASSERT_NE(result.triangle_soup, nullptr);

    // Unwelded, column-major input is close to the worst case for both metrics,
    // so both must improve rather than merely change.
    EXPECT_LT(result.statistics.acmr_after, result.statistics.acmr_before);
    EXPECT_LT(result.statistics.fetch_bytes_after, result.statistics.fetch_bytes_before);
    EXPECT_GT(result.statistics.overdraw_before, 0.0f);
    EXPECT_GT(result.statistics.overdraw_after, 0.0f);
    EXPECT_EQ(result.statistics.triangle_count, soup.index_data.size() / 3);
}

TEST(Mesh_optimizer, disabled_passes_are_a_faithful_copy)
{
    const Triangle_soup soup = make_unwelded_grid(4);
    Mesh_optimize_options options{};
    options.weld         = false;
    options.vertex_cache = false;
    options.overdraw     = false;
    options.vertex_fetch = false;

    const Mesh_optimize_result result = optimize_triangle_soup(soup, options);
    ASSERT_NE(result.triangle_soup, nullptr);
    EXPECT_EQ(result.triangle_soup->index_data, soup.index_data);
    EXPECT_EQ(result.triangle_soup->vertex_data, soup.vertex_data);
    for (std::size_t vertex = 0; vertex < soup.get_vertex_count(); ++vertex) {
        EXPECT_EQ(result.vertex_remap[vertex], static_cast<uint32_t>(vertex));
    }
    for (std::size_t triangle = 0; triangle < result.triangle_permutation.size(); ++triangle) {
        EXPECT_EQ(result.triangle_permutation[triangle], static_cast<uint32_t>(triangle));
    }
}

TEST(Mesh_optimizer, unreferenced_vertices_map_to_no_vertex)
{
    // The welding pass drops vertices no triangle references, so they have no
    // output slot. Mesh_optimize_result documents no_vertex for them; anything
    // composing Element_mappings through the remap has to carry the sentinel
    // rather than index with it, so pin that it really is produced.
    Triangle_soup soup = make_unwelded_grid(4);
    const std::size_t referenced_count = soup.get_vertex_count();
    const std::size_t stride = soup.vertex_format.streams.front().stride;
    // Append two vertices that nothing indexes, distinct from every existing one
    // so welding cannot merge them into a referenced slot.
    soup.vertex_data.resize((referenced_count + 2) * stride, uint8_t{0});
    const float far_away[3] = {1000.0f, 2000.0f, 3000.0f};
    std::memcpy(soup.vertex_data.data() + (referenced_count + 0) * stride, &far_away[0], sizeof(far_away));
    const float further[3] = {4000.0f, 5000.0f, 6000.0f};
    std::memcpy(soup.vertex_data.data() + (referenced_count + 1) * stride, &further[0], sizeof(further));
    ASSERT_EQ(soup.get_vertex_count(), referenced_count + 2);

    const Mesh_optimize_result result = optimize_triangle_soup(soup, Mesh_optimize_options{});
    ASSERT_NE(result.triangle_soup, nullptr);
    ASSERT_EQ(result.vertex_remap.size(), referenced_count + 2);

    EXPECT_EQ(result.vertex_remap[referenced_count + 0], Mesh_optimize_result::no_vertex);
    EXPECT_EQ(result.vertex_remap[referenced_count + 1], Mesh_optimize_result::no_vertex);
    // The unreferenced pair must not have made it into the output either.
    EXPECT_EQ(result.triangle_soup->get_vertex_count(), 5u * 5u);
    for (std::size_t vertex = 0; vertex < referenced_count; ++vertex) {
        EXPECT_NE(result.vertex_remap[vertex], Mesh_optimize_result::no_vertex) << "vertex " << vertex;
    }
}

TEST(Mesh_optimizer, the_remaps_agree_for_every_pass_combination)
{
    // The composition has four shapes depending on which of weld / vertex_fetch
    // ran. In particular weld off + fetch on is the only configuration where
    // the fetch remap itself can hold no_vertex.
    const Triangle_soup soup = make_unwelded_grid(4);
    for (int mask = 0; mask < 16; ++mask) {
        Mesh_optimize_options options{};
        options.weld         = ((mask & 1) != 0);
        options.vertex_cache = ((mask & 2) != 0);
        options.overdraw     = ((mask & 4) != 0);
        options.vertex_fetch = ((mask & 8) != 0);

        const Mesh_optimize_result result = optimize_triangle_soup(soup, options);
        ASSERT_NE(result.triangle_soup, nullptr) << "mask " << mask;
        ASSERT_EQ(result.triangle_permutation.size(), soup.index_data.size() / 3) << "mask " << mask;
        ASSERT_EQ(result.vertex_remap.size(), soup.get_vertex_count()) << "mask " << mask;

        for (std::size_t triangle = 0; triangle < result.triangle_permutation.size(); ++triangle) {
            const std::size_t source_triangle = result.triangle_permutation[triangle];
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const uint32_t source_vertex = soup.index_data[source_triangle * 3 + corner];
                ASSERT_NE(result.vertex_remap[source_vertex], Mesh_optimize_result::no_vertex) << "mask " << mask;
                EXPECT_EQ(result.vertex_remap[source_vertex], result.triangle_soup->index_data[triangle * 3 + corner])
                    << "mask " << mask << " triangle " << triangle << " corner " << corner;
            }
        }
    }
}

TEST(Mesh_optimizer, duplicate_triangles_still_yield_a_bijection)
{
    // Two bitwise-identical triangles collide in the triple table the
    // permutation is recovered against. Either candidate renders the same, but
    // the permutation must still name each source triangle exactly once.
    const std::vector<Test_vertex> vertices{
        {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}}, // duplicate of vertex 0
        {{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}}, // duplicate of vertex 1
        {{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}}, // duplicate of vertex 2
        {{2.0f, 2.0f, 0.0f}, {2.0f, 2.0f}}
    };
    // Triangles 0 and 1 become identical after welding; triangle 2 is distinct.
    const std::vector<uint32_t> indices{0, 1, 2, 3, 4, 5, 0, 1, 6};
    const Triangle_soup soup = make_soup(vertices, indices);

    const Mesh_optimize_result result = optimize_triangle_soup(soup, Mesh_optimize_options{});
    ASSERT_NE(result.triangle_soup, nullptr);
    ASSERT_EQ(result.triangle_permutation.size(), 3u);

    std::vector<int> seen(3, 0);
    for (const uint32_t source_triangle : result.triangle_permutation) {
        ASSERT_LT(source_triangle, 3u);
        ++seen[source_triangle];
    }
    for (std::size_t triangle = 0; triangle < 3; ++triangle) {
        EXPECT_EQ(seen[triangle], 1) << "source triangle " << triangle;
    }
}

TEST(Mesh_optimizer, refuses_input_it_cannot_describe)
{
    // Each of these must return a null soup rather than optimize something the
    // remaps would not describe, or hand meshoptimizer out-of-range indices.
    {
        Triangle_soup soup = make_unwelded_grid(2);
        soup.index_data.pop_back(); // not a whole number of triangles
        EXPECT_EQ(optimize_triangle_soup(soup, Mesh_optimize_options{}).triangle_soup, nullptr);
    }
    {
        Triangle_soup soup = make_unwelded_grid(2);
        soup.index_data[0] = static_cast<uint32_t>(soup.get_vertex_count()); // out of range
        EXPECT_EQ(optimize_triangle_soup(soup, Mesh_optimize_options{}).triangle_soup, nullptr);
    }
    {
        Triangle_soup soup = make_unwelded_grid(2);
        soup.index_data.clear();
        EXPECT_EQ(optimize_triangle_soup(soup, Mesh_optimize_options{}).triangle_soup, nullptr);
    }
    {
        Triangle_soup soup = make_unwelded_grid(2);
        soup.primitive_type = erhe::primitive::Primitive_type::lines;
        EXPECT_EQ(optimize_triangle_soup(soup, Mesh_optimize_options{}).triangle_soup, nullptr);
    }
    {
        // Multi-stream: get_vertex_count() and mesh_from_triangle_soup() both
        // assume a single stream bound at 0, so anything else must be refused
        // rather than measured against the wrong stride.
        Triangle_soup soup = make_unwelded_grid(2);
        soup.vertex_format.streams.emplace_back(1);
        EXPECT_EQ(optimize_triangle_soup(soup, Mesh_optimize_options{}).triangle_soup, nullptr);
    }
}

// Composition of Element_mappings onto the optimized order. Nothing reads the
// optimized build's mappings today - every consumer pins to `original` - so
// these tests are what keeps the composition honest until one does.

TEST(Mesh_optimizer, composed_corner_mappings_point_at_equal_vertex_data)
{
    const Triangle_soup soup = make_unwelded_grid(8);
    const Mesh_optimize_result result = optimize_triangle_soup(soup, Mesh_optimize_options{});
    ASSERT_NE(result.triangle_soup, nullptr);

    // A source mapping that names, for each corner of each triangle, the soup
    // vertex that corner uses - the shape parse_triangles() produces.
    Element_mappings source{};
    source.mesh_corner_to_vertex_buffer_index = soup.index_data;

    const Element_mappings composed = erhe::primitive::compose_element_mappings(source, result);
    ASSERT_EQ(composed.mesh_corner_to_vertex_buffer_index.size(), soup.index_data.size());

    // Each corner must now name a vertex of the OPTIMIZED soup carrying the
    // same data the source vertex did. That is the whole point: the mapping
    // has to describe the order of the build it is paired with.
    for (std::size_t corner = 0, end = soup.index_data.size(); corner < end; ++corner) {
        const uint32_t output_vertex = composed.mesh_corner_to_vertex_buffer_index[corner];
        ASSERT_NE(output_vertex, GEO::NO_INDEX) << "corner " << corner;
        ASSERT_LT(output_vertex, result.triangle_soup->get_vertex_count());
        EXPECT_EQ(vertex_bytes(*result.triangle_soup, output_vertex), vertex_bytes(soup, soup.index_data[corner]))
            << "corner " << corner;
    }
}

TEST(Mesh_optimizer, composed_facet_mappings_follow_the_triangle_permutation)
{
    const Triangle_soup soup = make_unwelded_grid(8);
    const Mesh_optimize_result result = optimize_triangle_soup(soup, Mesh_optimize_options{});
    ASSERT_NE(result.triangle_soup, nullptr);

    const std::size_t triangle_count = soup.index_data.size() / 3;

    // Give every source triangle a distinguishable facet, with one degenerate
    // NO_INDEX entry that has to survive the composition rather than be
    // turned into a real facet.
    Element_mappings source{};
    source.triangle_to_mesh_facet.resize(triangle_count);
    for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
        source.triangle_to_mesh_facet[triangle] = static_cast<uint32_t>(triangle * 7 + 1);
    }
    source.triangle_to_mesh_facet[triangle_count / 2] = GEO::NO_INDEX;

    const Element_mappings composed = erhe::primitive::compose_element_mappings(source, result);
    ASSERT_EQ(composed.triangle_to_mesh_facet.size(), triangle_count);

    std::size_t no_index_count = 0;
    for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
        const uint32_t source_triangle = result.triangle_permutation[triangle];
        EXPECT_EQ(composed.triangle_to_mesh_facet[triangle], source.triangle_to_mesh_facet[source_triangle])
            << "output triangle " << triangle;
        if (composed.triangle_to_mesh_facet[triangle] == GEO::NO_INDEX) {
            ++no_index_count;
        }
    }
    // The permutation is a bijection, so the one degenerate entry moves - it
    // does not multiply and it does not disappear.
    EXPECT_EQ(no_index_count, 1u);
}

TEST(Mesh_optimizer, composing_empty_mappings_yields_empty_mappings)
{
    // The soup import path has no mappings at all until geometry is parsed, so
    // composing must not invent tables sized after the optimization.
    const Triangle_soup soup = make_unwelded_grid(4);
    const Mesh_optimize_result result = optimize_triangle_soup(soup, Mesh_optimize_options{});
    ASSERT_NE(result.triangle_soup, nullptr);

    const Element_mappings composed = erhe::primitive::compose_element_mappings(Element_mappings{}, result);
    EXPECT_TRUE(composed.triangle_to_mesh_facet.empty());
    EXPECT_TRUE(composed.mesh_corner_to_vertex_buffer_index.empty());
    EXPECT_TRUE(composed.mesh_vertex_to_vertex_buffer_index.empty());
}

TEST(Mesh_optimizer, a_dropped_source_vertex_composes_to_no_index)
{
    // Welding drops a vertex no triangle references. A mapping that still
    // names it must come back as NO_INDEX, not as an index into nothing.
    Triangle_soup soup = make_unwelded_grid(4);
    const std::size_t stride = soup.vertex_format.streams.front().stride;
    const uint32_t unreferenced = static_cast<uint32_t>(soup.get_vertex_count());
    soup.vertex_data.resize(soup.vertex_data.size() + stride, uint8_t{0});

    const Mesh_optimize_result result = optimize_triangle_soup(soup, Mesh_optimize_options{});
    ASSERT_NE(result.triangle_soup, nullptr);
    ASSERT_EQ(result.vertex_remap[unreferenced], Mesh_optimize_result::no_vertex);

    Element_mappings source{};
    source.mesh_vertex_to_vertex_buffer_index = {unreferenced, GEO::NO_INDEX, soup.index_data.front()};

    const Element_mappings composed = erhe::primitive::compose_element_mappings(source, result);
    ASSERT_EQ(composed.mesh_vertex_to_vertex_buffer_index.size(), 3u);
    EXPECT_EQ(composed.mesh_vertex_to_vertex_buffer_index[0], GEO::NO_INDEX);
    EXPECT_EQ(composed.mesh_vertex_to_vertex_buffer_index[1], GEO::NO_INDEX);
    EXPECT_NE(composed.mesh_vertex_to_vertex_buffer_index[2], GEO::NO_INDEX);
}

// --- optimize_indexed_mesh(), the multi-stream core -----------------------
// The geometry path feeds it one stream per sink vertex stream, so what has to
// hold is that splitting the SAME bytes across streams changes nothing about
// the transformation - and that the weld still compares all of them.

namespace {

using erhe::primitive::Mesh_optimize_stream;
using erhe::primitive::optimize_indexed_mesh;

[[nodiscard]] auto make_split_vertex_format() -> Vertex_format
{
    return Vertex_format{
        Vertex_stream{0, {{Format::format_32_vec3_float, Vertex_attribute_usage::position }}},
        Vertex_stream{1, {{Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord}}}
    };
}

// The same vertices as make_soup(), with position and texcoord in streams of
// their own instead of interleaved in one.
[[nodiscard]] auto make_split_streams(const Triangle_soup& soup) -> std::vector<Mesh_optimize_stream>
{
    const std::size_t source_stride = soup.vertex_format.streams.front().stride;
    const std::size_t vertex_count  = soup.vertex_data.size() / source_stride;
    std::vector<Mesh_optimize_stream> streams;
    streams.push_back(Mesh_optimize_stream{.data = std::vector<uint8_t>(vertex_count * 12, uint8_t{0}), .stride = 12});
    streams.push_back(Mesh_optimize_stream{.data = std::vector<uint8_t>(vertex_count *  8, uint8_t{0}), .stride =  8});
    for (std::size_t i = 0; i < vertex_count; ++i) {
        std::memcpy(streams[0].data.data() + i * 12, soup.vertex_data.data() + i * source_stride + 0,  12);
        std::memcpy(streams[1].data.data() + i *  8, soup.vertex_data.data() + i * source_stride + 12,  8);
    }
    return streams;
}

} // anonymous namespace

TEST(Mesh_optimizer, splitting_a_vertex_across_streams_changes_nothing)
{
    const Triangle_soup        soup   {make_unwelded_grid(6)};
    const Mesh_optimize_options options{};

    const Mesh_optimize_result single = erhe::primitive::optimize_triangle_soup(soup, options);
    ASSERT_TRUE(single.triangle_soup);

    std::vector<Mesh_optimize_stream> streams = make_split_streams(soup);
    std::vector<uint32_t>             indices = soup.index_data;
    Mesh_optimize_result              multi{};
    const Vertex_format               split_format = make_split_vertex_format();
    ASSERT_TRUE(optimize_indexed_mesh(streams, indices, split_format, options, multi));

    // Same weld (the compare covers the same bytes, just split), same triangle
    // reordering (it reads only indices and positions), therefore same remaps.
    EXPECT_EQ(multi.vertex_remap,          single.vertex_remap);
    EXPECT_EQ(multi.triangle_permutation,  single.triangle_permutation);
    EXPECT_EQ(indices,                     single.triangle_soup->index_data);
    EXPECT_EQ(multi.statistics.vertex_count_after, single.statistics.vertex_count_after);

    // And each stream carries the same per-vertex bytes the interleaved output
    // has, at the same output slot.
    const std::size_t out_stride = single.triangle_soup->vertex_format.streams.front().stride;
    const std::size_t out_count  = multi.statistics.vertex_count_after;
    ASSERT_EQ(streams[0].data.size(), out_count * 12);
    ASSERT_EQ(streams[1].data.size(), out_count *  8);
    for (std::size_t i = 0; i < out_count; ++i) {
        const uint8_t* const interleaved = single.triangle_soup->vertex_data.data() + i * out_stride;
        EXPECT_EQ(std::memcmp(streams[0].data.data() + i * 12, interleaved +  0, 12), 0);
        EXPECT_EQ(std::memcmp(streams[1].data.data() + i *  8, interleaved + 12,  8), 0);
    }
}

TEST(Mesh_optimizer, the_weld_compares_every_stream_not_just_the_first)
{
    // Two triangles whose three vertices have identical POSITIONS but differ in
    // the second stream. A weld that only looked at stream 0 would merge them
    // and lose the texcoord split - the hard-edge / UV-seam case.
    const Vertex_format format = make_split_vertex_format();
    const float positions[6][3] = {
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}
    };
    const float texcoords[6][2] = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f},
        {0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}
    };
    std::vector<Mesh_optimize_stream> streams;
    streams.push_back(Mesh_optimize_stream{.data = std::vector<uint8_t>(6 * 12, uint8_t{0}), .stride = 12});
    streams.push_back(Mesh_optimize_stream{.data = std::vector<uint8_t>(6 *  8, uint8_t{0}), .stride =  8});
    for (std::size_t i = 0; i < 6; ++i) {
        std::memcpy(streams[0].data.data() + i * 12, &positions[i][0], 12);
        std::memcpy(streams[1].data.data() + i *  8, &texcoords[i][0],  8);
    }
    std::vector<uint32_t> indices{0, 1, 2, 3, 4, 5};

    Mesh_optimize_result result{};
    ASSERT_TRUE(optimize_indexed_mesh(streams, indices, format, Mesh_optimize_options{}, result));
    EXPECT_EQ(result.statistics.vertex_count_after, 6u);
}

TEST(Mesh_optimizer, the_multi_stream_core_refuses_streams_that_disagree)
{
    const Vertex_format format = make_split_vertex_format();
    std::vector<Mesh_optimize_stream> streams;
    streams.push_back(Mesh_optimize_stream{.data = std::vector<uint8_t>(3 * 12, uint8_t{0}), .stride = 12});
    streams.push_back(Mesh_optimize_stream{.data = std::vector<uint8_t>(2 *  8, uint8_t{0}), .stride =  8});
    const std::vector<Mesh_optimize_stream> before  = streams;
    std::vector<uint32_t>                   indices {0, 1, 2};
    const std::vector<uint32_t>             indices_before = indices;

    Mesh_optimize_result result{};
    EXPECT_FALSE(optimize_indexed_mesh(streams, indices, format, Mesh_optimize_options{}, result));
    // Refused inputs are left exactly as they were.
    ASSERT_EQ(streams.size(), before.size());
    for (std::size_t i = 0; i < streams.size(); ++i) {
        EXPECT_EQ(streams[i].data, before[i].data);
    }
    EXPECT_EQ(indices, indices_before);
}
