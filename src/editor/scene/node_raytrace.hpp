#pragma once

#include "erhe_primitive/geometry_mesh.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "scene/node_raytrace_mask.hpp"

#include <functional>

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
    class Line_renderer;
}
namespace erhe::scene {
    class Mesh;
}

namespace editor
{

[[nodiscard]] auto raytrace_node_mask(erhe::Item_base& item) -> uint32_t;


class Raytrace_primitive
{
public:
    Raytrace_primitive(Raytrace_primitive&&);
    Raytrace_primitive& operator=(Raytrace_primitive&&);
    ~Raytrace_primitive() noexcept;

    Raytrace_primitive(
        erhe::scene::Mesh*         mesh,
        std::size_t                primitive_index,
        erhe::raytrace::IGeometry* rt_geometry
    );
    Raytrace_primitive(const Raytrace_primitive&) = delete;
    Raytrace_primitive& operator=(const Raytrace_primitive&) = delete;

    erhe::scene::Mesh*                         mesh           {nullptr};
    std::size_t                                primitive_index{0};
    std::unique_ptr<erhe::raytrace::IInstance> rt_instance;
    std::unique_ptr<erhe::raytrace::IScene>    rt_scene;
};

class Node_raytrace
    : public erhe::Item<
        erhe::Item_base,
        erhe::scene::Node_attachment,
        Node_raytrace,
        erhe::Item_kind::clone_using_custom_clone_constructor
    >
{
public:
    explicit Node_raytrace(const std::shared_ptr<erhe::scene::Mesh>& mesh);
    Node_raytrace(
        const std::shared_ptr<erhe::scene::Mesh>&      mesh,
        const std::vector<erhe::primitive::Primitive>& primitives
    );
    Node_raytrace(const Node_raytrace& src, erhe::for_clone);

    Node_raytrace(const Node_raytrace&) = delete;
    Node_raytrace& operator=(const Node_raytrace&) = delete;
    Node_raytrace(Node_raytrace&&);
    Node_raytrace& operator=(Node_raytrace&&);
    ~Node_raytrace() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Node_raytrace"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type               () const -> uint64_t                             override;
    auto get_type_name          () const -> std::string_view                     override;
    void handle_flag_bits_update(uint64_t old_flag_bits, uint64_t new_flag_bits) override;

    // Implements Node_attachment
    void handle_item_host_update     (erhe::Item_host* old_item_host, erhe::Item_host* new_item_host) override;
    void handle_node_transform_update()                                                               override;

    // Public API
    [[nodiscard]] auto get_rt_primitives() const -> const std::vector<Raytrace_primitive>&;
    void set_mask         (uint32_t rt_mask);
    void attach_to_scene  (erhe::raytrace::IScene* rt_scene);
    void detach_from_scene(erhe::raytrace::IScene* rt_scene);
    void properties_imgui ();

private:
    void initialize(
        const std::shared_ptr<erhe::scene::Mesh>&      mesh,
        const std::vector<erhe::primitive::Primitive>& primitives
    );

    erhe::raytrace::IScene*         m_rt_scene{nullptr};
    std::vector<Raytrace_primitive> m_rt_primitives;
};

auto is_raytrace(const erhe::Item_base* scene_item) -> bool;
auto is_raytrace(const std::shared_ptr<erhe::Item_base>& scene_item) -> bool;

auto get_node_raytrace(const erhe::scene::Node* node) -> std::shared_ptr<Node_raytrace>;

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

[[nodiscard]] auto get_hit_node(const erhe::raytrace::Hit& hit) -> erhe::scene::Node*;

[[nodiscard]] auto get_hit_normal(const erhe::raytrace::Hit& hit) -> std::optional<glm::vec3>;

void draw_ray_hit(
    erhe::renderer::Line_renderer& line_renderer,
    const erhe::raytrace::Ray&     ray,
    const erhe::raytrace::Hit&     hit,
    const Ray_hit_style&           style = {}
);

[[nodiscard]] auto project_ray(
    erhe::raytrace::IScene* raytrace_scene,
    erhe::scene::Mesh*      ignore_mesh,
    erhe::raytrace::Ray&    ray,
    erhe::raytrace::Hit&    hit
) -> bool;

} // namespace editor
