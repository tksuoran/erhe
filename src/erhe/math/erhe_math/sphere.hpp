#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <vector>

namespace erhe::math {

class Sphere
{
public:
    glm::vec3 center;
    float radius;

    [[nodiscard]] auto volume() const
    {
        return (4.0f / 3.0f) * glm::pi<float>() * radius * radius * radius;
    }

    auto contains(const glm::vec3& point) const -> bool;
    auto contains(const glm::vec3& point, float epsilon) const -> bool;

    void enclose(const glm::vec3& point, float epsilon = 1e-4f);
    void enclose(const Sphere& sphere);

    [[nodiscard]] auto transformed_by(const glm::mat4& m) const -> Sphere;
};

auto optimal_enclosing_sphere(const std::vector<glm::vec3>& points) -> Sphere;
// auto optimal_enclosing_sphere(const glm::vec3& a, const glm::vec3& b) -> Sphere;
// auto optimal_enclosing_sphere(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) -> Sphere;
// auto optimal_enclosing_sphere(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d) -> Sphere;
// auto optimal_enclosing_sphere(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec3& e, std::size_t& redundant_point) -> Sphere;

} // namespace erhe::math
