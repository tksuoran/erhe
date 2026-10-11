#pragma once

#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace erhe::scene_renderer {

// One bucket of a Render_bucket_snapshot: the pipeline-identity part of a
// Render_bucket (buffer set, shader key, winding / culling partition, primitive
// mode) plus the range of the snapshot's records and draw commands it draws.
// The shader key carries the primitive-derived axes, SHADER_DEBUG and the
// pass's forced bits; the environment axes the render phase owns (light
// counts, shadow filter / bias / technique / depth bits, multiview count,
// USE_DDGI) are applied when the snapshot is drawn
// (Forward_renderer::render(const Draw_parameters&, const Render_bucket_snapshot&)).
class Prepared_bucket
{
public:
    Buffer_set                      buffer_set;
    Shader_key                      shader_key{};
    bool                            negative_determinant{false};
    bool                            double_sided        {false};
    erhe::primitive::Primitive_mode primitive_mode      {erhe::primitive::Primitive_mode::polygon_fill};
    // Range in Render_bucket_snapshot::draw_commands; the primitive records
    // of the bucket are the same range in units of the record size.
    std::size_t                     first_entry         {0};
    std::size_t                     entry_count         {0};
};

// The bucketed draws of one pass, prepared on the main thread in the prepare
// phase (Forward_renderer::prepare) and drawn by the render phase without
// reading any Mesh, Node, Primitive or Material: the complete per-primitive
// GPU records (Primitive_interface layout, pass color and size included) and
// the indexed draw commands, grouped into buckets in draw order.
//
// Storage a caller keeps across frames (one per pass, view and frame in
// flight): clear() keeps every vector's capacity and the buckets for reuse,
// so a steady-state prepare performs no heap allocation.
class Render_bucket_snapshot
{
public:
    void clear();

    // A new bucket at the end of the snapshot, reusing a previously
    // allocated one (its vertex buffer list keeps its capacity).
    [[nodiscard]] auto add_bucket ()       -> Prepared_bucket&;
    [[nodiscard]] auto get_buckets() const -> std::span<const Prepared_bucket>;

    // The primitive records of one bucket (record_size bytes each).
    [[nodiscard]] auto get_records(const Prepared_bucket& bucket, std::size_t record_size) const -> std::span<const std::byte>;
    // The draw commands of one bucket.
    [[nodiscard]] auto get_draw_commands(const Prepared_bucket& bucket) const -> std::span<const erhe::graphics::Draw_indexed_primitives_indirect_command>;

    // Whether any of the mesh spans the snapshot was prepared from held a
    // mesh. The render phase skips the pass entirely without (the same early
    // out Forward_renderer::render() took before bucketing), and begins it
    // with no draws when the meshes were all filtered away.
    bool                                                         has_meshes{false};
    // The forced shader-key bits of the pass: the render phase applies
    // USE_DDGI only where the pass did not force it off.
    uint32_t                                                     force_disable_mask{0};
    std::vector<std::byte>                                       primitive_records;
    std::vector<erhe::graphics::Draw_indexed_primitives_indirect_command> draw_commands;

private:
    std::vector<Prepared_bucket> m_buckets;
    std::size_t                  m_count{0};
};

} // namespace erhe::scene_renderer
