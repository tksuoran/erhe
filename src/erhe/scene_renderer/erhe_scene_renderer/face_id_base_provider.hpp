#pragma once

#include <cstddef>
#include <cstdint>

namespace erhe::scene { class Mesh; }

namespace erhe::scene_renderer {

// Supplies the per-(mesh, primitive) face-id base for the ID-buffer edge-line
// method. The edge-id pre-pass (Content_wide_line_renderer in id mode) and the
// polygon-fill pass (Primitive_buffer, which stamps the base into the lit
// variant's otherwise-unused primitive.color) must derive the SAME base for the
// same primitive, so a fill fragment can match face for face. The editor builds
// one shared per-frame assignment (keyed by primitive identity, so lookup is
// order-independent across the two passes) and exposes it through this
// interface. Bases start at 1; 0 is reserved as the "no edge" / background id.
class Face_id_base_provider
{
public:
    virtual ~Face_id_base_provider() noexcept = default;
    [[nodiscard]] virtual auto get_face_id_base(
        const erhe::scene::Mesh& mesh,
        std::size_t              primitive_index
    ) const -> uint32_t = 0;
};

} // namespace erhe::scene_renderer
