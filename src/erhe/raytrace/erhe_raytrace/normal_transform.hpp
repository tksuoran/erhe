#pragma once

#include <glm/glm.hpp>
#include <glm/gtx/matrix_operation.hpp>

namespace erhe::raytrace {

// Maps a local geometric normal to the instance's parent space, facing out of the solid also when the transform mirrors.
// The cofactor matrix, not the transform: transforming a normal by M shears it whenever the scale is non-uniform.
[[nodiscard]] inline auto get_normal_transform(const glm::mat4& transform) -> glm::mat4
{
    const glm::mat4 cofactor = glm::transpose(glm::adjugate(transform));
    return (glm::determinant(transform) < 0.0f) ? -cofactor : cofactor;
}

} // namespace erhe::raytrace
