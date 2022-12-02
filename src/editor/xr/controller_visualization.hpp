#pragma once

#include <memory>

namespace erhe::xr
{
    class Pose;
}

namespace erhe::scene
{
    class Mesh;
    class Node;
}

namespace editor
{

class Material_library;
class Mesh_memory;
class Scene_root;

class Controller_visualization
{
public:
    Controller_visualization(
        Mesh_memory&       mesh_memory,
        Scene_root&        scene_root,
        erhe::scene::Node* view_root
    );

    [[nodiscard]] auto get_node() const -> erhe::scene::Node*;

    void update(const erhe::xr::Pose& pose);

private:
    std::shared_ptr<erhe::scene::Node> m_controller_node;
    std::shared_ptr<erhe::scene::Mesh> m_controller_mesh;
};

} // namespace editor
