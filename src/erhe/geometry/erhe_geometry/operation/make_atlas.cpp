#include "erhe_geometry/operation/make_atlas.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/geometry.hpp"

#include <geogram/mesh/mesh.h>
#include <geogram/parameterization/mesh_atlas_maker.h>
#include <geogram/basic/attributes.h>

#include <mutex>

namespace erhe::geometry::operation {

namespace {

[[nodiscard]] auto to_geo(const Atlas_parameterizer parameterizer) -> GEO::ChartParameterizer
{
    switch (parameterizer) {
        case Atlas_parameterizer::projection:    return GEO::PARAM_PROJECTION;
        case Atlas_parameterizer::lscm:          return GEO::PARAM_LSCM;
        case Atlas_parameterizer::spectral_lscm: return GEO::PARAM_SPECTRAL_LSCM;
        case Atlas_parameterizer::abf:           return GEO::PARAM_ABF;
        default:                                 return GEO::PARAM_ABF;
    }
}

[[nodiscard]] auto to_geo(const Atlas_packer packer) -> GEO::ChartPacker
{
    switch (packer) {
        case Atlas_packer::none:   return GEO::PACK_NONE;
        case Atlas_packer::tetris: return GEO::PACK_TETRIS;
        case Atlas_packer::xatlas: return GEO::PACK_XATLAS;
        default:                   return GEO::PACK_XATLAS;
    }
}

void delete_attribute_if_present(GEO::AttributesManager& manager, const char* const name)
{
    if (manager.is_defined(name)) {
        manager.delete_attribute_store(name);
    }
}

} // anonymous namespace

// UV atlas generation via Geogram's mesh_make_atlas(). The mesh topology is
// preserved: mesh_make_atlas() fan-triangulates each polygon internally and
// commits the resulting UVs back to every original facet corner in a "tex_coord"
// facet-corners attribute (the packer only transforms UVs, never the geometry).
// We copy the source (with all its attributes) into the destination, run the
// atlas maker, then move the UVs from Geogram's "tex_coord" attribute into the
// selected erhe corner texcoord channel. Like Smooth, no source/destination
// provenance is tracked, so post_processing() leaves the freshly written UVs
// (and all other copied attributes) untouched (empty source tables -> zero weight
// -> skipped).
class Make_atlas : public Geometry_operation
{
public:
    Make_atlas(
        const Geometry&     source,
        Geometry&           destination,
        std::size_t         usage_index,
        double              hard_angles_threshold,
        Atlas_parameterizer parameterizer,
        Atlas_packer        packer)
        : Geometry_operation     {source, destination}
        , m_usage_index          {usage_index}
        , m_hard_angles_threshold{hard_angles_threshold}
        , m_parameterizer        {parameterizer}
        , m_packer               {packer}
    {
    }

    void build();

private:
    std::size_t         m_usage_index;
    double              m_hard_angles_threshold;
    Atlas_parameterizer m_parameterizer;
    Atlas_packer        m_packer;
};

void Make_atlas::build()
{
    destination.get_attributes().unbind();
    destination_mesh.copy(source_mesh, true);
    generate_mesh_atlas_texture_coordinates(
        destination_mesh,
        destination.get_attributes(),
        m_usage_index,
        m_hard_angles_threshold,
        m_parameterizer,
        m_packer
    );
    post_processing(structural_post_process_flags);
}

void generate_mesh_atlas_texture_coordinates(
    GEO::Mesh&                mesh,
    Mesh_attributes&          attributes,
    const std::size_t         usage_index,
    const double              hard_angles_threshold,
    const Atlas_parameterizer parameterizer,
    const Atlas_packer        packer)
{
    // Precondition: attributes is UNBOUND (see header). mesh_make_atlas() reads
    // vertices.point() in double precision and grows charts across facet
    // adjacency, so ensure both are in place before the call.
    mesh.vertices.set_double_precision();
    mesh.facets.connect();

    {
        // Geogram's progress system uses a process-global, non-thread-safe task
        // stack (basic/progress.cpp: "geo_assert(progress_tasks_.top() == task)").
        // The editor builds brushes on many worker threads, so concurrent
        // mesh_make_atlas() calls corrupt that stack. A single mesh_make_atlas()
        // call is safe (the MCP path uses one at a time), so serialize the Geogram
        // call here; the surrounding per-mesh work stays parallel.
        static std::mutex           s_mesh_make_atlas_mutex;
        std::lock_guard<std::mutex> lock{s_mesh_make_atlas_mutex};
        GEO::mesh_make_atlas(
            mesh,
            hard_angles_threshold,
            to_geo(parameterizer),
            to_geo(packer),
            false // verbose
        );
    }

    mesh.vertices.set_single_precision();
    attributes.bind();

    // Move Geogram's per-corner "tex_coord" (double[2]) into the selected erhe
    // corner texcoord channel, overwriting it. The scoped Attribute is unbound at
    // the end of the block before the store is deleted below (delete asserts that
    // the store has no live observers).
    {
        GEO::Attribute<double> tex_coord;
        tex_coord.bind_if_is_defined(mesh.facet_corners.attributes(), "tex_coord");
        if (tex_coord.is_bound()) {
            Attribute_present<GEO::vec2f>& corner_texcoord = attributes.corner_texcoord(usage_index);
            for (GEO::index_t corner : mesh.facet_corners) {
                corner_texcoord.set(
                    corner,
                    GEO::vec2f{
                        static_cast<float>(tex_coord[(2 * corner) + 0]),
                        static_cast<float>(tex_coord[(2 * corner) + 1])
                    }
                );
            }
        }
    }

    // Drop the scratch attributes mesh_make_atlas() leaves behind so they are not
    // carried forward by later operations or serialization. (erhe's own "id" is a
    // facet attribute; Geogram's "id" here is a vertex attribute - different store.)
    delete_attribute_if_present(mesh.facet_corners.attributes(), "tex_coord");
    delete_attribute_if_present(mesh.facets.attributes(),        "chart");
    delete_attribute_if_present(mesh.vertices.attributes(),      "id");
}

void make_atlas(
    const Geometry&     source,
    Geometry&           destination,
    const std::size_t   usage_index,
    const double        hard_angles_threshold,
    Atlas_parameterizer parameterizer,
    Atlas_packer        packer)
{
    Make_atlas operation{source, destination, usage_index, hard_angles_threshold, parameterizer, packer};
    operation.build();
}

} // namespace erhe::geometry::operation
