#pragma once

#include <glm/glm.hpp>

namespace erhe::physics
{

class Transform
{
public:
    explicit Transform(const glm::mat4 transform)
        : basis {glm::mat3{transform}}
        , origin{glm::vec3{transform[3]}}
    {
    }

    explicit Transform(
        const glm::mat3 basis  = glm::mat3{1.0f},
        const glm::vec3 origin = glm::vec3{0.0f}
    )
        : basis {basis}
        , origin{origin}
    {
    }

    glm::mat3 basis {1.0f};
    glm::vec3 origin{0.0f};
};

inline auto operator*(const Transform& lhs, const Transform& rhs) -> Transform
{
    return Transform{
        lhs.basis * rhs.basis,
        lhs.origin + lhs.basis * rhs.origin
    };
}

inline auto inverse(const Transform& transform) -> Transform
{
	const auto inverse_basis = glm::transpose(transform.basis);
    return Transform{inverse_basis, inverse_basis * -transform.origin};
}

} // namespace erhe::physics
