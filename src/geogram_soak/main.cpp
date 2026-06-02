// geogram_soak -- headless soak harness for the intermittent main-loop hang.
//
// Reproduces, with no rendering / SDL / Vulkan / headset, the GEO::Mesh
// corruption that the editor's parallel brush init (Scene_builder::make_brushes)
// produces on ARM (~1/8) and that later spins build_polygon_fill(). See
// doc/intermittent_main_loop_hang.md.
//
// It mirrors make_brushes() faithfully: N taskflow workers, each on its OWN
// task-local Geometry/GEO::Mesh, generate the same shapes the editor builds and
// call the same Geometry::process() steps. Geogram is initialized exactly as in
// editor.cpp (notably GEO::CmdLine::set_arg("sys:multithread", ...)). Meshes are
// validated AFTER the taskflow join (off the concurrent window) via the shared
// erhe::geometry::validate_mesh_structure(), so detection does not perturb the
// race the way an in-process() validator does.
//
// Knobs settle the root-cause bucket:
//   --workers 1        : sequential   -> expect CLEAN (concurrency is required)
//   --multithread off  : no parallel_for -> expect CLEAN (Geogram's internal
//                        threading / the non-atomic running_threads_invocations_
//                        counter is the racing path)
//   --workers N --multithread on : reproduces.
//
// Exit codes: 0 CLEAN, 2 corruption detected, 1 usage error.

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "erhe_geometry/shapes/cone.hpp"
#include "erhe_geometry/shapes/regular_polyhedron.hpp"
#include "erhe_geometry/shapes/sphere.hpp"
#include "erhe_geometry/shapes/torus.hpp"

#include <geogram/basic/attributes.h>
#include <geogram/basic/command_line.h>
#include <geogram/basic/command_line_args.h>
#include <geogram/basic/common.h>
#include <geogram/basic/geometry.h>
#include <geogram/delaunay/delaunay.h>
#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_repair.h>

#include "rapidjson/document.h"

#include <spdlog/spdlog.h>
#include <taskflow/taskflow.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using erhe::geometry::Geometry;

// One shape-build job: a name, the process() flags the editor uses for it, and
// a generator that fills a fresh GEO::Mesh (mirrors scene_builder.cpp).
class Shape_job
{
public:
    std::string                     name;
    std::uint64_t                   flags {0};
    std::function<void(GEO::Mesh&)> generate;
};

[[nodiscard]] auto build_shape_jobs(int detail) -> std::vector<Shape_job>
{
    const int d = (detail < 1) ? 1 : detail;

    // Flags copied verbatim from Scene_builder (scene_builder.cpp):
    const std::uint64_t platonic_flags =
        Geometry::process_flag_connect |
        Geometry::process_flag_build_edges |
        Geometry::process_flag_compute_facet_centroids |
        Geometry::process_flag_compute_smooth_vertex_normals |
        Geometry::process_flag_generate_facet_texture_coordinates |
        Geometry::process_flag_generate_tangents;
    const std::uint64_t curved_flags =
        Geometry::process_flag_connect |
        Geometry::process_flag_build_edges |
        Geometry::process_flag_generate_facet_texture_coordinates;

    namespace shapes = erhe::geometry::shapes;
    std::vector<Shape_job> jobs;
    jobs.push_back({"dodecahedron",  platonic_flags, [](GEO::Mesh& m){ shapes::make_dodecahedron (m, 1.0f); }});
    jobs.push_back({"icosahedron",   platonic_flags, [](GEO::Mesh& m){ shapes::make_icosahedron  (m, 1.0f); }});
    jobs.push_back({"octahedron",    platonic_flags, [](GEO::Mesh& m){ shapes::make_octahedron   (m, 1.0f); }});
    jobs.push_back({"cuboctahedron", platonic_flags, [](GEO::Mesh& m){ shapes::make_cuboctahedron(m, 1.0f); }});
    jobs.push_back({"tetrahedron",   platonic_flags, [](GEO::Mesh& m){ shapes::make_tetrahedron  (m, 1.0f); }});
    jobs.push_back({"cube",          platonic_flags, [](GEO::Mesh& m){ shapes::make_cube         (m, 1.0f); }});
    jobs.push_back({"sphere",        curved_flags,   [d](GEO::Mesh& m){ shapes::make_sphere(m, 1.0f, 8u * static_cast<unsigned int>(d), 6u * static_cast<unsigned int>(d)); }});
    jobs.push_back({"torus",         curved_flags,   [d](GEO::Mesh& m){ shapes::make_torus (m, 1.0f, 0.25f, 10 * d, 8 * d); }});
    jobs.push_back({"cylinder_thin", curved_flags,   [d](GEO::Mesh& m){ shapes::make_cylinder(m, -0.1f, 0.1f, 1.0f, true, true, 9 * d, 1 * d); }});
    jobs.push_back({"cylinder_tall", curved_flags,   [d](GEO::Mesh& m){ shapes::make_cylinder(m, -1.0f, 1.0f, 1.0f, true, true, 9 * d, 1 * d); }});
    jobs.push_back({"cone",          curved_flags,   [d](GEO::Mesh& m){ shapes::make_cone  (m, -1.0f, 1.0f, 1.0f, true, 10 * d, 5 * d); }});
    return jobs;
}

// Faithful copy of the editor's Json_library::make_geometry
// (src/editor/parsers/json_polyhedron.cpp): build a GEO::Mesh from one JSON
// polyhedron entry. The crucial difference from the basic shape generators is
// the GEO::mesh_repair() call -- the editor's ~92 concurrent Johnson-solid
// tasks each run mesh_repair, which the platonic/sphere/etc. builds never do.
void build_geometry_from_json(GEO::Mesh& mesh, const rapidjson::Value& json_mesh)
{
    try {
        const rapidjson::Value& json_vertices = json_mesh["vertex"];
        const std::size_t vertex_count = json_vertices.GetArray().Size();
        mesh.vertices.set_double_precision();
        mesh.vertices.create_vertices(static_cast<GEO::index_t>(vertex_count));
        {
            GEO::index_t vertex = 0;
            for (const rapidjson::Value& json_vertex : json_vertices.GetArray()) {
                const double x = json_vertex[0].GetDouble();
                const double y = json_vertex[1].GetDouble();
                const double z = json_vertex[2].GetDouble();
                mesh.vertices.point(vertex++) = GEO::vec3{x, y, z};
            }
        }
        const rapidjson::Value& json_faces = json_mesh["face"];
        for (const rapidjson::Value& json_face : json_faces.GetArray()) {
            const std::size_t  corner_count = json_face.GetArray().Size();
            const GEO::index_t facet = mesh.facets.create_polygon(static_cast<GEO::index_t>(corner_count));
            GEO::index_t local_facet_corner = 0;
            for (const rapidjson::Value& corner : json_face.GetArray()) {
                mesh.facets.set_vertex(facet, local_facet_corner, static_cast<GEO::index_t>(corner.GetInt()));
                ++local_facet_corner;
            }
        }
        GEO::mesh_repair(mesh, GEO::MESH_REPAIR_TOPOLOGY, 0.0);
        mesh.vertices.set_single_precision();
    } catch (...) {
        // Match the editor: a malformed entry yields no geometry. Clear so the
        // post-join validator does not flag a half-built (input-bad, not race-
        // corrupted) mesh as a false positive.
        mesh.clear();
    }
}

// Load johnson.json into a single shared rapidjson::Document, exactly like the
// editor builds one Json_library shared across all concurrent Johnson tasks.
[[nodiscard]] auto load_json_document(const std::string& path) -> std::unique_ptr<rapidjson::Document>
{
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return nullptr;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    auto doc = std::make_unique<rapidjson::Document>();
    doc->Parse(text.c_str(), text.length());
    if (doc->HasParseError() || !doc->IsObject()) {
        return nullptr;
    }
    return doc;
}

// Append one build job per polyhedron in the shared document. Each job's
// generator reads the SHARED const document concurrently (like the editor) and
// runs build_geometry_from_json (incl. mesh_repair).
void append_johnson_jobs(std::vector<Shape_job>& jobs, const rapidjson::Document& doc)
{
    const std::uint64_t johnson_flags =
        Geometry::process_flag_connect |
        Geometry::process_flag_build_edges |
        Geometry::process_flag_compute_facet_centroids |
        Geometry::process_flag_compute_smooth_vertex_normals |
        Geometry::process_flag_generate_facet_texture_coordinates;
    for (auto member = doc.MemberBegin(); member != doc.MemberEnd(); ++member) {
        std::string key = member->name.GetString();
        jobs.push_back({key, johnson_flags, [&doc, key](GEO::Mesh& mesh) {
            const auto entry = doc.FindMember(key.c_str());
            if (entry != doc.MemberEnd()) {
                build_geometry_from_json(mesh, entry->value);
            }
        }});
    }
}

void init_geometry_logging()
{
    // Route erhe::geometry's loggers somewhere valid (process()/the generators
    // deref these). Mirror src/erhe/geometry/test/main.cpp: reuse spdlog's
    // default logger rather than erhe::log's file sinks -- this is a console
    // tool. Keep it quiet (err) so a long soak does not flood adb shell.
    namespace g = erhe::geometry;
    std::shared_ptr<spdlog::logger> logger = spdlog::default_logger();
    g::log_geometry          = logger;
    g::log_geogram           = logger;
    g::log_build_edges       = logger;
    g::log_tangent_gen       = logger;
    g::log_cone              = logger;
    g::log_torus             = logger;
    g::log_sphere            = logger;
    g::log_polygon_texcoords = logger;
    g::log_interpolate       = logger;
    g::log_operation         = logger;
    g::log_catmull_clark     = logger;
    g::log_triangulate       = logger;
    g::log_subdivide         = logger;
    g::log_attribute_maps    = logger;
    g::log_merge             = logger;
    g::log_weld              = logger;
}

void init_geogram(bool multithread)
{
    // Identical to editor.cpp run_editor() Geogram init, except sys:multithread
    // is driven by the knob.
    GEO::initialize(GEO::GEOGRAM_INSTALL_NONE);
    GEO::CmdLine::import_arg_group("algo");
    GEO::CmdLine::set_arg("sys:multithread", multithread ? "true" : "false");
    GEO::geo_register_attribute_type<GEO::vec2f>("vec2f");
    GEO::geo_register_attribute_type<GEO::vec3f>("vec3f");
    GEO::geo_register_attribute_type<GEO::vec4f>("vec4f");
    GEO::geo_register_attribute_type<GEO::vec2i>("vec2i");
    GEO::geo_register_attribute_type<GEO::vec3i>("vec3i");
    GEO::geo_register_attribute_type<GEO::vec4i>("vec4i");
    GEO::geo_register_attribute_type<GEO::vec2u>("vec2u");
    GEO::geo_register_attribute_type<GEO::vec3u>("vec3u");
    GEO::geo_register_attribute_type<GEO::vec4u>("vec4u");
}

[[nodiscard]] auto parse_uint(const char* s, unsigned int fallback) -> unsigned int
{
    if (s == nullptr) {
        return fallback;
    }
    char* end = nullptr;
    const unsigned long v = std::strtoul(s, &end, 10);
    return (end == s) ? fallback : static_cast<unsigned int>(v);
}

// --- Minimal repro of the make_convex_hull Delaunay spin --------------------
//
// The editor's Brush::late_initialize() -> erhe::geometry::make_convex_hull()
// feeds a brush's vertices to a Geogram 3D Delaunay, which intermittently spins
// forever in locate_inexact() (the floating-point point-location walk). The
// cone is the trigger -- its coplanar base ring is a degenerate Delaunay
// configuration. The spin reproduces with BOTH the parallel (PDEL) and serial
// (BDEL) Delaunay (confirmed on-device), so it is NOT a parallel race: it is
// the inexact walk failing to terminate. This mode drives that exact call in a
// tight loop with a watchdog that flags any iteration that does not terminate.
[[nodiscard]] auto run_convex_hull_soak(
    unsigned int detail, unsigned int iters, const std::string& algo,
    double spin_timeout_s, unsigned int progress_every) -> int
{
    const int d = (detail < 1) ? 1 : static_cast<int>(detail);
    GEO::Mesh cone;
    // erhe Geometry meshes are single-precision, and make_cone writes float
    // points via set_pointf(), which needs the mesh in single precision FIRST
    // (a bare GEO::Mesh defaults to double precision -> set_pointf null-derefs).
    cone.vertices.set_single_precision();
    erhe::geometry::shapes::make_cone(cone, -1.0f, 1.0f, 1.0f, true, 10 * d, 5 * d);

    const GEO::index_t nb_pts = cone.vertices.nb();
    const GEO::index_t dim    = cone.vertices.dimension();
    GEO::vector<double> points;
    points.reserve(static_cast<std::size_t>(nb_pts) * dim);
    for (GEO::index_t v : cone.vertices) {
        const float* p = cone.vertices.single_precision_point_ptr(v);
        for (GEO::index_t c = 0; c < dim; ++c) {
            points.push_back(static_cast<double>(p[c]));
        }
    }

    std::printf(
        "convex-hull repro: cone nb_pts=%u dim=%u algo=%s iters=%u spin_timeout=%.1fs\n",
        nb_pts, dim, algo.c_str(), iters, spin_timeout_s
    );
    std::fflush(stdout);

    // Watchdog: mutex-guarded (no atomics, per project policy). The worker
    // records the start time before each Delaunay build; if the watchdog sees an
    // iteration exceed the timeout the build is spinning -> abort() so a
    // debuggerd tombstone captures the locate_inexact stack (same as the editor).
    std::mutex mtx;
    unsigned int cur_iter = 0;
    bool done = false;
    std::chrono::steady_clock::time_point iter_start = std::chrono::steady_clock::now();

    std::thread watchdog([&]() {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            std::lock_guard<std::mutex> lock(mtx);
            if (done) {
                return;
            }
            const double busy = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - iter_start).count();
            if (busy > spin_timeout_s) {
                std::printf(
                    "SPIN iter=%u algo=%s nb_pts=%u did not terminate in %.1fs "
                    "-- make_convex_hull Delaunay spin reproduced in isolation\n",
                    cur_iter, algo.c_str(), nb_pts, busy
                );
                std::fflush(stdout);
                std::abort();
            }
        }
    });

    for (unsigned int iter = 0; (iters == 0) || (iter < iters); ++iter) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            cur_iter   = iter;
            iter_start = std::chrono::steady_clock::now();
        }
        GEO::Delaunay_var delaunay = GEO::Delaunay::create(GEO::coord_index_t(dim), algo);
        delaunay->set_keeps_infinite(true);
        delaunay->set_vertices(nb_pts, points.data());
        if ((progress_every != 0) && (((iter + 1) % progress_every) == 0)) {
            std::printf("  convex-hull progress iter=%u (algo=%s)\n", iter + 1, algo.c_str());
            std::fflush(stdout);
        }
    }

    {
        std::lock_guard<std::mutex> lock(mtx);
        done = true;
    }
    watchdog.join();
    std::printf("CLEAN convex-hull iters=%u algo=%s nb_pts=%u (no spin)\n",
                iters, algo.c_str(), nb_pts);
    std::fflush(stdout);
    return 0;
}

void print_usage()
{
    std::printf(
        "geogram_soak -- headless Geogram parallel-init corruption soak\n"
        "  --workers N       worker threads (default: hardware concurrency)\n"
        "  --iters N         iterations; 0 = run forever (default 2000)\n"
        "  --batch N         shape-set repeats per iteration (default 8)\n"
        "  --detail N        shape tessellation detail (default 4, == editor)\n"
        "  --multithread X   Geogram sys:multithread: on|off (default on)\n"
        "  --johnson PATH    also build the Johnson solids from this johnson.json\n"
        "                    (each runs GEO::mesh_repair, like the editor)\n"
        "  --progress N      progress line every N iterations (default 100)\n"
        "  --verbose         lower the geometry log level to warn\n"
        "  --convex-hull     repro mode: loop make_convex_hull's Delaunay over\n"
        "                    the cone's vertices (the actual editor spin site)\n"
        "  --algo X          convex-hull Delaunay algorithm: BDEL (serial) |\n"
        "                    PDEL (parallel) (default BDEL; both reproduce)\n"
        "  --spin-timeout S  convex-hull mode: flag any Delaunay build that does\n"
        "                    not finish in S seconds as a spin (default 10)\n"
    );
}

} // namespace

int main(int argc, char** argv)
{
    unsigned int workers      = 0; // 0 -> hardware_concurrency below
    unsigned int iters        = 2000;
    unsigned int batch        = 8;
    unsigned int detail       = 4;
    unsigned int progress_every = 100;
    bool         multithread  = true;
    bool         verbose      = false;
    std::string  johnson_path;
    bool         convex_hull  = false;
    std::string  algo         = "BDEL";
    double       spin_timeout = 10.0;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        auto next = [&](void) -> const char* { return (i + 1 < argc) ? argv[++i] : nullptr; };
        if (std::strcmp(arg, "--workers") == 0) {
            workers = parse_uint(next(), workers);
        } else if (std::strcmp(arg, "--iters") == 0) {
            iters = parse_uint(next(), iters);
        } else if (std::strcmp(arg, "--batch") == 0) {
            batch = parse_uint(next(), batch);
        } else if (std::strcmp(arg, "--detail") == 0) {
            detail = parse_uint(next(), detail);
        } else if (std::strcmp(arg, "--progress") == 0) {
            progress_every = parse_uint(next(), progress_every);
        } else if (std::strcmp(arg, "--multithread") == 0) {
            const char* v = next();
            multithread = (v != nullptr) && (std::strcmp(v, "off") != 0) && (std::strcmp(v, "false") != 0) && (std::strcmp(v, "0") != 0);
        } else if (std::strcmp(arg, "--johnson") == 0) {
            const char* v = next();
            johnson_path = (v != nullptr) ? v : "";
        } else if (std::strcmp(arg, "--verbose") == 0) {
            verbose = true;
        } else if (std::strcmp(arg, "--convex-hull") == 0) {
            convex_hull = true;
        } else if (std::strcmp(arg, "--algo") == 0) {
            const char* v = next();
            if (v != nullptr) { algo = v; }
        } else if (std::strcmp(arg, "--spin-timeout") == 0) {
            const char* v = next();
            if (v != nullptr) { spin_timeout = std::strtod(v, nullptr); }
        } else if ((std::strcmp(arg, "--help") == 0) || (std::strcmp(arg, "-h") == 0)) {
            print_usage();
            return 0;
        } else {
            std::printf("Unknown argument: %s\n", arg);
            print_usage();
            return 1;
        }
    }

    if (workers == 0) {
        const unsigned int hc = std::thread::hardware_concurrency();
        workers = (hc == 0) ? 8u : hc;
    }
    if (batch == 0) {
        batch = 1;
    }

    init_geometry_logging();
    spdlog::set_level(verbose ? spdlog::level::warn : spdlog::level::err);
    init_geogram(multithread);

    if (convex_hull) {
        return run_convex_hull_soak(detail, iters, algo, spin_timeout, progress_every);
    }

    std::vector<Shape_job> jobs = build_shape_jobs(static_cast<int>(detail));

    // Shared across all Johnson tasks for the whole run -- must outlive the loop.
    std::unique_ptr<rapidjson::Document> johnson_doc;
    std::size_t johnson_count = 0;
    if (!johnson_path.empty()) {
        johnson_doc = load_json_document(johnson_path);
        if (!johnson_doc) {
            std::printf("ERROR: could not load/parse johnson json: %s\n", johnson_path.c_str());
            return 1;
        }
        const std::size_t before = jobs.size();
        append_johnson_jobs(jobs, *johnson_doc);
        johnson_count = jobs.size() - before;
    }

    const std::size_t per_iter = jobs.size() * batch;

    std::printf(
        "geogram_soak start: workers=%u multithread=%s iters=%u batch=%u detail=%u "
        "johnson=%zu shapes/iter=%zu\n",
        workers, multithread ? "on" : "off", iters, batch, detail, johnson_count, per_iter
    );
    std::fflush(stdout);

    tf::Executor executor{workers};

    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    std::uint64_t total_builds = 0;

    for (unsigned int iter = 0; (iters == 0) || (iter < iters); ++iter) {
        // Keep every built mesh alive past the join so validation runs OFF the
        // concurrent window (concurrent writes target distinct vector slots, so
        // this is race-free).
        std::vector<std::shared_ptr<Geometry>> geometries(per_iter);
        tf::Taskflow taskflow;
        for (std::size_t slot = 0; slot < per_iter; ++slot) {
            taskflow.emplace([slot, &jobs, &geometries]() {
                const Shape_job& job = jobs[slot % jobs.size()];
                std::shared_ptr<Geometry> geometry = std::make_shared<Geometry>(job.name);
                job.generate(geometry->get_mesh());
                geometry->process(job.flags);
                geometries[slot] = geometry;
            });
        }
        executor.run(taskflow).wait();
        total_builds += per_iter;

        for (std::size_t slot = 0; slot < per_iter; ++slot) {
            const std::shared_ptr<Geometry>& geometry = geometries[slot];
            if (!geometry) {
                continue;
            }
            const erhe::geometry::Mesh_structure_check check =
                erhe::geometry::validate_mesh_structure(geometry->get_mesh());
            if (!check.ok()) {
                const Shape_job& job = jobs[slot % jobs.size()];
                std::printf(
                    "CORRUPT iter=%u slot=%zu name=%s error=%s facets=%u verts=%u corners=%u "
                    "bad_facet=%u bad_facet_corner_count=%u corner_sum=%llu "
                    "workers=%u multithread=%s detail=%u total_builds=%llu\n",
                    iter, slot, job.name.c_str(), erhe::geometry::c_str(check.error),
                    check.facet_count, check.vertex_count, check.corner_count,
                    check.bad_facet, check.bad_facet_corner_count,
                    static_cast<unsigned long long>(check.corner_sum),
                    workers, multithread ? "on" : "off", detail,
                    static_cast<unsigned long long>(total_builds)
                );
                std::fflush(stdout);
                return 2;
            }
        }

        if ((progress_every != 0) && (((iter + 1) % progress_every) == 0)) {
            const double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t0).count();
            const double rate = (elapsed > 0.0) ? (static_cast<double>(total_builds) / elapsed) : 0.0;
            std::printf(
                "  progress iter=%u total_builds=%llu elapsed=%.1fs rate=%.0f builds/s\n",
                iter + 1, static_cast<unsigned long long>(total_builds), elapsed, rate
            );
            std::fflush(stdout);
        }
    }

    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count();
    std::printf(
        "CLEAN iters=%u total_builds=%llu workers=%u multithread=%s detail=%u elapsed=%.1fs\n",
        iters, static_cast<unsigned long long>(total_builds),
        workers, multithread ? "on" : "off", detail, elapsed
    );
    std::fflush(stdout);
    return 0;
}
