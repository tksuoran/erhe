#pragma once

#include "operations/operation.hpp"

#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/enums.hpp"

#include <geogram/basic/numeric.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace erhe::geometry { class Geometry; }
namespace erhe::scene    { class Mesh; }

namespace editor {

class App_context;

// Undo-able edit that moves a set of geometry vertices of a single mesh primitive.
//
// Unlike Mesh_operation, this mutates and reuses the *same* Geometry object (it does
// not allocate a fresh one) so that the Mesh_component_selection - which keys its
// component indices on the Geometry pointer - survives the edit and undo/redo. The
// vertex move does not change topology, so only smooth vertex normals are recomputed
// (no process_flag_connect, which would renumber corners and invalidate the stored
// component indices). The renderable mesh and raytrace acceleration structure are
// rebuilt and re-attached via the same parent-detach / set_primitives / re-attach
// sequence Mesh_operation uses, so picking keeps working after the move.
class Move_mesh_vertices_operation : public Operation
{
public:
    class Parameters
    {
    public:
        std::shared_ptr<erhe::scene::Mesh>        mesh;
        std::size_t                               primitive_index{0};
        std::shared_ptr<erhe::geometry::Geometry> geometry;
        std::vector<GEO::index_t>                 vertices;          // affected geometry vertices
        std::vector<glm::vec3>                    before_positions;  // mesh-local, parallel to vertices
        std::vector<glm::vec3>                    after_positions;   // mesh-local, parallel to vertices
        erhe::primitive::Build_info               build_info;
        erhe::primitive::Normal_style             normal_style{erhe::primitive::Normal_style::corner_normals};
    };

    explicit Move_mesh_vertices_operation(Parameters&& parameters);

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    void apply(App_context& context, const std::vector<glm::vec3>& positions);

    Parameters m_parameters;
};

}
