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

// Undo-able edit of the skin weights (joint_indices_0 / joint_weights_0
// vertex attributes) of a set of geometry vertices of a single mesh
// primitive, produced by one Weight_paint_tool stroke.
//
// Follows Move_mesh_vertices_operation: the *same* Geometry object is
// mutated in place (so Mesh_component_selection entries keyed on the
// Geometry pointer survive), and the primitive is rebuilt and shared
// across every mesh that references the Geometry. The rebuild also
// refreshes the auxiliary GPU streams that carry their own copy of the
// joint data (the expanded solid-wireframe mesh and the edge-line joint
// stream) - the stroke's live per-dab updates only patch the fill mesh.
// No physics or normal work: painting weights changes neither positions
// nor topology.
class Paint_weights_operation : public Operation
{
public:
    class Parameters
    {
    public:
        std::shared_ptr<erhe::scene::Mesh>        mesh;
        std::size_t                               primitive_index{0};
        std::shared_ptr<erhe::geometry::Geometry> geometry;
        std::vector<GEO::index_t>                 vertices;             // touched geometry vertices
        std::vector<glm::uvec4>                   before_joint_indices; // parallel to vertices
        std::vector<glm::vec4>                    before_joint_weights; // parallel to vertices
        std::vector<glm::uvec4>                   after_joint_indices;  // parallel to vertices
        std::vector<glm::vec4>                    after_joint_weights;  // parallel to vertices
        erhe::primitive::Build_info               build_info;
        erhe::primitive::Normal_style             normal_style{erhe::primitive::Normal_style::corner_normals};
    };

    explicit Paint_weights_operation(Parameters&& parameters);

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    void apply(
        App_context&                   context,
        const std::vector<glm::uvec4>& joint_indices,
        const std::vector<glm::vec4>&  joint_weights
    );

    Parameters m_parameters;
};

}
