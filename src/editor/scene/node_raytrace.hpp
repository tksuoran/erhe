#pragma once

#include "erhe_item/item.hpp"

#include <glm/glm.hpp>

#include <optional>

namespace erhe::geometry {
    class Geometry;
}
namespace erhe::raytrace {
    class IGeometry;
    class IInstance;
    class IScene;
    class Hit;
    class Ray;
}
namespace erhe::primitive {
    class Primitive;
}
namespace erhe::renderer {
    class Primitive_renderer;
}
namespace erhe::scene {
    class Mesh;
    class Node;
}

namespace editor {

[[nodiscard]] auto raytrace_node_mask(erhe::Item_base& item) -> uint32_t;

class Ray_hit_style
{
public:
    glm::vec4 ray_color    {1.0f, 1.0f, 1.0f, 1.0f};
    float     ray_thickness{2.0f};
    float     ray_length   {1.0f};
    glm::vec4 hit_color    {1.0f, 1.0f, 1.0f, 1.0f};
    float     hit_thickness{2.0f};
    float     hit_size     {0.5f};
};

[[nodiscard]] auto get_hit_node(const erhe::raytrace::Hit& hit) -> erhe::scene::Node*;

[[nodiscard]] auto get_hit_normal(const erhe::raytrace::Hit& hit) -> std::optional<glm::vec3>;

// ray_line_renderer, when given, receives the ray lines (tail toward the ray
// origin + dim continuation) while line_renderer keeps the hit markers. Pass
// a bucket with a HIGHER stencil reference than line_renderer's: the debug
// buckets layer first-wins per pixel within one stencil reference, so where
// the tail overlaps a marker (ray antiparallel to the surface normal) the
// earlier-drawn marker would block it - even with its faded, near-invisible
// fragments. A higher reference lets the ray lines alpha-blend on top of the
// marker pixels instead.
void draw_ray_hit(
    erhe::renderer::Primitive_renderer& line_renderer,
    const erhe::raytrace::Ray&          ray,
    const erhe::raytrace::Hit&          hit,
    const Ray_hit_style&                style = {},
    erhe::renderer::Primitive_renderer* ray_line_renderer = nullptr
);

[[nodiscard]] auto project_ray(
    erhe::raytrace::IScene* raytrace_scene,
    erhe::scene::Mesh*      ignore_mesh,
    erhe::raytrace::Ray&    ray,
    erhe::raytrace::Hit&    hit
) -> bool;

}
