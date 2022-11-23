#pragma once

#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/scene/node.hpp"
#include "scene/node_raytrace_mask.hpp"

#include <functional>

namespace erhe::application
{
    class Line_renderer;
}

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::raytrace
{
    class IBuffer;
    class IGeometry;
    class IInstance;
    class IScene;
    class Hit;
    class Ray;
}

namespace erhe::scene
{
    class Mesh;
}

namespace editor
{

[[nodiscard]] auto raytrace_node_mask(erhe::scene::Node& node) -> uint32_t;

class Raytrace_primitive
{
public:
    explicit Raytrace_primitive(
        const std::shared_ptr<erhe::geometry::Geometry>& geometry
    );

    std::shared_ptr<erhe::raytrace::IBuffer>  vertex_buffer;
    std::shared_ptr<erhe::raytrace::IBuffer>  index_buffer;
    erhe::primitive::Primitive_geometry       primitive_geometry;
};

class Node_raytrace
    : public erhe::scene::INode_attachment
{
public:
    explicit Node_raytrace(
        const std::shared_ptr<erhe::geometry::Geometry>& source_geometry
    );
    Node_raytrace(
        const std::shared_ptr<erhe::geometry::Geometry>& source_geometry,
        const std::shared_ptr<Raytrace_primitive>&       primitive
    );

    ~Node_raytrace() noexcept override;

    // Implements INode_attachment
    [[nodiscard]] auto node_attachment_type() const -> const char* override;
    void handle_node_scene_host_update(
        erhe::scene::Scene_host* old_scene_host,
        erhe::scene::Scene_host* new_scene_host
    ) override;
    void handle_node_transform_update      () override;
    void handle_node_visibility_mask_update(const uint64_t mask) override;

    // Public API
    [[nodiscard]] auto source_geometry   () -> std::shared_ptr<erhe::geometry::Geometry>;
    [[nodiscard]] auto raytrace_primitive() -> Raytrace_primitive*;
    [[nodiscard]] auto raytrace_geometry ()       ->       erhe::raytrace::IGeometry*;
    [[nodiscard]] auto raytrace_geometry () const -> const erhe::raytrace::IGeometry*;
    [[nodiscard]] auto raytrace_instance ()       ->       erhe::raytrace::IInstance*;
    [[nodiscard]] auto raytrace_instance () const -> const erhe::raytrace::IInstance*;
    [[nodiscard]] auto get_hit_normal    (const erhe::raytrace::Hit& hit) -> std::optional<glm::vec3>;

private:
    void initialize();

    std::shared_ptr<Raytrace_primitive>        m_primitive;
    std::shared_ptr<erhe::geometry::Geometry>  m_source_geometry;
    std::unique_ptr<erhe::raytrace::IGeometry> m_geometry;
    std::unique_ptr<erhe::raytrace::IScene>    m_scene;
    std::unique_ptr<erhe::raytrace::IInstance> m_instance;
};

auto is_raytrace(const erhe::scene::INode_attachment* const attachment) -> bool;
auto is_raytrace(const std::shared_ptr<erhe::scene::INode_attachment>& attachment) -> bool;
auto as_raytrace(erhe::scene::INode_attachment* attachment) -> Node_raytrace*;
auto as_raytrace(const std::shared_ptr<erhe::scene::INode_attachment>& attachment) -> std::shared_ptr<Node_raytrace>;

auto get_raytrace(erhe::scene::Node* node) -> std::shared_ptr<Node_raytrace>;

class Ray_hit_style
{
public:
    glm::vec4 ray_color    {1.0f, 1.0f, 1.0f, 1.0f};
    float     ray_thickness{4.0f};
    float     ray_length   {1.0f};
    glm::vec4 hit_color    {1.0f, 1.0f, 1.0f, 1.0f};
    float     hit_thickness{2.0f};
    float     hit_size     {0.5f};
};

void draw_ray_hit(
    erhe::application::Line_renderer& line_renderer,
    const erhe::raytrace::Ray&        ray,
    const erhe::raytrace::Hit&        hit,
    const Ray_hit_style&              style = {}
);

[[nodiscard]] auto project_ray(
    erhe::raytrace::IScene* const raytrace_scene,
    erhe::scene::Mesh*            ignore_mesh,
    erhe::raytrace::Ray&          ray,
    erhe::raytrace::Hit&          hit
) -> bool;

} // namespace editor
