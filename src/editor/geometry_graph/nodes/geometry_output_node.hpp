#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

#include <memory>

namespace erhe::primitive { class Material; }
namespace erhe::scene {
    class Mesh;
    class Node;
}

namespace editor {

class App_context;
class Scene_root;

// Terminal node piping the input geometry into a scene as a renderable
// mesh. Owns one erhe::scene::Node with an attached erhe::scene::Mesh
// which is updated in place on every re-evaluation; the scene node is
// removed when this graph node is deleted.
//
// Target scene defaults to the single registered scene root; when
// several scenes exist a stepper selects one. Material defaults to the
// first material in the scene's content library.
class Geometry_output_node : public Geometry_graph_node
{
public:
    explicit Geometry_output_node(App_context& context);
    ~Geometry_output_node() noexcept override;

    void evaluate(Geometry_graph&) override;
    void imgui   () override;

private:
    void remove_scene_node();

    App_context&                               m_context;
    std::shared_ptr<Scene_root>                m_scene_root;
    std::shared_ptr<erhe::primitive::Material> m_material;
    std::shared_ptr<erhe::scene::Node>         m_node;
    std::shared_ptr<erhe::scene::Mesh>         m_mesh;
};

}
