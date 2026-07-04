#pragma once

#include "operations/operation.hpp"

#include <geogram/basic/numeric.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace erhe::geometry { class Geometry; }

namespace editor {

class App_context;

// Undo-able edit that sets (or clears) the semi-sharp crease sharpness of a
// set of edges of a single Geometry (doc/subdivision_crease_edges.md).
//
// Like Move_mesh_vertices_operation this mutates and reuses the *same*
// Geometry object so the Mesh_component_selection - keyed on the Geometry
// pointer - survives the edit and undo/redo. Unlike it, no render geometry
// changes: the sharpness attribute only affects the crease overlay and future
// Catmull-Clark subdivisions, so no primitive/physics rebuild and no scene
// access are needed.
class Set_edge_sharpness_operation : public Operation
{
public:
    class Parameters
    {
    public:
        std::shared_ptr<erhe::geometry::Geometry>          geometry;
        std::vector<std::pair<GEO::index_t, GEO::index_t>> edges;  // canonical (min, max) vertex pairs
        std::vector<std::optional<float>>                  before; // parallel to edges; nullopt = attribute absent
        std::optional<float>                               after;  // one value for every edge; nullopt = clear
    };

    explicit Set_edge_sharpness_operation(Parameters&& parameters);

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    Parameters m_parameters;
};

}
