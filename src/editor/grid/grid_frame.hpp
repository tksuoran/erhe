#pragma once

#include <glm/glm.hpp>

namespace editor {

// How the grid pass uses the depth buffer.
enum class Grid_depth_mode : unsigned int {
    depth_tested = 0, // at its plane: content in front of the plane covers it
    behind_content    // at far depth: shows only where nothing was drawn
};

// Placement of a grid plane (grid y = 0) as one view sees it: the grid's own
// plane, or for an axis-aligned orthogonal view the axis plane facing the
// camera (Grid::get_view_frame()). A grid hover entry carries the frame it
// was hit in, so tools place onto the plane the view shows.
class Grid_frame
{
public:
    [[nodiscard]] auto normal_in_world   () const -> glm::vec3 { return glm::normalize(glm::vec3{world_from_grid * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}}); }
    [[nodiscard]] auto tangent_in_world  () const -> glm::vec3 { return glm::normalize(glm::vec3{world_from_grid * glm::vec4{0.0f, 0.0f, 1.0f, 0.0f}}); }
    [[nodiscard]] auto bitangent_in_world() const -> glm::vec3 { return glm::normalize(glm::vec3{world_from_grid * glm::vec4{1.0f, 0.0f, 0.0f, 0.0f}}); }

    glm::mat4 world_from_grid{1.0f};
    glm::mat4 grid_from_world{1.0f};
    // Sign of the world axis that grid x / grid z run along (axis labels
    // print world coordinates); +1 for a frame that is not axis-aligned.
    glm::vec2 label_sign     {1.0f, 1.0f};
};

}
