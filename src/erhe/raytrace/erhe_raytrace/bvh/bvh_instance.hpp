#pragma once

#include "erhe_raytrace/iinstance.hpp"
#include "erhe_math/aabb.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace erhe::raytrace {

class Bvh_scene;

class Bvh_instance : public IInstance
{
public:
    explicit Bvh_instance(std::string_view debug_label);
    ~Bvh_instance() noexcept override;

    // Implements IInstance
    void commit       ()                           override;
    void enable       ()                           override;
    void disable      ()                           override;
    void set_transform(glm::mat4 transform)        override;
    void set_scene    (IScene* scene)              override;
    void set_mask     (uint32_t mask)              override;
    void set_user_data(void* ptr)                  override;
    auto get_transform() const -> glm::mat4        override;
    auto get_scene    () const -> IScene*          override;
    auto get_mask     () const -> uint32_t         override;
    auto get_user_data() const -> void*            override;
    auto is_enabled   () const -> bool             override;
    auto debug_label  () const -> std::string_view override;

    // Bvh_instance public API
    auto intersect(Ray& ray, Hit& hit) -> bool;

    // Bounding box of the instanced scene, transformed to the space of the scene
    // this instance is attached to.
    [[nodiscard]] auto get_bbox() const -> erhe::math::Aabb;

    // Called by Bvh_scene when this instance is attached to / detached from it.
    void add_parent_scene   (Bvh_scene* scene);
    void remove_parent_scene(Bvh_scene* scene);

    // Called by the instanced scene when its bounds change or when it is destroyed.
    void notify_parents_modified    ();
    void on_instanced_scene_destroyed();

private:
    std::vector<Bvh_scene*> m_parent_scenes;
    glm::mat4   m_transform{1.0f};
    bool        m_enabled  {true};
    IScene*     m_scene    {nullptr};
    uint32_t    m_mask     {0xffffffffu};
    void*       m_user_data{nullptr};
    std::string m_debug_label;
};

} // namespace erhe::raytrace
