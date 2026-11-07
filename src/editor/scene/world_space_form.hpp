#pragma once

#include "scene/mesh_form.hpp"
#include "scene/world_space_validity.hpp"
#include "config/generated/scene_export_settings.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace erhe::primitive {
    class Primitive;
    class Primitive_render_shape;
}
namespace erhe::scene {
    class Mesh;
    class Xformable;
    using Node = Xformable;
}

namespace editor {

class Scene_root;

// One admitted mesh node a bake hands to install().
class World_space_form_mesh
{
public:
    std::shared_ptr<erhe::scene::Mesh>                         mesh;
    glm::mat4                                                  world_from_node{1.0f}; // relative to the scene root, as keyed
    // Parallel to the mesh's primitives: its render shape and the primitive
    // loaded from its World-space mesh item (null: no item).
    std::vector<const erhe::primitive::Primitive_render_shape*> shapes;
    std::vector<std::shared_ptr<erhe::primitive::Primitive>>    world_primitives;
};

// The world-space form of one open scene: render proxies drawn from the
// World-space mesh items of the last bake (doc/editor/world_space_bake.md "Two forms").
class World_space_form
{
public:
    World_space_form();
    ~World_space_form() noexcept;
    World_space_form(const World_space_form&) = delete;
    auto operator=(const World_space_form&) -> World_space_form& = delete;

    // Main thread, with the scene quiescent: replaces the form.
    void install  (Scene_root& scene_root, std::vector<World_space_form_mesh>&& meshes);
    void uninstall(Scene_root& scene_root);

    [[nodiscard]] auto get_validity()       -> World_space_validity&;
    [[nodiscard]] auto get_status  () const -> World_space_form_status;
    // The source mesh a render proxy of the form draws (null for any other mesh).
    [[nodiscard]] auto find_source (const erhe::scene::Mesh& proxy) const -> std::shared_ptr<erhe::scene::Mesh>;

private:
    World_space_validity                          m_validity;
    mutable std::mutex                            m_mutex; // the members below
    std::shared_ptr<erhe::scene::Node>            m_group_node;
    std::vector<std::weak_ptr<erhe::scene::Mesh>> m_sources; // carry Editor_item_flags::world_space_source
    std::unordered_map<const erhe::scene::Mesh*, std::weak_ptr<erhe::scene::Mesh>> m_source_by_proxy;
};

// The export settings the World-space mesh keys and the bake's admission read
// (the LOD request, instancing and its budget), as text compared at the edit.
[[nodiscard]] auto make_world_space_settings_signature(const std::optional<Scene_export_settings>& settings) -> std::string;

}
