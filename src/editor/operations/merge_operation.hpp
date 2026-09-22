#pragma once

#include "erhe_physics/irigid_body.hpp"

#include "operations/operation.hpp"

#include "erhe_primitive/build_info.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

namespace erhe::primitive { class Buffer_info; }

namespace editor {

class PP_context;
class Mesh_operation;
class Mesh_raytrace;

class Merge_operation : public Operation
{
public:
    class Parameters
    {
    public:
        App_context&                context;
        // The items to merge, in target-first order: the first mesh-carrying
        // node is the survivor. Filled by Operations::resolve_operation_items,
        // which puts the active mesh first (doc/editor/active_item.md D6).
        std::vector<std::shared_ptr<erhe::Item_base>> items;
        erhe::primitive::Build_info build_info;
        std::function<erhe::geometry::Geometry(
            const erhe::geometry::Geometry& lhs,
            const erhe::geometry::Geometry& rhs)
        >                           operation{};
    };

    explicit Merge_operation(Parameters&& parameters);

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    class Entry
    {
    public:
        std::shared_ptr<erhe::scene::Mesh> mesh;
        std::shared_ptr<erhe::scene::Node> node;
        std::shared_ptr<erhe::scene::Node> before_parent;
        // The node's physics state as the merge found it, restored by undo
        // (doc/erhe/property_system.md section 4.26).
        std::shared_ptr<erhe::physics::ICollision_shape> collision_shape;
        erhe::physics::Motion_mode                       motion_mode{erhe::physics::Motion_mode::e_none};
    };

    Parameters                                                 m_parameters;
    std::vector<Entry>                                         m_sources;
    // The compound shape the merged mesh's body is made from; empty when the
    // sources carried no body or the simulation is off.
    std::shared_ptr<erhe::physics::ICollision_shape>           m_combined_collision_shape;
    std::vector<erhe::scene::Mesh_primitive>                   m_first_mesh_primitives_before{};
    std::vector<erhe::scene::Mesh_primitive>                   m_first_mesh_primitives_after{};

    std::vector<std::shared_ptr<erhe::Item_base>>              m_selection_before;
    std::vector<std::shared_ptr<erhe::Item_base>>              m_selection_after;
    std::vector<std::shared_ptr<erhe::scene::Node>>            m_hold_nodes;
};

}
