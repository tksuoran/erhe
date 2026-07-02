#include "geometry_graph/geometry_graph_node_factory.hpp"
#include "geometry_graph/geometry_graph_node.hpp"
#include "geometry_graph/nodes/boolean_node.hpp"
#include "geometry_graph/nodes/conway_node.hpp"
#include "geometry_graph/nodes/geometry_output_node.hpp"
#include "geometry_graph/nodes/geometry_unary_operation_node.hpp"
#include "geometry_graph/nodes/group_nodes.hpp"
#include "geometry_graph/nodes/instance_nodes.hpp"
#include "geometry_graph/nodes/join_geometry_node.hpp"
#include "geometry_graph/nodes/math_node.hpp"
#include "geometry_graph/nodes/mesh_box_node.hpp"
#include "geometry_graph/nodes/mesh_cone_node.hpp"
#include "geometry_graph/nodes/mesh_disc_node.hpp"
#include "geometry_graph/nodes/mesh_sphere_node.hpp"
#include "geometry_graph/nodes/mesh_torus_node.hpp"
#include "geometry_graph/nodes/subdivide_node.hpp"
#include "geometry_graph/nodes/transform_node.hpp"
#include "geometry_graph/nodes/value_nodes.hpp"

#include "erhe_geometry/operation/normalize.hpp"
#include "erhe_geometry/operation/repair.hpp"
#include "erhe_geometry/operation/reverse.hpp"
#include "erhe_geometry/operation/triangulate.hpp"

namespace editor {

auto make_geometry_graph_node(App_context& context, const std::string& type_name) -> std::shared_ptr<Geometry_graph_node>
{
    std::shared_ptr<Geometry_graph_node> node{};
    if      (type_name == "box")          { node = std::make_shared<Mesh_box_node   >(); }
    else if (type_name == "sphere")       { node = std::make_shared<Mesh_sphere_node>(); }
    else if (type_name == "torus")        { node = std::make_shared<Mesh_torus_node >(); }
    else if (type_name == "cone")         { node = std::make_shared<Mesh_cone_node  >(); }
    else if (type_name == "disc")         { node = std::make_shared<Mesh_disc_node  >(); }
    else if (type_name == "subdivide")    { node = std::make_shared<Subdivide_node  >(); }
    else if (type_name == "conway")       { node = std::make_shared<Conway_node     >(); }
    else if (type_name == "transform")    { node = std::make_shared<Transform_node  >(); }
    else if (type_name == "triangulate")  { node = std::make_shared<Geometry_unary_operation_node>("Triangulate", &erhe::geometry::operation::triangulate); }
    else if (type_name == "normalize")    { node = std::make_shared<Geometry_unary_operation_node>("Normalize",   &erhe::geometry::operation::normalize); }
    else if (type_name == "reverse")      { node = std::make_shared<Geometry_unary_operation_node>("Reverse",     &erhe::geometry::operation::reverse); }
    else if (type_name == "repair")       { node = std::make_shared<Geometry_unary_operation_node>("Repair",      &erhe::geometry::operation::repair); }
    else if (type_name == "distribute")   { node = std::make_shared<Distribute_points_node >(); }
    else if (type_name == "instance")     { node = std::make_shared<Instance_on_points_node>(); }
    else if (type_name == "realize")      { node = std::make_shared<Realize_instances_node >(); }
    else if (type_name == "join")         { node = std::make_shared<Join_geometry_node>(); }
    else if (type_name == "boolean")      { node = std::make_shared<Boolean_node      >(); }
    else if (type_name == "float")        { node = std::make_shared<Float_value_node  >(); }
    else if (type_name == "integer")      { node = std::make_shared<Integer_value_node>(); }
    else if (type_name == "vector")       { node = std::make_shared<Vector_value_node >(); }
    else if (type_name == "math")         { node = std::make_shared<Math_node         >(); }
    else if (type_name == "output")       { node = std::make_shared<Geometry_output_node>(context); }
    else if (type_name == "group_input")  { node = std::make_shared<Group_input_node    >(); }
    else if (type_name == "group_output") { node = std::make_shared<Group_output_node   >(); }
    else if (type_name == "group")        { node = std::make_shared<Group_node          >(context); }
    if (node) {
        node->set_factory_type_name(type_name); // used by graph serialization to recreate the node
    }
    return node;
}

}
