#include "renderers/lightmap_partitioner.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "renderers/lightmap_baker.hpp"
#include "renderers/lightmap_report.hpp"
#include "renderers/lightmap_streamer.hpp"
#include "renderers/lightmap_tile_io.hpp"
#include "operations/operation_stack.hpp"
#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/bake_transform.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include <fmt/format.h>
#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>

namespace editor {

namespace {

// Non-skinned lightmapped content meshes with a node - the set the split
// estimate partitions. Counted at launch and at commit so meshes added
// mid-flight can be warned about.
[[nodiscard]] auto count_lightmapped_meshes(Scene_root& scene_root) -> std::size_t
{
    std::size_t count = 0;
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root.layers().content()->meshes) {
        if (!mesh || mesh->skin) {
            continue;
        }
        if ((mesh->get_flag_bits() & erhe::Item_flags::lightmapped) == 0u) {
            continue;
        }
        if (mesh->get_node() == nullptr) {
            continue;
        }
        ++count;
    }
    return count;
}

} // anonymous namespace

// Snapshot + heavy-phase state of one asynchronous prepare. Everything the
// workers touch is captured here at launch on the main thread; the live
// partitioner members stay untouched until commit_prepare().
class Lightmap_partitioner::Prepare_job
{
public:
    class Region_task
    {
    public:
        class Piece_result
        {
        public:
            int                                         tile{-1};
            std::shared_ptr<erhe::primitive::Primitive> primitive; // empty = failed piece, skipped at commit
            int64_t                                     unwrap_us{0};
            int64_t                                     build_us{0};
        };
        std::size_t                                entry_index{0};
        std::size_t                                source_primitive_index{0};
        int                                        overflow_tile{-1};
        std::shared_ptr<erhe::geometry::Geometry>  source_geometry;
        glm::mat4                                  world_from_node{1.0f};
        std::shared_ptr<erhe::primitive::Material> material;
        erhe::primitive::Normal_style              normal_style{};
        std::string                                subject;
        // Per-facet chart order keys per tile (Params::chart_order entries
        // for this task's source primitive, extracted at launch).
        std::map<int, std::vector<float>>          chart_order;
        std::vector<Piece_result>                  results; // heavy-phase output, in piece order

        // Timing instrumentation (microseconds, filled by process_region):
        // estimates the gain of finer-grained parallelism - bake_clip_us is
        // the serial-per-region prefix, unwrap/build split per-piece work,
        // and max_piece_us bounds the critical path if pieces ran parallel.
        int64_t     bake_clip_us{0};
        int64_t     unwrap_us{0};
        int64_t     build_us{0};
        int64_t     max_piece_us{0};
        std::size_t clip_piece_count{0};
    };

    // Heavy phase for one piece: channel-2 re-unwrap + renderable/raytrace
    // primitive build. Writes only its own Piece_result slot (results are
    // index-addressed so pieces run as parallel subflow tasks); a slot whose
    // primitive stays empty is a failed/skipped piece, dropped at commit in
    // piece order - identical to the old push-back-on-success ordering, so
    // piece ordinals stay deterministic.
    // Unwrap density: world-space geometry, so the density is the world
    // density directly (no node-scale folding) - each piece unwraps at ITS
    // TILE's nominal density (tile_texture_size / cell side), so gutter
    // and minimum-chart sizing match the density the tile rasterizes at.
    // Mirror Make_atlas_operation's per-facet fallback so one degenerate
    // piece does not abort the whole partition.
    static void process_piece(
        const Params&                                     params,
        const erhe::primitive::Build_info&                build_info,
        const std::vector<float>&                         tile_texels_per_meter,
        Lightmap_report* const                            report,
        const Region_task&                                task,
        const erhe::geometry::operation::Clip_tile_piece& piece,
        Region_task::Piece_result&                        result
    )
    {
        using Clock = std::chrono::steady_clock;
        const auto elapsed_us = [](const Clock::time_point from, const Clock::time_point to) -> int64_t {
            return std::chrono::duration_cast<std::chrono::microseconds>(to - from).count();
        };

        if (!piece.geometry) {
            return;
        }
        if ((piece.tile < 0) || (piece.tile >= static_cast<int>(tile_texels_per_meter.size()))) {
            // Empty-quadrant leaf (tile -1): grid occupancy is AABB-
            // conservative, so nothing should route here; drop it
            // rather than bake an unaddressable piece.
            return;
        }
        const float tile_tpm = tile_texels_per_meter[static_cast<std::size_t>(piece.tile)];
        const std::string piece_subject = fmt::format("{}.tile{}", task.subject, piece.tile);
        // Reorder Charts By Bake: order keys measured on the previous
        // layout's piece, applied to this re-clip by facet id (the clip
        // is deterministic for unchanged sources).
        const auto order_it = task.chart_order.find(piece.tile);
        const std::vector<float>* const order_keys = (order_it != task.chart_order.end()) ? &order_it->second : nullptr;
        const Clock::time_point piece_start = Clock::now();
        std::shared_ptr<erhe::geometry::Geometry> atlas_geometry = std::make_shared<erhe::geometry::Geometry>(piece_subject);
        try {
            try {
                erhe::geometry::operation::make_atlas(
                    *piece.geometry.get(),
                    *atlas_geometry.get(),
                    2, // lightmap UV channel (texcoord usage_index 2)
                    static_cast<double>(params.hard_angles_deg),
                    params.parameterizer,
                    params.packer,
                    static_cast<double>(tile_tpm),
                    static_cast<double>(params.gutter_texels),
                    static_cast<double>(params.min_chart_texels),
                    order_keys
                );
            } catch (const std::exception& e) {
                if (params.parameterizer == erhe::geometry::operation::Atlas_parameterizer::per_facet) {
                    throw;
                }
                if (report != nullptr) {
                    report->add_warning(
                        Lightmap_report::Stage::partition,
                        piece_subject,
                        fmt::format("parameterizer failed ({}); fell back to per-facet unwrap", e.what())
                    );
                }
                erhe::geometry::operation::make_atlas(
                    *piece.geometry.get(),
                    *atlas_geometry.get(),
                    2,
                    static_cast<double>(params.hard_angles_deg),
                    erhe::geometry::operation::Atlas_parameterizer::per_facet,
                    params.packer,
                    static_cast<double>(tile_tpm),
                    static_cast<double>(params.gutter_texels),
                    static_cast<double>(params.min_chart_texels),
                    order_keys
                );
            }
        } catch (const std::exception& e) {
            result.unwrap_us = elapsed_us(piece_start, Clock::now());
            if (report != nullptr) {
                report->add_error(Lightmap_report::Stage::partition, piece_subject, fmt::format("unwrap failed: {}", e.what()));
            }
            return;
        }
        const Clock::time_point unwrap_end = Clock::now();
        result.unwrap_us = elapsed_us(piece_start, unwrap_end);

        std::shared_ptr<erhe::primitive::Primitive> piece_primitive = std::make_shared<erhe::primitive::Primitive>(atlas_geometry);
        const bool renderable_ok = piece_primitive->make_renderable_mesh(build_info, task.normal_style);
        const bool raytrace_ok   = renderable_ok && piece_primitive->make_raytrace();
        result.build_us = elapsed_us(unwrap_end, Clock::now());
        if (!renderable_ok || !raytrace_ok) {
            if (report != nullptr) {
                report->add_error(
                    Lightmap_report::Stage::partition,
                    piece_subject,
                    renderable_ok ? "raytrace build failed" : "renderable mesh build failed"
                );
            }
            return;
        }
        result.tile      = piece.tile;
        result.primitive = std::move(piece_primitive);
    }

    // Heavy phase for one region: world-space bake + kd clip on the calling
    // worker, then the per-piece work fans out as subflow child tasks
    // (join() blocks with the worker co-running children), so one large
    // mesh clipped across many tiles no longer serializes on a single
    // worker. Everything is independent: bake_transform and
    // clip_by_tile_tree reach no Geogram algorithm and are safe on distinct
    // destinations (clip_tile_tree.hpp thread-safety note; the tile tree is
    // read-only here, concurrent reads of a shared source geometry are
    // fine), make_atlas takes geogram_lock internally only around its
    // Geogram-parameterizer branch (per-facet unwraps run concurrently),
    // buffer-mesh allocation groups serialize on
    // buffer_mesh_allocation_mutex, and raytrace builds are worker-safe
    // (deferred glTF finalize does the same). Lightmap_report already takes
    // worker-thread reports (async UV unwrap failures).
    static void process_region(
        const Params&                                                 params,
        const erhe::primitive::Build_info&                            build_info,
        const std::vector<erhe::geometry::operation::Clip_tree_node>& tile_tree,
        const std::vector<float>&                                     tile_texels_per_meter,
        Lightmap_report* const                                        report,
        Region_task&                                                  task,
        const std::atomic<bool>&                                      cancel_requested,
        tf::Subflow* const                                            subflow
    )
    {
        using Clock = std::chrono::steady_clock;
        const auto elapsed_us = [](const Clock::time_point from, const Clock::time_point to) -> int64_t {
            return std::chrono::duration_cast<std::chrono::microseconds>(to - from).count();
        };
        const Clock::time_point region_start = Clock::now();

        erhe::geometry::Geometry world_geometry{fmt::format("{}.world", task.subject)};
        std::vector<erhe::geometry::operation::Clip_tile_piece> pieces;
        try {
            erhe::geometry::operation::bake_transform(
                *task.source_geometry.get(),
                world_geometry,
                erhe::geometry::to_geo_mat4f(task.world_from_node)
            );
            erhe::geometry::operation::clip_by_tile_tree(world_geometry, tile_tree, task.overflow_tile, pieces);
        } catch (const std::exception& e) {
            task.bake_clip_us = elapsed_us(region_start, Clock::now());
            if (report != nullptr) {
                report->add_error(Lightmap_report::Stage::partition, task.subject, fmt::format("clip failed: {}", e.what()));
            }
            return;
        }
        task.bake_clip_us     = elapsed_us(region_start, Clock::now());
        task.clip_piece_count = pieces.size();

        task.results.resize(pieces.size());
        if (subflow != nullptr) {
            for (std::size_t i = 0; i < pieces.size(); ++i) {
                subflow->emplace(
                    [&params, &build_info, &tile_texels_per_meter, report, &task, &cancel_requested, &pieces, i]() {
                        if (!cancel_requested.load(std::memory_order_relaxed)) {
                            process_piece(params, build_info, tile_texels_per_meter, report, task, pieces[i], task.results[i]);
                        }
                    }
                );
            }
            // join() keeps `pieces` (and the reference captures) alive until
            // every child ran; the worker co-runs children while blocked.
            subflow->join();
        } else {
            for (std::size_t i = 0; i < pieces.size(); ++i) {
                if (!cancel_requested.load(std::memory_order_relaxed)) {
                    process_piece(params, build_info, tile_texels_per_meter, report, task, pieces[i], task.results[i]);
                }
            }
        }

        for (const Region_task::Piece_result& result : task.results) {
            task.unwrap_us   += result.unwrap_us;
            task.build_us    += result.build_us;
            task.max_piece_us = std::max(task.max_piece_us, result.unwrap_us + result.build_us);
        }
    }

    Scene_root*                                             scene_root{nullptr};
    Params                                                  params{};
    int                                                     tile_texture_size{0};
    int                                                     resident_tile_budget{0};
    std::vector<erhe::geometry::operation::Clip_tree_node>  tile_tree;
    int                                                     tile_count{0};
    std::vector<Lightmap_baker::Tile>                       tile_descs;
    std::vector<float>                                      tile_texels_per_meter; // index-aligned with tile_descs
    std::optional<erhe::primitive::Build_info>              build_info; // Buffer_info holds references - no default construction
    std::vector<Original_entry>                             entries;    // skeletons; piece nodes/meshes filled at commit
    std::vector<Region_task>                                tasks;
    std::size_t                                             lightmapped_mesh_count_at_launch{0};
    std::chrono::steady_clock::time_point                   launch_time{};
    std::atomic<std::size_t>                                regions_done{0};
    std::atomic<bool>                                       cancel_requested{false};
    tf::Taskflow                                            taskflow;   // must outlive the run
    tf::Future<void>                                        future;
};

Lightmap_partitioner::Lightmap_partitioner(App_context& context)
    : m_context{context}
{
}

Lightmap_partitioner::~Lightmap_partitioner()
{
    // Editor teardown: do not destroy a running taskflow. No reports or
    // counter bookkeeping - everything is going away.
    if (m_prepare_job) {
        m_prepare_job->cancel_requested.store(true, std::memory_order_relaxed);
        if (m_prepare_job->future.valid()) {
            m_prepare_job->future.cancel();
            m_prepare_job->future.wait();
        }
    }
}

auto Lightmap_partitioner::request_prepare(
    Scene_root&   scene_root,
    const Params& params,
    const int     tile_texture_size,
    const int     resident_tile_budget
) -> bool
{
    ERHE_PROFILE_FUNCTION();

    const std::chrono::steady_clock::time_point request_start = std::chrono::steady_clock::now();

    Lightmap_baker* const  baker  = m_context.lightmap_baker;
    Lightmap_report* const report = m_context.lightmap_report;
    if ((baker == nullptr) || (m_context.mesh_memory == nullptr)) {
        return false;
    }
    if (m_prepare_job) {
        if (report != nullptr) {
            report->add_error(Lightmap_report::Stage::partition, "prepare", "prepare already in flight - cancel or wait first");
        }
        return false;
    }
    // Defense in depth (callers guard too): a queued mesh operation would
    // swap source primitives mid-flight; the commit-time validation would
    // catch it, but refusing up front gives a better error.
    {
        const std::size_t async_ops =
            m_context.get_async_in_flight_count();
        if (async_ops > 0) {
            if (report != nullptr) {
                report->add_error(Lightmap_report::Stage::partition, "prepare", "mesh operations in flight - wait until they settle");
            }
            return false;
        }
    }
    if (report != nullptr) {
        report->clear_stage(Lightmap_report::Stage::partition);
    }
    // The split estimate sizes against the baker's tile size - apply the
    // requested config before computing it (snapshot re-applied at commit).
    baker->set_tile_config(tile_texture_size, resident_tile_budget);

    // The tile partition comes from a geometry-only split ESTIMATE of the
    // original meshes (world areas at a nominal chart coverage): no unwrap
    // or prior layout is needed, and the partitioned relayout at commit
    // re-packs every piece with its measured UVs.
    const std::chrono::steady_clock::time_point estimate_start = std::chrono::steady_clock::now();
    const Lightmap_baker::Estimate_split split = baker->compute_tile_split_estimate(scene_root);
    const std::chrono::steady_clock::time_point estimate_end = std::chrono::steady_clock::now();
    if (split.empty()) {
        if (report != nullptr) {
            report->add_error(Lightmap_report::Stage::partition, "prepare", "no lightmapped meshes to partition");
        }
        return false;
    }

    // Group the estimate regions by source mesh: one piece mesh per original
    // mesh, one Mesh_primitive per (source primitive, overlapped tile).
    class Mesh_group
    {
    public:
        std::shared_ptr<erhe::scene::Mesh>                  mesh;
        std::vector<const Lightmap_baker::Estimate_region*> regions;
    };
    std::vector<Mesh_group> groups;
    for (const Lightmap_baker::Estimate_region& region : split.regions) {
        if (!region.mesh) {
            continue;
        }
        auto it = std::find_if(groups.begin(), groups.end(), [&region](const Mesh_group& group) {
            return group.mesh == region.mesh;
        });
        if (it == groups.end()) {
            groups.push_back(Mesh_group{region.mesh, {}});
            it = groups.end() - 1;
        }
        it->regions.push_back(&region);
    }

    // Snapshot everything the heavy phase needs; the live members
    // (m_entries, m_tile_tree, ...) stay untouched until commit, so the old
    // partition keeps rendering while the new one computes.
    std::unique_ptr<Prepare_job> job = std::make_unique<Prepare_job>();
    job->scene_root           = &scene_root;
    job->params               = params;
    job->tile_texture_size    = tile_texture_size;
    job->resident_tile_budget = resident_tile_budget;
    job->tile_tree            = split.kd_nodes;
    job->tile_count           = split.tile_count;
    job->tile_descs           = split.tiles;
    job->tile_texels_per_meter.reserve(split.tiles.size());
    for (const Lightmap_baker::Tile& tile : split.tiles) {
        job->tile_texels_per_meter.push_back(tile.texels_per_meter);
    }
    job->build_info.emplace(
        erhe::primitive::Build_info{
            .primitive_types = {
                .fill_triangles          = true,
                .fill_triangles_expanded = true,
                .edge_lines              = true,
                .corner_points           = true,
                .centroid_points         = true
            },
            .buffer_info = m_context.mesh_memory->make_primitive_buffer_info()
        }
    );
    job->lightmapped_mesh_count_at_launch = count_lightmapped_meshes(scene_root);

    for (const Mesh_group& group : groups) {
        erhe::scene::Node* const node = group.mesh->get_node();
        if (node == nullptr) {
            continue;
        }
        const glm::mat4 world_from_node = node->world_from_node();

        const std::size_t entry_index = job->entries.size();
        Original_entry entry;
        entry.original_mesh           = group.mesh;
        entry.world_from_node_at_clip = world_from_node;
        job->entries.push_back(std::move(entry));

        const std::vector<erhe::scene::Mesh_primitive>& source_primitives = group.mesh->get_primitives();
        for (const Lightmap_baker::Estimate_region* region : group.regions) {
            if (region->primitive_index >= source_primitives.size()) {
                continue;
            }
            const erhe::scene::Mesh_primitive& source_mesh_primitive = source_primitives[region->primitive_index];
            if (!source_mesh_primitive.primitive || !source_mesh_primitive.primitive->render_shape) {
                continue;
            }
            const std::shared_ptr<erhe::primitive::Primitive_render_shape>& render_shape = source_mesh_primitive.primitive->render_shape;
            const std::shared_ptr<erhe::geometry::Geometry>& source_geometry = render_shape->get_geometry();
            if (!source_geometry) {
                continue;
            }
            // Chart order keys for this source primitive, per tile
            // (Reorder Charts By Bake; empty for ordinary prepares).
            std::map<int, std::vector<float>> task_chart_order;
            if (params.chart_order) {
                for (const auto& [identity, keys] : *params.chart_order) {
                    if ((std::get<0>(identity) == group.mesh.get()) && (std::get<1>(identity) == region->primitive_index)) {
                        task_chart_order.emplace(std::get<2>(identity), keys);
                    }
                }
            }
            job->tasks.push_back(
                Prepare_job::Region_task{
                    .entry_index            = entry_index,
                    .source_primitive_index = region->primitive_index,
                    .overflow_tile          = region->tile,
                    .source_geometry        = source_geometry,
                    .world_from_node        = world_from_node,
                    .material               = source_mesh_primitive.material,
                    .normal_style           = render_shape->get_normal_style(),
                    .subject                = fmt::format("{}[{}]", group.mesh->get_name(), region->primitive_index),
                    .chart_order            = std::move(task_chart_order),
                    .results                = {}
                }
            );
        }
    }
    if (job->tasks.empty()) {
        if (report != nullptr) {
            report->add_error(Lightmap_report::Stage::partition, "prepare", "no source primitives to partition");
        }
        return false;
    }

    // Held for the whole flight so every async_busy gate (UI buttons, MCP
    // guards, deferred reorder) also covers an in-flight prepare; released
    // on commit / abort / discard, all on the main thread.
    ++m_context.pending_async_ops;
    m_prepare_job = std::move(job);
    Prepare_job* const raw_job = m_prepare_job.get();

    raw_job->launch_time = std::chrono::steady_clock::now();
    {
        const auto ms = [](const std::chrono::steady_clock::time_point from, const std::chrono::steady_clock::time_point to) {
            return static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(to - from).count()) / 1000.0;
        };
        log_render->info(
            "Lightmap_partitioner: prepare timing: split estimate {:.1f} ms, snapshot {:.1f} ms (main thread, {} regions)",
            ms(estimate_start, estimate_end),
            ms(request_start, raw_job->launch_time) - ms(estimate_start, estimate_end),
            raw_job->tasks.size()
        );
    }

    if ((m_context.executor == nullptr) || (raw_job->tasks.size() <= 1)) {
        // Synchronous fallback: run inline (no subflow) and commit before
        // returning.
        for (Prepare_job::Region_task& task : raw_job->tasks) {
            if (!raw_job->cancel_requested.load(std::memory_order_relaxed)) {
                Prepare_job::process_region(raw_job->params, *raw_job->build_info, raw_job->tile_tree, raw_job->tile_texels_per_meter, report, task, raw_job->cancel_requested, nullptr);
            }
            raw_job->regions_done.fetch_add(1, std::memory_order_relaxed);
        }
        commit_prepare();
        return m_last_prepare_result.committed;
    }

    // One task per region; each fans its per-piece work out as subflow
    // children, so a single large mesh clipped across many tiles cannot pin
    // the whole prepare on one worker.
    for (std::size_t i = 0; i < raw_job->tasks.size(); ++i) {
        raw_job->taskflow.emplace(
            [raw_job, report, i](tf::Subflow& subflow) {
                if (!raw_job->cancel_requested.load(std::memory_order_relaxed)) {
                    Prepare_job::process_region(raw_job->params, *raw_job->build_info, raw_job->tile_tree, raw_job->tile_texels_per_meter, report, raw_job->tasks[i], raw_job->cancel_requested, &subflow);
                }
                raw_job->regions_done.fetch_add(1, std::memory_order_relaxed);
            }
        );
    }
    raw_job->future = m_context.executor->run(raw_job->taskflow);
    log_render->info("Lightmap_partitioner: prepare launched ({} regions)", raw_job->tasks.size());
    return true;
}

void Lightmap_partitioner::update()
{
    if (!m_prepare_job) {
        return;
    }
    if (m_prepare_job->future.valid() && (m_prepare_job->future.wait_for(std::chrono::seconds{0}) != std::future_status::ready)) {
        return;
    }
    if (m_prepare_job->cancel_requested.load(std::memory_order_relaxed)) {
        finish_prepare_discard("prepare cancelled");
        return;
    }
    commit_prepare();
}

void Lightmap_partitioner::cancel_prepare()
{
    if (!m_prepare_job) {
        return;
    }
    // The per-region check in the taskflow lambda is the real mechanism;
    // future.cancel() additionally drops not-yet-scheduled chunk tasks.
    m_prepare_job->cancel_requested.store(true, std::memory_order_relaxed);
    if (m_prepare_job->future.valid()) {
        m_prepare_job->future.cancel();
    }
}

auto Lightmap_partitioner::prepare(
    Scene_root&   scene_root,
    const Params& params,
    const int     tile_texture_size,
    const int     resident_tile_budget
) -> bool
{
    if (!request_prepare(scene_root, params, tile_texture_size, resident_tile_budget)) {
        return false;
    }
    if (m_prepare_job) {
        // Workers need no main-thread service (buffer uploads just queue for
        // the next frame flush), so a plain wait cannot deadlock.
        if (m_prepare_job->future.valid()) {
            m_prepare_job->future.wait();
        }
        update();
    }
    return m_last_prepare_result.committed;
}

auto Lightmap_partitioner::get_prepare_progress() const -> Prepare_progress
{
    Prepare_progress progress;
    if (m_prepare_job) {
        progress.in_flight        = true;
        progress.regions_done     = m_prepare_job->regions_done.load(std::memory_order_relaxed);
        progress.regions_total    = m_prepare_job->tasks.size();
        progress.cancel_requested = m_prepare_job->cancel_requested.load(std::memory_order_relaxed);
    }
    return progress;
}

auto Lightmap_partitioner::validate_job_against_scene(const Prepare_job& job) const -> std::string
{
    // Structural changes abort (the pieces were clipped from geometry that
    // no longer exists in the scene); transform moves are tolerated - the
    // existing count_stale_transforms() warning covers them, matching the
    // synchronous flow's move-after-prepare behavior.
    for (const Prepare_job::Region_task& task : job.tasks) {
        const Original_entry& entry = job.entries[task.entry_index];
        const std::shared_ptr<erhe::scene::Mesh>& mesh = entry.original_mesh;
        if (!mesh) {
            return "source mesh dropped";
        }
        if (mesh->get_item_host() != job.scene_root) {
            return fmt::format("mesh '{}' left the scene", mesh->get_name());
        }
        if (mesh->get_node() == nullptr) {
            return fmt::format("mesh '{}' lost its node", mesh->get_name());
        }
        const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
        if (task.source_primitive_index >= primitives.size()) {
            return fmt::format("mesh '{}' primitive list changed", mesh->get_name());
        }
        const erhe::scene::Mesh_primitive& mesh_primitive = primitives[task.source_primitive_index];
        if (!mesh_primitive.primitive ||
            !mesh_primitive.primitive->render_shape ||
            (mesh_primitive.primitive->render_shape->get_geometry() != task.source_geometry)) {
            return fmt::format("mesh '{}' primitive {} geometry swapped", mesh->get_name(), task.source_primitive_index);
        }
    }
    return {};
}

void Lightmap_partitioner::commit_prepare()
{
    ERHE_PROFILE_FUNCTION();

    std::unique_ptr<Prepare_job> job = std::move(m_prepare_job); // future is ready - safe to own/destroy
    Lightmap_baker* const  baker  = m_context.lightmap_baker;
    Lightmap_report* const report = m_context.lightmap_report;
    Scene_root&            scene_root = *job->scene_root;

    using Clock = std::chrono::steady_clock;
    const auto to_ms = [](const int64_t us) { return static_cast<double>(us) / 1000.0; };
    const Clock::time_point commit_start = Clock::now();

    // Heavy-phase timing summary: measures how the parallel-per-region work
    // distributed, and estimates what per-piece parallelism would gain (the
    // per-piece critical path = each region's serial bake+clip prefix plus
    // its single slowest piece).
    {
        int64_t sum_bake_clip = 0;
        int64_t sum_unwrap    = 0;
        int64_t sum_build     = 0;
        int64_t max_piece_parallel_path = 0;
        std::vector<const Prepare_job::Region_task*> by_total;
        by_total.reserve(job->tasks.size());
        for (const Prepare_job::Region_task& task : job->tasks) {
            sum_bake_clip += task.bake_clip_us;
            sum_unwrap    += task.unwrap_us;
            sum_build     += task.build_us;
            max_piece_parallel_path = std::max(max_piece_parallel_path, task.bake_clip_us + task.max_piece_us);
            by_total.push_back(&task);
        }
        std::sort(
            by_total.begin(), by_total.end(),
            [](const Prepare_job::Region_task* lhs, const Prepare_job::Region_task* rhs) {
                return (lhs->bake_clip_us + lhs->unwrap_us + lhs->build_us) > (rhs->bake_clip_us + rhs->unwrap_us + rhs->build_us);
            }
        );
        const double wall_ms = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(commit_start - job->launch_time).count()) / 1000.0;
        const int64_t sum_total = sum_bake_clip + sum_unwrap + sum_build;
        log_render->info(
            "Lightmap_partitioner: prepare heavy phase: wall {:.1f} ms, worker cpu {:.1f} ms (x{:.2f}): bake+clip {:.1f} ms, unwrap {:.1f} ms, primitive build {:.1f} ms; est. per-piece-parallel critical path {:.1f} ms",
            wall_ms,
            to_ms(sum_total),
            (wall_ms > 0.0) ? (to_ms(sum_total) / wall_ms) : 0.0,
            to_ms(sum_bake_clip), to_ms(sum_unwrap), to_ms(sum_build),
            to_ms(max_piece_parallel_path)
        );
        const std::size_t top_count = std::min<std::size_t>(5, by_total.size());
        for (std::size_t i = 0; i < top_count; ++i) {
            const Prepare_job::Region_task& task = *by_total[i];
            log_render->info(
                "Lightmap_partitioner:   region {} '{}': total {:.1f} ms (bake+clip {:.1f}, unwrap {:.1f}, build {:.1f}), {} pieces, max piece {:.1f} ms",
                i, task.subject,
                to_ms(task.bake_clip_us + task.unwrap_us + task.build_us),
                to_ms(task.bake_clip_us), to_ms(task.unwrap_us), to_ms(task.build_us),
                task.clip_piece_count,
                to_ms(task.max_piece_us)
            );
        }
    }

    const std::string stale_reason = validate_job_against_scene(*job);
    if (!stale_reason.empty()) {
        if (report != nullptr) {
            report->add_error(
                Lightmap_report::Stage::partition,
                "prepare",
                fmt::format("scene changed during prepare ({}) - re-run Prepare", stale_reason)
            );
        }
        m_last_prepare_result = Prepare_result{.committed = false, .mesh_count = 0, .piece_count = 0, .tile_count = 0, .abort_reason = stale_reason};
        --m_context.pending_async_ops;
        return; // old partition untouched
    }

    const Clock::time_point validate_end = Clock::now();

    // Assemble piece meshes in task order (identical to the synchronous
    // order; ordinals count successful pieces per (source mesh, source
    // primitive) region), dropping entries with no pieces.
    std::vector<Original_entry> entries = std::move(job->entries);
    std::vector<std::vector<erhe::scene::Mesh_primitive>> piece_primitives_per_entry(entries.size());
    for (Prepare_job::Region_task& task : job->tasks) {
        // Source identity snapshot for staleness detection (geometry swaps
        // by mesh operations, next to the transform snapshot).
        entries[task.entry_index].source_geometries_at_clip.emplace_back(task.source_primitive_index, task.source_geometry.get());
        int ordinal = 0;
        for (Prepare_job::Region_task::Piece_result& result : task.results) {
            if (!result.primitive) {
                continue; // failed/skipped piece slot (reported in the heavy phase)
            }
            piece_primitives_per_entry[task.entry_index].emplace_back(std::move(result.primitive), task.material);
            entries[task.entry_index].pieces.push_back(
                Piece_info{
                    .tile                   = result.tile,
                    .source_primitive_index = task.source_primitive_index,
                    .ordinal                = ordinal++
                }
            );
        }
    }
    std::size_t total_pieces = 0;
    {
        std::vector<Original_entry> kept;
        kept.reserve(entries.size());
        for (std::size_t entry_index = 0; entry_index < entries.size(); ++entry_index) {
            Original_entry& entry = entries[entry_index];
            std::vector<erhe::scene::Mesh_primitive>& piece_primitives = piece_primitives_per_entry[entry_index];
            if (piece_primitives.empty()) {
                continue;
            }
            total_pieces += piece_primitives.size();
            erhe::scene::Node* const node = entry.original_mesh->get_node();
            entry.piece_node = std::make_shared<erhe::scene::Node>(fmt::format("{}.lm", (node != nullptr) ? node->get_name() : entry.original_mesh->get_name()));
            entry.piece_mesh = std::make_shared<erhe::scene::Mesh>(fmt::format("{}.lm", entry.original_mesh->get_name()));
            entry.piece_mesh->layer_id = scene_root.layers().content()->id;
            entry.piece_mesh->set_primitives(piece_primitives);
            // Render proxies: draw + cast shadows in place of the source
            // (apply_visibility toggles their visible flag), but stay out
            // of the item tree (no show_in_ui), the ID buffer (no id flag),
            // raytrace picking (render_proxy -> mask 0 in
            // raytrace_node_mask) and glTF export. Selection and editing
            // always target the proxy_hidden original.
            entry.piece_mesh->enable_flag_bits(
                erhe::Item_flags::content      |
                erhe::Item_flags::shadow_cast  |
                erhe::Item_flags::render_proxy
            );
            entry.piece_node->attach(entry.piece_mesh);
            // Piece nodes hold world-space geometry on a static identity
            // transform: skip the per-frame transform update pass and lock
            // them against the viewport transform tool.
            entry.piece_node->enable_flag_bits(
                erhe::Item_flags::content                 |
                erhe::Item_flags::visible                 |
                erhe::Item_flags::render_proxy            |
                erhe::Item_flags::no_transform_update     |
                erhe::Item_flags::lock_viewport_transform
            );
            kept.push_back(std::move(entry));
        }
        entries = std::move(kept);
    }

    if (entries.empty()) {
        if (report != nullptr) {
            report->add_error(Lightmap_report::Stage::partition, "prepare", "no pieces produced");
        }
        m_last_prepare_result = Prepare_result{.committed = false, .mesh_count = 0, .piece_count = 0, .tile_count = 0, .abort_reason = "no pieces produced"};
        --m_context.pending_async_ops;
        return; // old partition kept
    }

    // Re-apply the launch-time tile config: the editor tick pushes the live
    // settings into the baker every frame, so a value the user changed
    // mid-flight would otherwise size the relayout differently than the
    // split estimate that produced the pieces.
    if (baker != nullptr) {
        baker->set_tile_config(job->tile_texture_size, job->resident_tile_budget);
    }

    // Swap the partition: tear down the old one (if any) and install the new
    // one with no relayout/publish in between, so nothing renders an
    // intermediate empty layout.
    if (is_prepared() && (m_scene_root != nullptr)) {
        teardown_scene_state();
    }
    clear_store();
    {
        // Commit: group node with identity transform at the scene root; the
        // pieces are world-space geometry, so identity world_from_node is
        // what makes them render (and raytrace) in place.
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root.item_host_mutex};
        m_group_node = std::make_shared<erhe::scene::Node>("Lightmap Pieces");
        m_group_node->enable_flag_bits(
            erhe::Item_flags::content                 |
            erhe::Item_flags::visible                 |
            erhe::Item_flags::render_proxy            |
            erhe::Item_flags::no_transform_update     |
            erhe::Item_flags::lock_viewport_transform
        );
        m_group_node->set_parent(scene_root.get_scene().get_root_node());
        for (Original_entry& entry : entries) {
            entry.piece_node->set_parent(m_group_node);
            m_piece_meshes.insert(entry.piece_mesh.get());
        }
    }
    m_entries     = std::move(entries);
    m_scene_root  = &scene_root;
    m_last_params = job->params;
    m_tile_tree   = std::move(job->tile_tree);
    m_tile_count  = job->tile_count;
    m_tile_descs  = std::move(job->tile_descs);
    apply_visibility();
    const Clock::time_point swap_end = Clock::now();

    // Switch the baker layout over to the pieces (the partitioned branch of
    // update_layout is active now that the store is populated) and push
    // their atlas mappings (white-fallback sentinel for non-resident tiles)
    // onto the piece Mesh_primitives.
    if (baker != nullptr) {
        if (baker->update_layout(scene_root, job->params.min_face_texels)) {
            baker->publish_regions();
        }
        // The display atlas still holds the previous bake at the previous
        // packing - the fresh mappings would sample rubbish; show the
        // unbaked white look instead.
        baker->clear_display_to_white();
        // Fresh lighting right away: one full sweep of every tile, then
        // pause (with the pause autosave writing the new packing to disk).
        if (baker->is_bake_supported()) {
            baker->request_single_iteration();
        }
    }
    // Manifest regions that are pieces resolve through this partition; make
    // the streamer re-apply mappings against the new piece set.
    if (m_context.lightmap_streamer != nullptr) {
        m_context.lightmap_streamer->invalidate();
    }
    const Clock::time_point relayout_end = Clock::now();
    {
        const auto span_ms = [](const Clock::time_point from, const Clock::time_point to) {
            return static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(to - from).count()) / 1000.0;
        };
        log_render->info(
            "Lightmap_partitioner: commit (main thread): validate+stats {:.1f} ms, assemble+swap {:.1f} ms, relayout+publish {:.1f} ms, total {:.1f} ms",
            span_ms(commit_start, validate_end),
            span_ms(validate_end, swap_end),
            span_ms(swap_end, relayout_end),
            span_ms(commit_start, relayout_end)
        );
    }

    const std::size_t lightmapped_now = count_lightmapped_meshes(scene_root);
    if ((report != nullptr) && (lightmapped_now > job->lightmapped_mesh_count_at_launch)) {
        report->add_warning(
            Lightmap_report::Stage::partition,
            "prepare",
            fmt::format(
                "{} lightmapped meshes added during prepare are not partitioned - re-run Prepare",
                lightmapped_now - job->lightmapped_mesh_count_at_launch
            )
        );
    }

    m_last_prepare_result = Prepare_result{
        .committed    = true,
        .mesh_count   = m_entries.size(),
        .piece_count  = total_pieces,
        .tile_count   = m_tile_count,
        .abort_reason = {}
    };
    --m_context.pending_async_ops;

    log_render->info(
        "Lightmap_partitioner: {} source meshes partitioned into {} world-space pieces across {} tiles",
        m_entries.size(), total_pieces, m_tile_count
    );
}

void Lightmap_partitioner::finish_prepare_discard(const char* const reason)
{
    Lightmap_report* const report = m_context.lightmap_report;
    if (report != nullptr) {
        report->add_warning(Lightmap_report::Stage::partition, "prepare", reason);
    }
    m_last_prepare_result = Prepare_result{.committed = false, .mesh_count = 0, .piece_count = 0, .tile_count = 0, .abort_reason = reason};
    --m_context.pending_async_ops;
    m_prepare_job.reset();
    log_render->info("Lightmap_partitioner: {}", reason);
}

void Lightmap_partitioner::teardown_scene_state()
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{m_scene_root->item_host_mutex};
    for (Original_entry& entry : m_entries) {
        if (entry.original_mesh) {
            entry.original_mesh->disable_flag_bits(erhe::Item_flags::proxy_hidden);
        }
        if (entry.piece_node) {
            entry.piece_node->set_parent(std::shared_ptr<erhe::Hierarchy>{});
        }
    }
    if (m_group_node) {
        m_group_node->set_parent(std::shared_ptr<erhe::Hierarchy>{});
    }
}

void Lightmap_partitioner::clear_store()
{
    // m_last_params is kept - revert()'s trailing relayout uses it.
    m_entries.clear();
    m_piece_meshes.clear();
    m_group_node.reset();
    m_tile_tree.clear();
    m_tile_count = 0;
    m_tile_descs.clear();
    m_scene_root = nullptr;
}

void Lightmap_partitioner::revert()
{
    if (!is_prepared() || (m_scene_root == nullptr)) {
        clear_store();
        return;
    }
    teardown_scene_state();
    Scene_root* const scene_root = m_scene_root;
    clear_store();
    // With the partition gone there is no layout source left; this clears
    // the baker layout (originals render unlit) until a re-prepare runs.
    if ((m_context.lightmap_baker != nullptr) && (scene_root != nullptr)) {
        if (m_context.lightmap_baker->update_layout(*scene_root, m_last_params.min_face_texels)) {
            m_context.lightmap_baker->publish_regions();
        }
    }
    if (m_context.lightmap_streamer != nullptr) {
        m_context.lightmap_streamer->invalidate();
    }
    log_render->info("Lightmap_partitioner: partition reverted");
}

void Lightmap_partitioner::set_render_with_lightmaps(const bool enabled)
{
    if (m_render_with_lightmaps == enabled) {
        return;
    }
    m_render_with_lightmaps = enabled;
    apply_visibility();
}

void Lightmap_partitioner::apply_visibility()
{
    if (!is_prepared() || (m_scene_root == nullptr)) {
        return;
    }
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{m_scene_root->item_host_mutex};
    for (Original_entry& entry : m_entries) {
        if (entry.original_mesh) {
            // The original stays fully live - visible (so ID render and
            // raytrace picking keep working; selection/editing always
            // target it), just excluded from the visual + shadow passes
            // while its render proxy draws instead.
            if (m_render_with_lightmaps) {
                entry.original_mesh->enable_flag_bits(erhe::Item_flags::proxy_hidden);
            } else {
                entry.original_mesh->disable_flag_bits(erhe::Item_flags::proxy_hidden);
            }
        }
        if (entry.piece_mesh) {
            if (m_render_with_lightmaps) {
                entry.piece_mesh->enable_flag_bits(erhe::Item_flags::visible);
            } else {
                entry.piece_mesh->disable_flag_bits(erhe::Item_flags::visible);
            }
        }
    }
}

namespace {

[[nodiscard]] auto entry_geometry_stale(const Lightmap_partitioner::Original_entry& entry) -> bool
{
    if (!entry.original_mesh) {
        return false;
    }
    const std::vector<erhe::scene::Mesh_primitive>& primitives = entry.original_mesh->get_primitives();
    for (const auto& [primitive_index, geometry_at_clip] : entry.source_geometries_at_clip) {
        const erhe::geometry::Geometry* current = nullptr;
        if (primitive_index < primitives.size()) {
            const erhe::primitive::Primitive* const primitive = primitives[primitive_index].primitive.get();
            if ((primitive != nullptr) && primitive->render_shape) {
                current = primitive->render_shape->get_geometry().get();
            }
        }
        if (current != geometry_at_clip) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

auto Lightmap_partitioner::count_stale_sources() const -> std::size_t
{
    std::size_t stale = 0;
    for (const Original_entry& entry : m_entries) {
        const erhe::scene::Node* const node = entry.original_mesh ? entry.original_mesh->get_node() : nullptr;
        if (node == nullptr) {
            continue;
        }
        if ((node->world_from_node() != entry.world_from_node_at_clip) || entry_geometry_stale(entry)) {
            ++stale;
        }
    }
    return stale;
}

auto Lightmap_partitioner::get_source_state_hash() const -> uint64_t
{
    uint64_t hash = 0xcbf29ce484222325ull;
    const auto mix = [&hash](const void* data, const std::size_t byte_count) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (std::size_t i = 0; i < byte_count; ++i) {
            hash = (hash ^ bytes[i]) * 0x100000001b3ull;
        }
    };
    for (const Original_entry& entry : m_entries) {
        const erhe::scene::Node* const node = entry.original_mesh ? entry.original_mesh->get_node() : nullptr;
        if (node == nullptr) {
            continue;
        }
        const glm::mat4 world_from_node = node->world_from_node();
        mix(&world_from_node, sizeof(world_from_node));
        const std::vector<erhe::scene::Mesh_primitive>& primitives = entry.original_mesh->get_primitives();
        for (const erhe::scene::Mesh_primitive& mesh_primitive : primitives) {
            const erhe::primitive::Primitive* const primitive = mesh_primitive.primitive.get();
            const erhe::geometry::Geometry* const geometry =
                ((primitive != nullptr) && primitive->render_shape) ? primitive->render_shape->get_geometry().get() : nullptr;
            mix(&geometry, sizeof(geometry));
        }
    }
    return hash;
}

auto Lightmap_partitioner::find_piece(
    const std::string& node_path,
    const std::string& node_index_path,
    const std::string& mesh_name,
    const std::size_t  source_primitive_index,
    const int          tile,
    const int          ordinal
) const -> std::pair<erhe::scene::Mesh*, std::size_t>
{
    for (const Original_entry& entry : m_entries) {
        if (!entry.original_mesh || !entry.piece_mesh) {
            continue;
        }
        if (entry.original_mesh->get_name() != mesh_name) {
            continue;
        }
        const erhe::scene::Node* const node = entry.original_mesh->get_node();
        if (!node_index_path.empty()) {
            if (Lightmap_tile_io::node_index_path(node) != node_index_path) {
                continue;
            }
        } else if (Lightmap_tile_io::node_path(node) != node_path) {
            continue;
        }
        for (std::size_t piece_index = 0; piece_index < entry.pieces.size(); ++piece_index) {
            const Piece_info& piece = entry.pieces[piece_index];
            if ((piece.tile == tile) && (piece.source_primitive_index == source_primitive_index) && (piece.ordinal == ordinal)) {
                return {entry.piece_mesh.get(), piece_index};
            }
        }
    }
    return {nullptr, 0};
}

void Lightmap_partitioner::on_scene_closed(const Scene_root* scene_root)
{
    // An in-flight job targeting this scene must not commit: cancel, wait
    // for the workers to drain (bounded - each worker finishes at most its
    // current region), and discard. Runs before the prepared-store check
    // because a job can be in flight with no live partition.
    if (m_prepare_job && (m_prepare_job->scene_root == scene_root)) {
        m_prepare_job->cancel_requested.store(true, std::memory_order_relaxed);
        if (m_prepare_job->future.valid()) {
            m_prepare_job->future.cancel();
            m_prepare_job->future.wait();
        }
        finish_prepare_discard("prepare discarded: scene closed");
    }
    if ((m_scene_root == nullptr) || (m_scene_root != scene_root)) {
        return;
    }
    // The scene is going away - drop every reference without mutating it.
    m_entries.clear();
    m_piece_meshes.clear();
    m_group_node.reset();
    m_tile_tree.clear();
    m_tile_count = 0;
    m_tile_descs.clear();
    m_scene_root = nullptr;
}

} // namespace editor
