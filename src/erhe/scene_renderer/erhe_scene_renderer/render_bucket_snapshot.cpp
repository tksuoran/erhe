#include "erhe_scene_renderer/render_bucket_snapshot.hpp"

#include "erhe_verify/verify.hpp"

namespace erhe::scene_renderer {

void Render_bucket_snapshot::clear()
{
    m_count            = 0;
    has_meshes         = false;
    force_disable_mask = 0;
    primitive_records.clear();
    draw_commands.clear();
}

auto Render_bucket_snapshot::add_bucket() -> Prepared_bucket&
{
    if (m_count == m_buckets.size()) {
        m_buckets.emplace_back();
    }
    Prepared_bucket& bucket = m_buckets[m_count];
    ++m_count;
    bucket.buffer_set.vertex_buffers.clear();
    bucket.first_entry = 0;
    bucket.entry_count = 0;
    return bucket;
}

auto Render_bucket_snapshot::get_buckets() const -> std::span<const Prepared_bucket>
{
    return std::span<const Prepared_bucket>{m_buckets.data(), m_count};
}

auto Render_bucket_snapshot::get_records(const Prepared_bucket& bucket, const std::size_t record_size) const -> std::span<const std::byte>
{
    const std::size_t begin = bucket.first_entry * record_size;
    const std::size_t size  = bucket.entry_count * record_size;
    ERHE_VERIFY(begin + size <= primitive_records.size());
    return std::span<const std::byte>{primitive_records.data() + begin, size};
}

auto Render_bucket_snapshot::get_draw_commands(const Prepared_bucket& bucket) const -> std::span<const erhe::graphics::Draw_indexed_primitives_indirect_command>
{
    ERHE_VERIFY(bucket.first_entry + bucket.entry_count <= draw_commands.size());
    return std::span<const erhe::graphics::Draw_indexed_primitives_indirect_command>{
        draw_commands.data() + bucket.first_entry,
        bucket.entry_count
    };
}

} // namespace erhe::scene_renderer
