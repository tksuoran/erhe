#pragma once

#include "operations/operation.hpp"

#include "erhe_primitive/build_info.hpp"
#include "erhe_scene/mesh.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace erhe::geometry {
    class Geometry;
}
namespace erhe {
    class Item_base;
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
    std::optional<glm::mat4>    transform{};

    std::vector<std::shared_ptr<erhe::Item_base>> items{};

    // std::function<
    //     bool(
    //         const std::shared_ptr<erhe::Item_base>& item
    //     )
    // >                           make_entry_filter;

    std::function<
        void (
            erhe::scene::Node*,
            Mesh_operation_parameters&
        )
    >                           make_entry_node_callback{};

    // std::function<
    //     void (
    //         const std::shared_ptr<erhe::scene::Node>&,
    //         Compound_operation::Parameters&
    //     )
    // >                           make_operations_result_callback;
};

class Mesh_operation : public Operation
{
public:
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
    void execute (App_context& context)  override;
    void undo    (App_context& context)  override;

    // Public API
    void add_entry   (Entry&& entry);
    void make_entries(
        std::function<
            void(
                const erhe::geometry::Geometry& before_geometry,
                erhe::geometry::Geometry&       after_geo_mesh,
                erhe::scene::Node*              node
            )
        > geometry_operation
    );
    void make_entries(
        std::function<
            void(
                const erhe::geometry::Geometry& before_geo_mesh,
                erhe::geometry::Geometry&       after_geo_mesh
            )
        > geometry_operation
    );
    void make_entries(std::function<void(const std::shared_ptr<erhe::scene::Mesh>& mesh)> mesh_operation);

protected:
    auto describe_entries() const -> std::string;

    Mesh_operation_parameters m_parameters;
    std::vector<Entry>        m_entries;
};

}
