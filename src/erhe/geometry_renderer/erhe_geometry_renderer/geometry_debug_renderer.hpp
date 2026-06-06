#pragma once

#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>

namespace GEO {
    typedef unsigned int geo_index_t;
    typedef geo_index_t index_t;
}

namespace erhe::geometry { class Geometry; }
namespace erhe::math     { class Coordinate_conventions; }
namespace erhe::renderer {
    class Primitive_renderer;
    class Text_renderer;
}

namespace erhe::geometry_renderer {

// conventions must match the conventions clip_from_world was built with
// (the graphics device coordinate conventions); used to project debug text
// positions to window space.
void debug_draw(
    const erhe::math::Viewport&               viewport,
    const glm::mat4&                          clip_from_world,
    const erhe::math::Coordinate_conventions& conventions,
    erhe::renderer::Primitive_renderer&       line_renderer,
    erhe::renderer::Text_renderer&            text_renderer,
    glm::mat4                                 world_from_local,
    erhe::geometry::Geometry&                 geometry,
    GEO::index_t                              facet_filter
);

} // namespace erhe::geometry_renderer
