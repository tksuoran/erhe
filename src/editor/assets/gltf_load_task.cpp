#include "assets/gltf_load_task.hpp"

#include "app_context.hpp"
#include "assets/asset_load_tick_context.hpp"
#include "assets/asset_manager.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "editor_log.hpp"
#include "parsers/gltf.hpp"
#include "scene/scene_root.hpp"

#include "erhe_scene_renderer/mesh_memory.hpp"

#include "erhe_file/file.hpp"
#include "erhe_item/item.hpp"
#include "erhe_gltf/image_transfer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_task/task.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_utility/memory_budget.hpp"
#include "erhe_verify/verify.hpp"

#include <taskflow/taskflow.hpp>

#include <chrono>
#include <exception>
#include <filesystem>

namespace editor {

Gltf_load_task::Gltf_load_task(
    App_context&                                  context,
    Asset_load_request                            request,
    std::shared_ptr<Asset_load_handle>            handle,
    std::function<void(const Asset_load_result&)> on_done
)
    : Asset_load_task{std::move(handle)}
    , m_context      {context}
    , m_request      {std::move(request)}
    , m_on_done      {std::move(on_done)}
{
    const std::shared_ptr<Scene_root> import_target = m_request.import_target.lock();
    if (import_target) {
        m_mode          = Mode::import_into_scene;
        m_mesh_layer_id = import_target->layers().content()->id;
    } else if (m_request.prefab_template) {
        m_mode          = Mode::load_prefab_template;
        // Prefab instances are retargeted to the destination scene's content
        // layer when they are instantiated.
        m_mesh_layer_id = 0;
    } else {
        m_mesh_layer_id = Mesh_layer_id::content;
    }
}

// Runs on the main thread from the tick that settled the task. Called
// exactly once, with the opened scene or null.
void Gltf_load_task::notify_done()
{
    if (!m_on_done) {
        return;
    }
    std::function<void(const Asset_load_result&)> on_done;
    on_done.swap(m_on_done);
    on_done(
        Asset_load_result{
            .state          = m_handle->get_state(),
            .scene_root     = m_scene_root,
            .foreign_gltf   = m_foreign_gltf,
            .prepared_parse = m_prepared_parse
        }
    );
}

Gltf_load_task::~Gltf_load_task() noexcept = default;

auto Gltf_load_task::is_worker_idle() const -> bool
{
    // The parse and the Buffer_mesh build are the workers this task spawns
    // directly. (The raytrace / edge-line finalize tasks the publish step
    // kicks off are owned by the existing Async_raytrace_kickoff_operation
    // machinery, not by this task.)
    const bool scan_idle  = !m_scan_result  || m_scan_result ->finished.load(std::memory_order_acquire);
    const bool parse_idle = !m_parse_result || m_parse_result->finished.load(std::memory_order_acquire);
    const bool build_idle = !m_build_state || m_build_state->is_worker_idle();
    // The image decode tasks residency starts (they write only into the
    // residency's shared decode state, but this task owns them).
    const bool decode_idle = !parse_idle || !m_parse_result || m_parse_result->gltf_data.image_residency.is_decode_idle();
    return scan_idle && parse_idle && build_idle && decode_idle;
}

void Gltf_load_task::start_scan(Asset_load_tick_context& tick_context)
{
    auto scan_result = std::make_shared<Scan_result>();
    const std::filesystem::path path = m_handle->get_path();
    m_scan_result = scan_result;
    erhe::task::spawn(
        tick_context.executor,
        [scan_result, path]() {
            try {
                const Gltf_scan_summary summary = editor::scan_gltf(path);
                scan_result->is_erhe_scene = is_erhe_scene(summary.extensions_used);
                scan_result->ok            = true;
            } catch (...) {
                scan_result->ok = false;
            }
            scan_result->finished.store(true, std::memory_order_release);
        }
    );
}

auto Gltf_load_task::Build_state::is_finished() -> bool
{
    const std::lock_guard<std::mutex> lock{mutex};
    return (next_job == jobs.size()) && (finished_count == jobs.size());
}

auto Gltf_load_task::Build_state::is_worker_idle() -> bool
{
    const std::lock_guard<std::mutex> lock{mutex};
    return running_count == 0;
}

void Gltf_load_task::start_build(Asset_load_tick_context& tick_context)
{
    erhe::scene_renderer::Mesh_memory* const mesh_memory = m_context.mesh_memory;
    ERHE_VERIFY(mesh_memory != nullptr);

    // Both Build_infos are made HERE, on the main thread:
    // make_primitive_buffer_info can create a Vertex_input_state, which is a
    // GPU object. The worker only consumes them.
    //
    // Mesh_memory_queue::loader is what makes the vertex / index uploads
    // budget-drained instead of "all of it, this frame". It is only legal
    // because publish below gates on the watermark (plan 2.6).
    const erhe::primitive::Build_info build_info = make_import_build_info(m_context, erhe::scene_renderer::Mesh_memory_queue::loader);
    const erhe::primitive::Build_info skinned_build_info{
        .primitive_types = build_info.primitive_types,
        .buffer_info     = mesh_memory->make_skinned_primitive_buffer_info(erhe::scene_renderer::Mesh_memory_queue::loader),
        .constant_color  = build_info.constant_color,
        .keep_geometry   = build_info.keep_geometry,
        .normal_style    = build_info.normal_style,
        .vertex_id_vec3  = build_info.vertex_id_vec3,
        .autocolor       = build_info.autocolor
    };

    std::shared_ptr<Build_state> build_state = std::make_shared<Build_state>(build_info, skinned_build_info);
    build_state->start_time = std::chrono::steady_clock::now();
    // Without worker contexts (GL, headless / null window) the worker may
    // not allocate GPU buffers: no async build at all, the main-thread
    // finalize pass (finalize_imported_meshes) builds the buffer meshes - the
    // pre-async import behavior.
    if (tick_context.graphics_device.supports_worker_contexts()) {
        // gltf_data.nodes keeps the meshes (and through them the primitives)
        // alive until the task is reaped, which waits for the build tasks.
        build_state->jobs = collect_imported_build_jobs(
            m_parse_result->gltf_data,
            build_state->build_info,
            build_state->skinned_build_info,
            build_state->primitive_count
        );
        build_state->release_soups = (get_cpu_mesh_data_retention(m_context) == Cpu_mesh_data_retention::release);
    }
    m_build_state = build_state;
}

void Gltf_load_task::dispatch_build_jobs(Asset_load_tick_context& tick_context)
{
    Build_state& state = *m_build_state;
    erhe::scene_renderer::Mesh_memory* const mesh_memory     = m_context.mesh_memory;
    erhe::graphics::Device* const            graphics_device = &tick_context.graphics_device;
    while (state.next_job < state.jobs.size()) {
        const std::size_t         job_index = state.next_job;
        const Imported_build_job& job       = state.jobs[job_index];
        // Two leases: the soup the job reads (a deferred payload) for the
        // job's duration, and the upload bytes, which wait in the loader
        // transfer queue until the budgeted drain stages them - that lease
        // goes to the queue when the build is done.
        std::shared_ptr<erhe::utility::Memory_budget_lease> payload_lease = std::make_shared<erhe::utility::Memory_budget_lease>();
        std::shared_ptr<erhe::utility::Memory_budget_lease> lease         = std::make_shared<erhe::utility::Memory_budget_lease>();
        if (tick_context.load_memory_budget) {
            if (job.payload_byte_count > 0) {
                *payload_lease = tick_context.load_memory_budget->try_acquire(job.payload_byte_count, erhe::utility::Memory_budget_kind::mesh);
                if (payload_lease->is_empty()) {
                    return; // resumes when finished jobs and drained uploads give bytes back
                }
            }
            *lease = tick_context.load_memory_budget->try_acquire(job.upload_byte_count, erhe::utility::Memory_budget_kind::mesh);
            if (lease->is_empty()) {
                return; // the payload lease goes with it
            }
        }
        {
            const std::lock_guard<std::mutex> lock{state.mutex};
            ++state.next_job;
            ++state.running_count;
        }
        erhe::task::spawn(
            tick_context.executor,
            [build_state = m_build_state, job_index, payload_lease, lease, mesh_memory, graphics_device, path = m_handle->get_path()]() {
                Imported_build_result result{};
                try {
                    result = run_imported_build_job(build_state->jobs[job_index], *graphics_device, build_state->release_soups);
                } catch (const std::exception& e) {
                    // A primitive without buffer meshes is built by the
                    // main-thread finalize pass instead.
                    log_parsers->error("async glTF load: buffer mesh build failed for '{}': {}", erhe::file::to_string(path), e.what());
                } catch (...) {
                    log_parsers->error("async glTF load: buffer mesh build failed for '{}'", erhe::file::to_string(path));
                }
                payload_lease->reset(); // the soup is released or resident by now
                mesh_memory->get_loader_transfer_queue().hold_until_drained(std::move(*lease));
                const std::lock_guard<std::mutex> lock{build_state->mutex};
                --build_state->running_count;
                ++build_state->finished_count;
                if (result.prepared) {
                    ++build_state->prepared_count;
                }
                if (result.released) {
                    ++build_state->released_count;
                }
            }
        );
    }
}

void Gltf_load_task::start_parse(Asset_load_tick_context& tick_context)
{
    const std::filesystem::path path = m_handle->get_path();

    // Unhosted container root: the parse must not touch a live Scene from a
    // worker. This is the same shape the Asset_manager container loader and
    // Prefab_library template loader already use.
    auto parse_result = std::make_shared<Parse_result>();
    if (m_mode == Mode::load_prefab_template) {
        // Prefab template root: content + show_in_ui, but NOT import_root -
        // it is the template itself, not an import wrapper. Unhosted;
        // Prefab_library::finish_load_template hosts it.
        parse_result->container_node = std::make_shared<erhe::scene::Xform>(
            m_request.root_node_name.empty() ? erhe::file::to_string(path.filename()) : m_request.root_node_name
        );
        parse_result->container_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    } else if (m_mode != Mode::open_erhe_scene) {
        // The import path hangs the file's content from an import_root
        // wrapper named after the file, unparented until the operation
        // attaches it. Mirror exactly what the inline parse in
        // make_import_gltf_operation produces.
        parse_result->container_node = std::make_shared<erhe::scene::Xform>(erhe::file::to_string(path.filename()));
        parse_result->container_node->enable_flag_bits(
            erhe::Item_flags::content | erhe::Item_flags::show_in_ui | erhe::Item_flags::import_root
        );
    } else {
        parse_result->container_node = std::make_shared<erhe::scene::Xform>("open scene container");
    }

    // Everything device-derived is resolved HERE, on the main thread, and
    // handed to the worker by value.
    erhe::gltf::Gltf_parse_arguments parse_arguments{
        .executor        = tick_context.executor,
        .device_options  = erhe::gltf::query_gltf_device_options(tick_context.graphics_device),
        .root_node       = parse_result->container_node,
        .mesh_layer_id   = m_mesh_layer_id,
        .path            = path,
        .parallel        = (m_context.editor_settings == nullptr) || m_context.editor_settings->load.parallel_gltf_parse,
        .fix_spot_lights = m_context.fix_gltf_spot_lights,
        .prepared_mesh_data_use = get_prepared_mesh_data_use(m_context),
        .triangle_soup_reload   = get_triangle_soup_reload(m_context),
        // The build jobs read the mesh payloads, in parallel under the load
        // memory budget - only where workers may build (worker contexts).
        .mesh_payload           = tick_context.graphics_device.supports_worker_contexts()
            ? erhe::gltf::Mesh_payload_load::deferred
            : erhe::gltf::Mesh_payload_load::in_parse
    };
    m_mesh_payload_deferred = (parse_arguments.mesh_payload == erhe::gltf::Mesh_payload_load::deferred);

    m_parse_result = parse_result;
    erhe::task::spawn(
        tick_context.executor,
        [parse_result, parse_arguments, path]() mutable {
            const std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
            try {
                parse_result->gltf_data = erhe::gltf::parse_gltf(parse_arguments);
                parse_result->ok        = true;
            } catch (...) {
                parse_result->ok = false;
            }
            log_parsers->info(
                "async parse_gltf '{}': {} ms",
                erhe::file::to_string(path.filename()),
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
            );
            // Release-store last: everything above must be visible to the
            // main thread before it observes `finished`.
            parse_result->finished.store(true, std::memory_order_release);
        }
    );
}

auto Gltf_load_task::advance_residency(Asset_load_tick_context& tick_context) -> bool
{
    erhe::gltf::Gltf_data& gltf_data = m_parse_result->gltf_data;

    if (!m_image_transfer) {
        // The decoded pixels share the load memory budget with the mesh
        // builds (set before the first decode).
        gltf_data.image_residency.set_memory_budget(tick_context.load_memory_budget);
        m_image_transfer = std::make_unique<erhe::gltf::Image_transfer>(
            tick_context.graphics_device,
            erhe::gltf::Image_transfer_mode::frame_recording,
            tick_context.max_upload_staging_bytes
        );
    }

    // Keep the decode workers fed: images decode / transcode ahead of the
    // upload, as far as the decoded-bytes budget allows (decoded pixels
    // waiting here plus those being decoded); each upload below frees its
    // image's bytes for the next decode.
    gltf_data.image_residency.schedule_decodes();

    const std::size_t total = gltf_data.image_residency.decoded_images.size();
    while (!tick_context.budget.is_exhausted() && tick_context.budget.take_residency_item()) {
        std::size_t granted = tick_context.budget.take_gpu_upload_bytes(
            tick_context.budget.get_remaining_gpu_upload_bytes()
        );
        if (granted == 0) {
            return false; // out of upload budget this frame
        }
        const bool did_work = gltf_data.image_residency.process_next_image_into_frame(
            gltf_data,
            tick_context.graphics_device,
            *m_image_transfer,
            tick_context.command_buffer,
            granted
        );
        tick_context.budget.give_back_gpu_upload_bytes(granted);
        if (!did_work) {
            if (gltf_data.image_residency.get_pending_image_count() == 0) {
                return true;
            }
            return false; // budget or staging ran out mid-image, or no decode finished yet
        }
        if (total > 0) {
            const std::size_t done = total - gltf_data.image_residency.get_pending_image_count();
            m_handle->set_progress(0.5f + 0.4f * static_cast<float>(done) / static_cast<float>(total));
        }
    }
    return gltf_data.image_residency.get_pending_image_count() == 0;
}

void Gltf_load_task::log_staging_statistics(Asset_load_tick_context& tick_context) const
{
    const erhe::gltf::Gltf_image_residency& residency = m_parse_result->gltf_data.image_residency;
    const erhe::graphics::Upload_staging_usage staging = tick_context.graphics_device.get_upload_staging_usage();
    erhe::utility::Memory_budget::Statistics budget{};
    if (tick_context.load_memory_budget) {
        budget = tick_context.load_memory_budget->get_statistics();
    }
    constexpr std::size_t mesh  = static_cast<std::size_t>(erhe::utility::Memory_budget_kind::mesh);
    constexpr std::size_t image = static_cast<std::size_t>(erhe::utility::Memory_budget_kind::image);
    log_parsers->info(
        "async load '{}' resident: load memory budget high-water {} of {} bytes (meshes {}, images {}; "
        "{} mesh / {} image leases, {} oversize, {} refused; since the load phase began); "
        "decoded pixels high-water {} bytes; "
        "upload staging high-water {} of {} bytes cap (device lifetime), {} waits, {} oversize; "
        "upload budget {} bytes per frame",
        erhe::file::to_string(m_handle->get_path().filename()),
        budget.high_water_byte_count,
        budget.capacity_byte_count,
        budget.high_water_by_kind[mesh],
        budget.high_water_by_kind[image],
        budget.lease_count_by_kind[mesh],
        budget.lease_count_by_kind[image],
        budget.oversize_grant_count,
        budget.refused_count,
        residency.get_pending_byte_high_water(),
        staging.high_water_bytes,
        staging.capacity_byte_count,
        staging.refused_count,
        staging.oversize_count,
        (m_context.asset_manager != nullptr) ? m_context.asset_manager->get_gpu_upload_bytes_per_frame() : std::size_t{0}
    );
    const erhe::gltf::Gltf_image_decode_path_counts& paths = residency.get_decode_path_counts();
    log_parsers->info(
        "async load '{}' images: {} Basis KTX2 transcoded, {} raw ASTC KTX2 uploaded as stored, "
        "{} raw ASTC KTX2 decoded to RGBA8 (device lacks ASTC), {} decoded (PNG / JPEG / DDS), {} failed",
        erhe::file::to_string(m_handle->get_path().filename()),
        paths.basis_transcoded,
        paths.raw_blocks,
        paths.raw_blocks_decoded,
        paths.decoded,
        paths.failed
    );
}

auto Gltf_load_task::tick(Asset_load_tick_context& tick_context) -> Asset_load_state
{
    // An import whose destination scene closed has nothing left to publish
    // into. Treat it exactly like a cancel (plan 2.11).
    if (
        (m_mode == Mode::import_into_scene) &&
        (m_phase != Phase::settled) &&
        m_request.import_target.expired()
    ) {
        log_parsers->info(
            "async import '{}': destination scene closed - cancelling",
            erhe::file::to_string(m_handle->get_path())
        );
        m_handle->request_cancel();
    }
    if (m_handle->is_cancel_requested() && (m_phase != Phase::settled)) {
        // Cooperative: an in-flight parse is left to finish into its own
        // detached result, which dies with the task once is_worker_idle().
        m_phase = Phase::settled;
        m_handle->set_state(Asset_load_state::cancelled);
        log_parsers->info("async load cancelled: '{}'", erhe::file::to_string(m_handle->get_path()));
        notify_done();
        return Asset_load_state::cancelled;
    }

    switch (m_phase) {
        case Phase::start: {
            m_handle->set_state(Asset_load_state::running);
            if (m_mode != Mode::open_erhe_scene && m_mode != Mode::open_foreign_gltf) {
                // No scan: neither the import machinery nor a prefab template
                // cares whether the file is an erhe-authored scene.
                start_parse(tick_context);
                m_phase = Phase::parsing;
                m_handle->set_progress(0.05f);
                return Asset_load_state::running;
            }
            start_scan(tick_context);
            m_phase = Phase::scanning;
            m_handle->set_progress(0.01f);
            return Asset_load_state::running;
        }

        case Phase::scanning: {
            if (!m_scan_result->finished.load(std::memory_order_acquire)) {
                return Asset_load_state::running;
            }
            if (!m_scan_result->ok) {
                m_phase = Phase::settled;
                m_handle->set_failed("scan failed");
                notify_done();
                return Asset_load_state::failed;
            }
            const bool open_as_erhe_scene =
                m_scan_result->is_erhe_scene &&
                (m_request.erhe_scene_open == Erhe_scene_open::full_scene);
            m_mode = open_as_erhe_scene ? Mode::open_erhe_scene : Mode::open_foreign_gltf;
            if (m_mode == Mode::open_foreign_gltf) {
                log_parsers->info(
                    "async load '{}': {} - loading for import",
                    erhe::file::to_string(m_handle->get_path()),
                    m_scan_result->is_erhe_scene
                        ? "erhe-authored scene opened as foreign glTF"
                        : "not an erhe-authored scene"
                );
                m_foreign_gltf = true;
            }

            // Record adoption (plan 2.8): nothing to parse, and the adopt
            // path needs the record intact.
            const bool adoptable =
                (m_context.asset_manager != nullptr) &&
                static_cast<bool>(m_context.asset_manager->find_adoptable_container(m_handle->get_path()));
            if (adoptable) {
                log_parsers->info(
                    "async load '{}': adopting a loaded container record - completing inline",
                    erhe::file::to_string(m_handle->get_path())
                );
                m_phase = Phase::settled;
                if (m_mode == Mode::open_foreign_gltf) {
                    // Hand back with NO prepared parse: the import operation
                    // takes the record's parse itself, which is the cheaper
                    // and the already-correct path.
                    m_handle->set_progress(1.0f);
                    m_handle->set_state(Asset_load_state::done);
                    notify_done();
                    return Asset_load_state::done;
                }
                // Re-validated by open_scene_gltf itself, which looks the
                // record up again and falls back to a fresh parse.
                m_scene_root = open_scene_gltf(m_context, m_handle->get_path());
                if (m_scene_root) {
                    m_handle->set_progress(1.0f);
                    m_handle->set_state(Asset_load_state::done);
                } else {
                    m_handle->set_failed("open failed");
                }
                notify_done();
                return m_handle->get_state();
            }
            start_parse(tick_context);
            m_phase = Phase::parsing;
            m_handle->set_progress(0.05f);
            return Asset_load_state::running;
        }

        case Phase::parsing: {
            if (!m_parse_result->finished.load(std::memory_order_acquire)) {
                return Asset_load_state::running; // still on a worker; costs this frame nothing
            }
            if (!m_parse_result->ok) {
                m_phase = Phase::settled;
                m_handle->set_failed("parse failed");
                notify_done();
                return Asset_load_state::failed;
            }
            m_handle->set_progress(0.35f);
            start_build(*&tick_context);
            m_phase = Phase::building;
            return Asset_load_state::running;
        }

        case Phase::building: {
            // Mesh builds first: publish waits for them, textures may lag.
            dispatch_build_jobs(tick_context);
            if (!m_build_state->is_finished()) {
                // The images need nothing from the build: decode and upload
                // them meanwhile, so the decode overlaps the build instead
                // of following it. Publish still waits for both.
                static_cast<void>(advance_residency(tick_context));
                return Asset_load_state::running;
            }
            log_parsers->info(
                "build_imported_buffer_meshes: {} primitives, {} ms ({} unique shapes built in parallel, {} of them prepared, {} soups released){}",
                m_build_state->primitive_count,
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_build_state->start_time).count(),
                m_build_state->jobs.size(),
                m_build_state->prepared_count,
                m_build_state->released_count,
                m_mesh_payload_deferred ? " (mesh payloads read by the jobs)" : ""
            );
            if (m_mesh_payload_deferred) {
                // The jobs validated the prepared mesh data: the per-file
                // summary. Every payload is read: the shared read handles
                // close (a save may now replace the file).
                erhe::gltf::finish_prepared_mesh_statistics(m_parse_result->gltf_data, m_handle->get_path());
                erhe::gltf::close_source_files(m_parse_result->gltf_data);
            }
            // Everything the build enqueued has a ticket at or below this.
            if (m_context.mesh_memory != nullptr) {
                m_build_ticket = m_context.mesh_memory->get_loader_transfer_queue().get_last_ticket();
            }
            m_handle->set_progress(0.5f);
            m_phase = Phase::residency;
            [[fallthrough]];
        }

        case Phase::residency: {
            if (!advance_residency(tick_context)) {
                return Asset_load_state::running;
            }
            // Bound here, not while the build worker may still run.
            m_parse_result->gltf_data.image_residency.bind_material_textures(m_parse_result->gltf_data);
            log_staging_statistics(tick_context);
            m_handle->set_progress(0.9f);
            m_handle->set_state(Asset_load_state::resident);
            m_phase = Phase::publish;
            return Asset_load_state::resident;
        }

        case Phase::publish: {
            // Publish gate (plan 2.5): the loader transfer queue is
            // budget-drained, so "enqueued" does NOT imply "uploaded". Hold
            // publish until the watermark has passed every ticket the build
            // produced, otherwise the scene could appear drawing from vertex
            // and index bytes still sitting in the queue.
            if (m_context.mesh_memory != nullptr) {
                const std::uint64_t watermark = m_context.mesh_memory->get_loader_transfer_queue().get_watermark();
                if (watermark < m_build_ticket) {
                    return Asset_load_state::resident;
                }
            }
            // Publish-once (plan 2.7): one atomic step, deliberately not
            // sliced. A tree big enough to overrun the frame overruns it -
            // the alternative is a scene that mutates under the tools
            // mid-import.
            m_phase = Phase::settled;
            if (m_mode != Mode::open_erhe_scene) {
                // Nothing to publish here: for a foreign open the scene does
                // not exist yet, and for an import the destination scene is
                // mutated by the (undoable) operation, not by this task. Hand
                // the finished parse back and let the caller queue the
                // (now cheap, still undoable) Scene_open_operation.
                m_prepared_parse = std::make_shared<Prepared_gltf_parse>();
                m_prepared_parse->gltf_data = std::move(m_parse_result->gltf_data);
                m_prepared_parse->root_node = m_parse_result->container_node;
                m_handle->set_progress(1.0f);
                m_handle->set_state(Asset_load_state::done);
                notify_done();
                return Asset_load_state::done;
            }
            m_scene_root = finish_open_scene_gltf(
                m_context,
                m_handle->get_path(),
                m_parse_result->gltf_data,
                m_parse_result->container_node,
                false
            );
            if (!m_scene_root) {
                m_handle->set_failed("no ERHE_scene payload");
                notify_done();
                return Asset_load_state::failed;
            }
            m_handle->set_progress(1.0f);
            m_handle->set_state(Asset_load_state::done);
            notify_done();
            return Asset_load_state::done;
        }

        case Phase::settled:
        default: {
            return m_handle->get_state();
        }
    }
}

} // namespace editor
