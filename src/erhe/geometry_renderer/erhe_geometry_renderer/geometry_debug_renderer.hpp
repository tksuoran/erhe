#pragma once

#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>

namespace GEO {
    typedef unsigned int geo_index_t;
    typedef geo_index_t index_t;
}

namespace erhe::geometry { class Geometry; }
namespace erhe::renderer {
    class Scoped_line_renderer;
    class Text_renderer;
}

namespace erhe::geometry_renderer {

void debug_draw(
    const erhe::math::Viewport&           viewport,
    const glm::mat4&                      clip_from_world,
    erhe::renderer::Scoped_line_renderer& line_renderer,
    erhe::renderer::Text_renderer&        text_renderer,
    glm::mat4                             world_from_local,
    erhe::geometry::Geometry&             geometry,
    GEO::index_t                          facet_filter
);

} // namespace erhe::geometry_renderer
