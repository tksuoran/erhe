#include "erhe_geometry/operation/remesh.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/geometry_log.hpp"

#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_remesh.h>     // remesh_smooth
#include <geogram/mesh/mesh_decimate.h>   // mesh_decimate_vertex_clustering
#include <geogram/mesh/mesh_geometry.h>   // set_anisotropy

#include <algorithm>
#include <vector>

// TEMPORARY diagnostic: capture the exact triangulated, double-precision input
// handed to GEO::remesh_smooth() inside Remesh::build() as a standalone,
// compilable C++ reproducer, so the observed empty-RDT crash (GEO::remesh_smooth
// -> CVT.compute_surface() returns an empty M_out -> the unconditional adjust
// step feeds NL_NB_VARIABLES=0 into OpenNL and aborts) can be filed as an
// upstream Geogram bug report and isolated against official upstream Geogram.
// Each call writes one remesh_repro_NNNN.cpp into the working directory. Set to 0
// to compile the capture out. REMOVE once the Geogram issue is reported/resolved.
#define ERHE_CAPTURE_REMESH_REPRO 0

#if ERHE_CAPTURE_REMESH_REPRO
#   include <cstdio>
#   include <mutex>
#endif

namespace erhe::geometry::operation {

namespace {

// Structural flags, plus normal / texture-coordinate regeneration when requested.
[[nodiscard]] auto remesh_process_flags(const bool regenerate_attributes) -> uint64_t
{
    uint64_t flags =
        Geometry::process_flag_connect |
        Geometry::process_flag_build_edges |
        Geometry::process_flag_compute_facet_centroids;
    if (regenerate_attributes) {
        flags |=
            Geometry::process_flag_compute_smooth_vertex_normals |
            Geometry::process_flag_generate_facet_texture_coordinates;
    }
    return flags;
}

} // anonymous namespace

#if ERHE_CAPTURE_REMESH_REPRO
namespace {

// Write a self-contained reproducer for the exact mesh passed to
// GEO::remesh_smooth() below. The input is already triangulated and in double
// precision (Remesh::build() does input.facets.triangulate() and
// input.vertices.set_double_precision() first), so we dump its vertex
// coordinates as C99/C++ hex float literals (exact bit-for-bit; the empty-RDT
// failure is suspected to be floating-point / parallel-Delaunay sensitive) plus
// the triangle facet vertex indices. The generated program rebuilds an identical
// GEO::Mesh and replays remesh_smooth() so it reproduces standalone, both against
// erhe's geogram fork and against official upstream Geogram (the isolation test).
void write_geogram_remesh_repro(const GEO::Mesh& input, unsigned int target_point_count, GEO::coord_index_t dim)
{
    static std::mutex   repro_mutex;
    static unsigned int repro_index = 0;
    unsigned int index = 0;
    {
        std::lock_guard<std::mutex> lock{repro_mutex};
        index = repro_index++;
    }

    const GEO::index_t nb_vertices = input.vertices.nb();
    const GEO::index_t nb_facets   = input.facets.nb();

    char filename[64];
    std::snprintf(filename, sizeof(filename), "remesh_repro_%04u.cpp", index);
    std::FILE* file = std::fopen(filename, "wb");
    if (file == nullptr) {
        log_geogram->warn("Remesh: could not open {} for remesh repro capture", filename);
        return;
    }

    std::fprintf(
        file,
        "// Auto-generated standalone reproducer for a GEO::remesh_smooth() crash,\n"
        "// captured from erhe::geometry::operation::Remesh::build() immediately before\n"
        "// GEO::remesh_smooth(input, M_out, target, dim). In the editor, remesh_smooth\n"
        "// -> Centroidal_Voronoi_Tesselation::compute_surface() returns an EMPTY M_out\n"
        "// (compute_RDT yields 0 triangles), and remesh_smooth then unconditionally runs\n"
        "// mesh_adjust_surface() on it, which feeds NL_NB_VARIABLES=0 into OpenNL and\n"
        "// aborts (nl_assert(param > 0), nl_api.c). The empty RDT is the real bug; the\n"
        "// abort is the downstream symptom.\n"
        "//\n"
        "// Build against erhe's geogram FORK (static, erhe CMake config):\n"
        "//   build_repro.bat remesh_repro_%04u.cpp  ->  repro_build\\repro.exe\n"
        "// Build against OFFICIAL UPSTREAM geogram (the isolation test):\n"
        "//   build_repro_upstream.bat remesh_repro_%04u.cpp  ->  repro_build_upstream\\repro.exe\n"
        "//\n"
        "// Run modes:\n"
        "//   repro.exe                       full isolation matrix (count empty RDTs:\n"
        "//                                   target 2000/4000, multi- and single-threaded)\n"
        "//   repro.exe matrix <iters>        same, with a custom iteration count\n"
        "//   repro.exe count <target> <iters> [single]   count empties for one config\n"
        "//   repro.exe abort <target>        single remesh_smooth(adjust=true), reproduces\n"
        "//                                   the actual editor nl_assert abort\n"
        "//\n"
        "// NOTE: geogram must be built with -ffp-contract=off to match the editor\n"
        "// (FMA contraction breaks its exact predicates); see the geogram section of\n"
        "// erhe's top-level CMakeLists.txt.\n"
        "\n"
        "#include <geogram/basic/assert.h>\n"
        "#include <geogram/basic/command_line.h>\n"
        "#include <geogram/basic/command_line_args.h>\n"
        "#include <geogram/basic/common.h>\n"
        "#include <geogram/basic/process.h>\n"
        "#include <geogram/mesh/mesh.h>\n"
        "#include <geogram/mesh/mesh_remesh.h>\n"
        "\n"
        "#include <cstdio>\n"
        "#include <cstdlib>\n"
        "#include <cstring>\n"
        "\n"
        "static const GEO::index_t       nb_vertices    = %u;\n"
        "static const GEO::index_t       nb_facets      = %u;\n"
        "static const GEO::coord_index_t dim            = %u;\n"
        "static const GEO::index_t       default_target = %u;\n"
        "\n"
        "// nb_vertices * 3 doubles, row-major (one vertex per line). Hex float\n"
        "// literals are exact; the trailing comment shows the %%.17g decimal value.\n"
        "static const double vertices[%u] = {\n",
        index,
        index,
        static_cast<unsigned int>(nb_vertices),
        static_cast<unsigned int>(nb_facets),
        static_cast<unsigned int>(dim),
        target_point_count,
        static_cast<unsigned int>(nb_vertices * 3)
    );

    for (GEO::index_t v = 0; v < nb_vertices; ++v) {
        const double* p = input.vertices.point_ptr(v);
        std::fprintf(file, "    %a, %a, %a, //", p[0], p[1], p[2]);
        std::fprintf(file, " %.17g %.17g %.17g\n", p[0], p[1], p[2]);
    }

    std::fprintf(
        file,
        "};\n"
        "\n"
        "// nb_facets * 3 triangle vertex indices (the input is triangulated).\n"
        "static const GEO::index_t facets[%u] = {\n",
        static_cast<unsigned int>(nb_facets * 3)
    );

    for (GEO::index_t f = 0; f < nb_facets; ++f) {
        std::fprintf(
            file,
            "    %u, %u, %u,\n",
            static_cast<unsigned int>(input.facets.vertex(f, 0)),
            static_cast<unsigned int>(input.facets.vertex(f, 1)),
            static_cast<unsigned int>(input.facets.vertex(f, 2))
        );
    }

    // The rest of the program is fixed boilerplate; %% escapes a literal %.
    std::fprintf(
        file,
        "};\n"
        "\n"
        "static void build_input(GEO::Mesh& M)\n"
        "{\n"
        "    GEO::vector<double> coords(nb_vertices * 3);\n"
        "    for (GEO::index_t i = 0; i < nb_vertices * 3; ++i) {\n"
        "        coords[i] = vertices[i];\n"
        "    }\n"
        "    GEO::vector<GEO::index_t> tris(nb_facets * 3);\n"
        "    for (GEO::index_t i = 0; i < nb_facets * 3; ++i) {\n"
        "        tris[i] = facets[i];\n"
        "    }\n"
        "    M.clear();\n"
        "    M.vertices.set_dimension(3); // a fresh GEO::Mesh defaults to double precision\n"
        "    M.facets.assign_triangle_mesh(3, coords, tris, true);\n"
        "    M.facets.connect();\n"
        "}\n"
        "\n"
        "// Returns the result vertex count. adjust=false so an empty RDT does NOT abort\n"
        "// in mesh_adjust_surface (NL_NB_VARIABLES=0); this lets us measure the empty\n"
        "// rate across a loop. The empty RDT itself comes from compute_surface(), before\n"
        "// any adjust solve, so adjust=false still observes it.\n"
        "static GEO::index_t remesh_result_vertices(GEO::index_t target)\n"
        "{\n"
        "    GEO::Mesh M_in;\n"
        "    build_input(M_in);\n"
        "    GEO::Mesh M_out;\n"
        "    M_out.vertices.set_double_precision();\n"
        "    GEO::remesh_smooth(M_in, M_out, target, dim, 5, 30, 7, false /*adjust*/);\n"
        "    return M_out.vertices.nb();\n"
        "}\n"
        "\n"
        "static int count_empties(GEO::index_t target, int iterations, const char* label)\n"
        "{\n"
        "    int empties = 0;\n"
        "    for (int i = 0; i < iterations; ++i) {\n"
        "        GEO::index_t n = remesh_result_vertices(target);\n"
        "        if (n == 0) {\n"
        "            ++empties;\n"
        "        }\n"
        "        std::printf(\"  [%%s] iter %%2d: out.vertices.nb() = %%u%%s\\n\",\n"
        "                    label, i, n, (n == 0) ? \"   <-- EMPTY RDT\" : \"\");\n"
        "        std::fflush(stdout);\n"
        "    }\n"
        "    std::printf(\"[%%s] target=%%u : %%d/%%d EMPTY\\n\", label, target, empties, iterations);\n"
        "    std::fflush(stdout);\n"
        "    return empties;\n"
        "}\n"
        "\n"
        "int main(int argc, char** argv)\n"
        "{\n"
        "    GEO::initialize(GEO::GEOGRAM_INSTALL_NONE);\n"
        "    // Abort (not throw) on geo_assert: the parallel Delaunay asserts on geogram's\n"
        "    // own worker threads, where the default ASSERT_THROW is swallowed by the\n"
        "    // thread pool and the process would otherwise exit 0.\n"
        "    GEO::set_assert_mode(GEO::ASSERT_ABORT);\n"
        "    // Match erhe's CmdLine setup (editor.cpp): remesh_smooth queries\n"
        "    // remesh:multi_nerve and remesh:RVC_centroids (declared in the \"remesh\"\n"
        "    // group); querying an undeclared arg aborts.\n"
        "    GEO::CmdLine::import_arg_group(\"algo\");\n"
        "    GEO::CmdLine::import_arg_group(\"remesh\");\n"
        "    GEO::CmdLine::set_arg(\"sys:multithread\", \"true\");\n"
        "\n"
        "    if (argc >= 2 && std::strcmp(argv[1], \"abort\") == 0) {\n"
        "        GEO::index_t target = (argc >= 3) ? GEO::index_t(std::atoi(argv[2])) : default_target;\n"
        "        std::printf(\"abort mode: remesh_smooth(target=%%u, adjust=true) -- expect nl_assert abort on empty RDT\\n\", target);\n"
        "        std::fflush(stdout);\n"
        "        GEO::Mesh M_in;\n"
        "        build_input(M_in);\n"
        "        GEO::Mesh M_out;\n"
        "        M_out.vertices.set_double_precision();\n"
        "        GEO::remesh_smooth(M_in, M_out, target, dim); // adjust defaults to true\n"
        "        std::printf(\"abort mode: completed WITHOUT abort, out.vertices.nb() = %%u\\n\", M_out.vertices.nb());\n"
        "        return 0;\n"
        "    }\n"
        "\n"
        "    if (argc >= 2 && std::strcmp(argv[1], \"count\") == 0) {\n"
        "        GEO::index_t target = (argc >= 3) ? GEO::index_t(std::atoi(argv[2])) : default_target;\n"
        "        int          iters  = (argc >= 4) ? std::atoi(argv[3]) : 20;\n"
        "        bool         single = (argc >= 5) && (std::strcmp(argv[4], \"single\") == 0);\n"
        "        if (single) {\n"
        "            GEO::Process::set_max_threads(1);\n"
        "        }\n"
        "        count_empties(target, iters, single ? \"single\" : \"multi\");\n"
        "        return 0;\n"
        "    }\n"
        "\n"
        "    // Default / \"matrix\": full isolation matrix.\n"
        "    int iters = 10;\n"
        "    if (argc >= 3 && std::strcmp(argv[1], \"matrix\") == 0) {\n"
        "        iters = std::atoi(argv[2]);\n"
        "    }\n"
        "    std::printf(\"input: nb_vertices=%%u nb_facets=%%u dim=%%u (target default=%%u)\\n\",\n"
        "                nb_vertices, nb_facets, (unsigned)dim, default_target);\n"
        "    std::printf(\"max concurrent threads = %%u, iters per config = %%d\\n\",\n"
        "                GEO::Process::maximum_concurrent_threads(), iters);\n"
        "    std::fflush(stdout);\n"
        "    count_empties(default_target, iters, \"multi-2000\");\n"
        "    count_empties(4000,           iters, \"multi-4000\");\n"
        "    GEO::Process::set_max_threads(1);\n"
        "    count_empties(default_target, iters, \"single-2000\");\n"
        "    count_empties(4000,           iters, \"single-4000\");\n"
        "    return 0;\n"
        "}\n"
    );

    std::fclose(file);
    log_geogram->info(
        "Remesh: wrote Geogram remesh repro {} (nb_vertices={}, nb_facets={}, target={}, dim={})",
        filename, nb_vertices, nb_facets, target_point_count, static_cast<unsigned int>(dim)
    );
}

} // namespace
#endif // ERHE_CAPTURE_REMESH_REPRO

// Surface remeshing (isotropic or anisotropic) via Geogram's centroidal
// Voronoi tessellation. remesh_smooth() needs a non-const, triangulated input
// surface in double precision and writes a brand new surface into M_out, so we
// build a private working copy of the source and let the result replace the
// destination mesh wholesale. Like Repair, no source/destination provenance is
// tracked: post_processing() leaves the Geogram-written positions untouched
// (empty source tables -> zero weight -> skipped) and, when requested,
// regenerates normals and facet texture coordinates.
class Remesh : public Geometry_operation
{
public:
    Remesh(const Geometry& source, Geometry& destination, unsigned int target_point_count, double anisotropy, bool regenerate_attributes)
        : Geometry_operation    {source, destination}
        , m_target_point_count  {target_point_count}
        , m_anisotropy          {anisotropy}
        , m_regenerate_attributes{regenerate_attributes}
    {
    }

    void build();

private:
    unsigned int m_target_point_count;
    double       m_anisotropy;
    bool         m_regenerate_attributes;
};

void Remesh::build()
{
    GEO::Mesh input;
    input.copy(source_mesh, true);
    input.vertices.set_double_precision();
    input.facets.triangulate();
    // remesh_smooth()'s multinerve RDT path computes per-seed restricted-Voronoi
    // connected components, which require facet-facet adjacency on the input
    // surface. triangulate() rebuilds the facets via assign_triangle_mesh() and
    // leaves every facet edge a boundary (no adjacency), so without this the
    // surface looks fully disconnected: each facet becomes its own component,
    // the RDT explodes into a fragmented soup, mesh_postprocess_RDT() strips it
    // all, and mesh_adjust_surface() aborts on the empty mesh. Build adjacency
    // from the (already welded) shared vertices so the surface is connected.
    input.facets.connect();

    const bool                anisotropic = (m_anisotropy > 0.0);
    const GEO::coord_index_t  dim         = anisotropic ? GEO::coord_index_t{6} : GEO::coord_index_t{3};
    if (anisotropic) {
        // Raises the embedding to 6D (normals scaled by m_anisotropy) so the
        // CVT elongates triangles along low-curvature directions.
        GEO::set_anisotropy(input, m_anisotropy);
    }

    destination.get_attributes().unbind();
    // remesh_smooth writes the result surface via MeshVertices::assign_points(),
    // which requires double precision; the destination starts single precision.
    destination_mesh.vertices.set_double_precision();
#if ERHE_CAPTURE_REMESH_REPRO
    // Capture the isotropic 3D input only: the capture dumps 3 coordinates per
    // vertex, which matches the dim==3 path (input has not been raised to the
    // anisotropic 6D embedding here).
    if (!anisotropic) {
        write_geogram_remesh_repro(input, m_target_point_count, dim);
    }
#endif
    GEO::remesh_smooth(input, destination_mesh, m_target_point_count, dim);

    // Robustness guard against a degenerate remesh result. Geogram's
    // remesh_smooth() can return an empty surface for some inputs -- e.g. the
    // multinerve branch of RVD::compute_RDT emits RDT vertices but zero triangles
    // for near-cospherical CVT seeds (the editor disables remesh:multi_nerve to
    // dodge that geogram bug; see editor.cpp). An empty result would otherwise
    // replace the object with a zero-facet mesh (and, with the unguarded geogram
    // adjust step, abort via an OpenNL assertion). Keep the original surface and
    // warn instead of emitting an empty mesh.
    if (destination_mesh.facets.nb() == 0) {
        log_geometry->warn(
            "Remesh produced an empty surface (vertices={}, facets={}); keeping the original mesh.",
            destination_mesh.vertices.nb(), destination_mesh.facets.nb()
        );
        if (&source_mesh != &destination_mesh) {
            destination_mesh.copy(source_mesh, true);
        }
    }
    if (destination_mesh.vertices.dimension() != 3) {
        // Drop the anisotropic 6D embedding, keep the 3D positions.
        destination_mesh.vertices.set_dimension(3);
    }
    destination_mesh.vertices.set_single_precision();
    destination.get_attributes().bind();

    post_processing(remesh_process_flags(m_regenerate_attributes));
}

void remesh(const Geometry& source, Geometry& destination, unsigned int target_point_count, double anisotropy, bool regenerate_attributes)
{
    Remesh operation{source, destination, target_point_count, anisotropy, regenerate_attributes};
    operation.build();
}

// Vertex-clustering decimation via Geogram. Operates in place on a triangulated
// double-precision copy of the source; the result replaces the destination mesh.
class Decimate : public Geometry_operation
{
public:
    Decimate(const Geometry& source, Geometry& destination, unsigned int nb_bins, bool regenerate_attributes)
        : Geometry_operation     {source, destination}
        , m_nb_bins              {nb_bins}
        , m_regenerate_attributes{regenerate_attributes}
    {
    }

    void build();

private:
    unsigned int m_nb_bins;
    bool         m_regenerate_attributes;
};

void Decimate::build()
{
    destination.get_attributes().unbind();
    destination_mesh.copy(source_mesh, true);
    destination_mesh.vertices.set_double_precision();
    destination_mesh.facets.triangulate();
    // mesh_decimate_vertex_clustering() is a surface (facet) operation: when it
    // clusters and deletes merged vertices it only remaps facet-corner
    // connectivity, never the independent edge store. erhe's processed source
    // meshes carry a populated edge list (Geometry::build_edges()), and copy()
    // brings it along, so the merged vertices leave those edges dangling.
    // MeshVertices::delete_elements() then rewrites the dangling endpoints to
    // NO_VERTEX, and the mesh_repair() that runs at the end of decimation
    // asserts in MeshVertices::remove_isolated() when it dereferences
    // to_delete[NO_VERTEX]. The edges are irrelevant to decimation and
    // post_processing() rebuilds them, so drop them from the working mesh first.
    destination_mesh.edges.clear();
    GEO::mesh_decimate_vertex_clustering(destination_mesh, m_nb_bins, GEO::MESH_DECIMATE_DEFAULT);
    destination_mesh.vertices.set_single_precision();
    destination.get_attributes().bind();

    post_processing(remesh_process_flags(m_regenerate_attributes));
}

void decimate(const Geometry& source, Geometry& destination, unsigned int nb_bins, bool regenerate_attributes)
{
    Decimate operation{source, destination, nb_bins, regenerate_attributes};
    operation.build();
}

// Topology-preserving Laplacian (umbrella-operator) smoothing using Taubin's
// lambda/mu scheme. Geogram's GEO::mesh_smooth() solves a global least-squares
// Laplacian system that locks only vertices flagged in a "selection" attribute;
// with no locked vertices (the case for the editor's typical closed meshes,
// which have no border) that system's null space is the constant (collapse to a
// point) solution, so it would collapse the mesh. The explicit Taubin form below
// is well posed for both open and closed meshes and barely shrinks volume, while
// keeping the original polygon topology (no triangulation, no new vertices), so
// the source attributes stay valid and can be preserved.
class Smooth : public Geometry_operation
{
public:
    Smooth(const Geometry& source, Geometry& destination, unsigned int iterations, float strength, bool regenerate_attributes)
        : Geometry_operation     {source, destination}
        , m_iterations           {iterations}
        , m_strength             {strength}
        , m_regenerate_attributes{regenerate_attributes}
    {
    }

    void build();

private:
    unsigned int m_iterations;
    float        m_strength;
    bool         m_regenerate_attributes;
};

void Smooth::build()
{
    destination.get_attributes().unbind();
    destination_mesh.copy(source_mesh, true);
    destination_mesh.vertices.set_double_precision();

    const double lambda = static_cast<double>(m_strength);
    if ((m_iterations > 0) && (lambda > 0.0)) {
        // Taubin lambda/mu: alternate a shrinking Laplacian step (lambda > 0) with
        // an inflating step (mu < -lambda) so volume is preserved. mu is derived
        // from lambda via the pass-band frequency k_PB: 1/lambda + 1/mu = k_PB.
        const double k_pb = 0.1;
        const double mu   = 1.0 / (k_pb - (1.0 / lambda)); // negative, |mu| > lambda for lambda < 1/k_PB

        const GEO::index_t        vertex_count = destination_mesh.vertices.nb();
        std::vector<GEO::vec3>    neighbor_sum(vertex_count);
        std::vector<GEO::index_t> neighbor_count(vertex_count);

        const unsigned int pass_count = m_iterations * 2u;
        for (unsigned int pass = 0; pass < pass_count; ++pass) {
            const double factor = ((pass % 2u) == 0u) ? lambda : mu;
            std::fill(neighbor_sum.begin(),   neighbor_sum.end(),   GEO::vec3{0.0, 0.0, 0.0});
            std::fill(neighbor_count.begin(), neighbor_count.end(), GEO::index_t{0});

            // Uniform umbrella operator: each directed facet edge (corner -> next)
            // contributes its endpoint w as a neighbor of v.
            for (GEO::index_t facet : destination_mesh.facets) {
                const GEO::index_t corner_count = destination_mesh.facets.nb_corners(facet);
                for (GEO::index_t local_corner = 0; local_corner < corner_count; ++local_corner) {
                    const GEO::index_t corner      = destination_mesh.facets.corner(facet, local_corner);
                    const GEO::index_t next_corner = destination_mesh.facets.next_corner_around_facet(facet, corner);
                    const GEO::index_t v           = destination_mesh.facet_corners.vertex(corner);
                    const GEO::index_t w           = destination_mesh.facet_corners.vertex(next_corner);
                    neighbor_sum[v] += destination_mesh.vertices.point(w);
                    ++neighbor_count[v];
                }
            }

            for (GEO::index_t v : destination_mesh.vertices) {
                if (neighbor_count[v] == 0) {
                    continue;
                }
                const GEO::vec3 average = (1.0 / static_cast<double>(neighbor_count[v])) * neighbor_sum[v];
                GEO::vec3&      point   = destination_mesh.vertices.point(v);
                point = point + factor * (average - point);
            }
        }
    }

    destination_mesh.vertices.set_single_precision();
    destination.get_attributes().bind();

    post_processing(remesh_process_flags(m_regenerate_attributes));
}

void smooth(const Geometry& source, Geometry& destination, unsigned int iterations, float strength, bool regenerate_attributes)
{
    Smooth operation{source, destination, iterations, strength, regenerate_attributes};
    operation.build();
}

} // namespace erhe::geometry::operation
