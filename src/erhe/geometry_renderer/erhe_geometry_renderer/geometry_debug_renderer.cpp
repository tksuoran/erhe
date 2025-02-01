#include "erhe_geometry_renderer/geometry_debug_renderer.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_renderer/scoped_line_renderer.hpp"
#include "erhe_renderer/text_renderer.hpp"

namespace erhe::geometry_renderer {

void debug_draw(
    const erhe::math::Viewport&           viewport,
    const glm::mat4&                      clip_from_world,
    erhe::renderer::Scoped_line_renderer& line_renderer,
    erhe::renderer::Text_renderer&        text_renderer,
    glm::mat4                             world_from_local,
    erhe::geometry::Geometry&             geometry,
    GEO::index_t                          facet_filter
)
{
    geometry.access_debug_entries(
        [&](
            std::vector<erhe::geometry::Geometry::Debug_text>& debug_texts,
            std::vector<erhe::geometry::Geometry::Debug_line>& debug_lines
        ) {
            const GEO::Mesh& geo_mesh = geometry.get_mesh();
            for (const erhe::geometry::Geometry::Debug_line& entry : debug_lines) {
                if (facet_filter != GEO::NO_INDEX) {
                    if ((entry.reference_facet != GEO::NO_INDEX) && (facet_filter != entry.reference_facet)) {
                        continue;
                    }
                    if (entry.reference_vertex != GEO::NO_INDEX) {
                        bool found = false;
                        for (GEO::index_t corner_in_filter_facet : geo_mesh.facets.corners(facet_filter)) {
                            const GEO::index_t vertex_in_filter_facet = geo_mesh.facet_corners.vertex(corner_in_filter_facet);
                            if (entry.reference_vertex == vertex_in_filter_facet) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            continue;
                        }
                    }
                }
                const erhe::geometry::Geometry::Debug_vertex& v0 = entry.vertices[0];
                const erhe::geometry::Geometry::Debug_vertex& v1 = entry.vertices[1];
                const glm::vec4 p0{world_from_local * glm::vec4{glm::vec3{v0.position}, 1.0f}};
                const glm::vec4 p1{world_from_local * glm::vec4{glm::vec3{v1.position}, 1.0f}};
                line_renderer.add_line(v0.color, v0.width, p0, v1.color, v1.width, p1);
            }
            for (const erhe::geometry::Geometry::Debug_text& entry : debug_texts) {
                if (facet_filter != GEO::NO_INDEX) {
                    if ((entry.reference_facet != GEO::NO_INDEX) && (facet_filter != entry.reference_facet)) {
                        continue;
                    }
                    if (entry.reference_vertex != GEO::NO_INDEX) {
                        bool found = false;
                        for (GEO::index_t corner_in_filter_facet : geo_mesh.facets.corners(facet_filter)) {
                            const GEO::index_t vertex_in_filter_facet = geo_mesh.facet_corners.vertex(corner_in_filter_facet);
                            if (entry.reference_vertex == vertex_in_filter_facet) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            continue;
                        }
                    }
                }
                const glm::vec3 p_world = glm::vec3{world_from_local * glm::vec4{entry.position, 1.0f}};
                const glm::vec3 p_window = viewport.project_to_screen_space(
                    clip_from_world,
                    p_world,
                    0.0f,
                    1.0f
                );
                const glm::vec3 p{
                     p_window.x,
                     p_window.y,
                    -p_window.z
                };

                text_renderer.print(p, entry.color, entry.text);
                //// constexpr float d = 0.1f;
                //// const glm::vec3 px = p_world + glm::vec3{ d, 0.0f, 0.0f};
                //// const glm::vec3 nx = p_world + glm::vec3{-d, 0.0f, 0.0f};
                //// const glm::vec3 py = p_world + glm::vec3{0.0f,  d, 0.0f};
                //// const glm::vec3 ny = p_world + glm::vec3{0.0f, -d, 0.0f};
                //// const glm::vec3 pz = p_world + glm::vec3{0.0f, 0.0f,  d};
                //// const glm::vec3 nz = p_world + glm::vec3{0.0f, 0.0f, -d};
                //// line_renderer.set_thickness(1.0f);
                //// line_renderer.set_line_color(glm::vec4{1.0f, 0.0f, 1.0f, 1.0f});
                //// line_renderer.add_lines( { {nx, px}, {ny, py}, {nz, pz} } );
            }
        }
    );
}


} // namespace erhe::geometry_renderer
