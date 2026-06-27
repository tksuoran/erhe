#pragma once

#include "operations/operation.hpp"

#include "erhe_primitive/build_info.hpp"
#include "erhe_scene/mesh.hpp"

#include <geogram/basic/numeric.h>
#include <glm/glm.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
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

    // Per-Geometry selected-facet snapshot, taken on the main thread from the
    // Mesh_component_selection store (which must not be read from the async worker
    // thread). Keyed by the Geometry the facet indices index into; a selection-aware
    // operation (e.g. Catmull-Clark) looks up the geometry it is about to process and
    // restricts the topology change to those facets. Empty / missing => whole mesh.
    std::unordered_map<const erhe::geometry::Geometry*, std::set<GEO::index_t>> selected_facets{};

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

    // Most general form. The geometry operation additionally receives the
    // selected-facet set for the geometry it is processing (nullptr when the mesh
    // has no live component selection), so a selection-aware operation can restrict
    // its topology change to those facets. The other make_entries overloads delegate
    // here, passing nullptr.
    void make_entries(
        std::function<
            void(
                const erhe::geometry::Geometry& before_geometry,
                erhe::geometry::Geometry&       after_geometry,
                erhe::scene::Node*              node,
                const std::set<GEO::index_t>*   selected_facets
            )
        > geometry_operation
    );
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
