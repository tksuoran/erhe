#include "erhe_scene_renderer/draw_list_key.hpp"

#include <fmt/format.h>

namespace erhe::scene_renderer {

auto c_str(const Draw_purpose purpose) -> const char*
{
    switch (purpose) {
        case Draw_purpose::color:  return "color";
        case Draw_purpose::shadow: return "shadow";
        default:                   return "?";
    }
}

auto get_draw_mobility(const erhe::scene::Mesh& mesh, const Draw_mobility requested) -> Draw_mobility
{
    switch (get_vertex_space(mesh)) {
        case Vertex_space::skinned: return Draw_mobility::skinned;
        case Vertex_space::world:   return Draw_mobility::world_space;
        default:                    return requested;
    }
}

auto get_vertex_space(const Draw_mobility mobility) -> Vertex_space
{
    switch (mobility) {
        case Draw_mobility::skinned:     return Vertex_space::skinned;
        case Draw_mobility::world_space: return Vertex_space::world;
        default:                         return Vertex_space::node;
    }
}

auto c_str(const Draw_mobility mobility) -> const char*
{
    switch (mobility) {
        case Draw_mobility::static_:     return "static";
        case Draw_mobility::dynamic:     return "dynamic";
        case Draw_mobility::skinned:     return "skinned";
        case Draw_mobility::world_space: return "world_space";
        default:                         return "?";
    }
}

auto Draw_list_key::operator==(const Draw_list_key& other) const -> bool
{
    return
        (purpose              == other.purpose             ) &&
        (mobility             == other.mobility            ) &&
        (blending             == other.blending            ) &&
        (negative_determinant == other.negative_determinant) &&
        (double_sided         == other.double_sided        ) &&
        (primitive_mode       == other.primitive_mode      ) &&
        (layer_id             == other.layer_id            ) &&
        (primitive_key_hash   == other.primitive_key_hash  ) &&
        (buffer_set           == other.buffer_set          ) &&
        (primitive_key        == other.primitive_key       );
}

auto Draw_list_key::get_hash() const -> uint64_t
{
    uint64_t hash = erhe::hash::hash(static_cast<uint8_t>(purpose));
    hash = erhe::hash::hash(static_cast<uint8_t >(mobility            ), hash);
    hash = erhe::hash::hash(static_cast<uint8_t >(blending            ), hash);
    hash = erhe::hash::hash(static_cast<uint8_t >(negative_determinant), hash);
    hash = erhe::hash::hash(static_cast<uint8_t >(double_sided        ), hash);
    hash = erhe::hash::hash(static_cast<uint8_t >(primitive_mode      ), hash);
    hash = erhe::hash::hash(static_cast<uint64_t>(layer_id            ), hash);
    hash = erhe::hash::hash(static_cast<uint64_t>(buffer_set.vertex_input_key    ), hash);
    hash = erhe::hash::hash(static_cast<uint64_t>(buffer_set.index_buffer.pool_id  ), hash);
    hash = erhe::hash::hash(static_cast<uint64_t>(buffer_set.index_buffer.buffer_id), hash);
    for (const Pool_buffer_identity& vertex_buffer : buffer_set.vertex_buffers) {
        hash = erhe::hash::hash(static_cast<uint64_t>(vertex_buffer.pool_id  ), hash);
        hash = erhe::hash::hash(static_cast<uint64_t>(vertex_buffer.buffer_id), hash);
    }
    hash = erhe::hash::hash(primitive_key_hash, hash);
    return hash;
}

auto Draw_list_key::describe() const -> std::string
{
    return fmt::format(
        "{} {} {} layer={} neg_det={} two_sided={} mode={} vik={} ib={}:{} vbs={} key={}",
        c_str(purpose),
        c_str(mobility),
        c_str(blending),
        layer_id,
        negative_determinant,
        double_sided,
        erhe::primitive::c_str(primitive_mode),
        buffer_set.vertex_input_key,
        buffer_set.index_buffer.pool_id,
        buffer_set.index_buffer.buffer_id,
        buffer_set.vertex_buffers.size(),
        primitive_key.describe()
    );
}

} // namespace erhe::scene_renderer
