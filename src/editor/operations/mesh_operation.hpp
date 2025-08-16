#pragma once

#include "operations/ioperation.hpp"

#include "erhe_primitive/build_info.hpp"
#include "erhe_scene/mesh.hpp"

#include <functional>
#include <vector>

namespace erhe::geometry {
    class Geometry;
}
namespace erhe::scene {
    class Mesh;
    class Node;
}

namespace editor {

class App_context;
class Node_physics;

class Mesh_operation_parameters
{
public:
    App_context&                context;
    erhe::primitive::Build_info build_info;
};

class Mesh_operation : public Operation
{
protected:
    class Entry
    {
    public:
        // TODO consider keeping node always alive using std::shared_ptr<erhe::scene::Node> node;
        std::shared_ptr<erhe::scene::Mesh> scene_mesh;

        class Version
        {
        public:
            std::shared_ptr<Node_physics>            node_physics{};
            std::vector<erhe::scene::Mesh_primitive> primitives{};
        };
        Version before{};
        Version after{};
    };

    explicit Mesh_operation(Mesh_operation_parameters&& parameters);
    ~Mesh_operation() noexcept override;

    // Implements Operation
    auto describe() const -> std::string override;
    void execute (App_context& context)  override;
    void undo    (App_context& context)  override;

    // Public API
    void add_entry   (Entry&& entry);
    void make_entries(
        const std::function<
            void(
                const erhe::geometry::Geometry& before_geometry,
                erhe::geometry::Geometry&       after_geo_mesh,
                erhe::scene::Node*              node
            )
        > operation
    );
    void make_entries(
        const std::function<
            void(
                const erhe::geometry::Geometry& before_geo_mesh,
                erhe::geometry::Geometry&       after_geo_mesh
            )
        > operation
    );

protected:
    Mesh_operation_parameters m_parameters;
    std::vector<Entry>        m_entries;
};

}
