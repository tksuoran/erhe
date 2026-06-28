#pragma once

#include "operations/operation.hpp"

#include "erhe_geometry/operation/geometry_operation.hpp"
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

    // Per-Geometry component-selection snapshot (whichever component type the active
    // mesh-component mode selects: vertices, facets, or edges), taken on the main
    // thread alongside selected_facets. A selection-aware topology operation remaps
    // these source components to the components it produces, so the selection follows
    // the geometry change to the newly created components. Keyed by Geometry identity;
    // empty / missing => nothing to remap.
    std::unordered_map<const erhe::geometry::Geometry*, erhe::geometry::operation::Geometry_component_selection> component_selection{};

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

        // Post-operation mesh-component selection for one result primitive: the
        // components the operation produced from this mesh's selected source
        // components, addressed against the result Geometry. Installed into the
        // Mesh_component_selection store by execute() so the selection follows the
        // topology change. primitive_index indexes the post-swap primitive list
        // (i.e. after.primitives). Only present when this primitive carried a live
        // component selection that mapped to a non-empty result.
        class Selection_remap
        {
        public:
            std::size_t                                            primitive_index{0};
            std::shared_ptr<erhe::geometry::Geometry>              after_geometry{};
            erhe::geometry::operation::Geometry_component_selection components{};
        };
        std::vector<Selection_remap> selection_remaps{};
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
    // its topology change to those facets. It also receives a source component
    // selection to remap (nullptr when none) and a destination component selection to
    // fill with the components produced from it, so the mesh-component selection can
    // follow the topology change. The other make_entries overloads delegate here,
    // passing nullptr for all three.
    void make_entries(
        std::function<
            void(
                const erhe::geometry::Geometry&                               before_geometry,
                erhe::geometry::Geometry&                                     after_geometry,
                erhe::scene::Node*                                            node,
                const std::set<GEO::index_t>*                                 selected_facets,
                const erhe::geometry::operation::Geometry_component_selection* remap_source,
                erhe::geometry::operation::Geometry_component_selection*       remap_destination
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
