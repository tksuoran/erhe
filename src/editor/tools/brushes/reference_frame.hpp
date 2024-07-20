#pragma once

#include "erhe_geometry/types.hpp"
#include <glm/glm.hpp>
#include <cstdint>

namespace erhe::geometry {
    class Geometry;
}

namespace editor {

class Reference_frame
{
public:
    Reference_frame();

    Reference_frame(const erhe::geometry::Geometry& geometry, erhe::geometry::Polygon_id polygon_id, uint32_t face_offset, uint32_t corner_offset);

    void transform_by(const glm::mat4& m);

    [[nodiscard]] auto transform() const -> glm::mat4;
    [[nodiscard]] auto scale    () const -> float;

    uint32_t                   corner_count {0};
    uint32_t                   face_offset  {0};
    uint32_t                   corner_offset{0};
    erhe::geometry::Polygon_id polygon_id   {0};
    erhe::geometry::Corner_id  corner_id    {0};
    glm::vec3                  centroid     {0.0f, 0.0f, 0.0f}; // polygon centroid
    glm::vec3                  position     {1.0f, 0.0f, 0.0f}; // one of polygon corner points
    glm::vec3                  B            {0.0f, 0.0f, 1.0f};
    glm::vec3                  T            {1.0f, 0.0f, 0.0f};
    glm::vec3                  N            {0.0f, 1.0f, 0.0f};
};

}
