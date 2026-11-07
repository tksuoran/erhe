#include "assets/gltf_load_task.hpp"

#include "app_context.hpp"
#include "assets/load_memory_budget.hpp"
#include "assets/asset_load_tick_context.hpp"
#include "assets/asset_manager.hpp"
#include "assets/asset_paths.hpp"
#include "assets/editor_asset.hpp"
#include "assets/editor_asset_load.hpp"
#include "assets/prepared_asset.hpp"
#include "assets/scene_export.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "editor_log.hpp"
#include "parsers/gltf.hpp"
#include "parsers/gltf_extensions_import.hpp"
#include "scene/generated/scene_settings.hpp"
#include "scene/generated/scene_settings_serialization.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_save_service.hpp"

#include "erhe_scene_renderer/mesh_memory.hpp"

#include "erhe_file/file.hpp"
#include "erhe_item/item.hpp"
#include "erhe_gltf/image_transfer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_raytrace/prebuilt_bvh.hpp"
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

namespace {

// One build job of a source open (doc/editor/prepared_assets.md "The editor's
// own loading"): from its items, from items it derives and stores, or as
// run_imported_build_job() builds it.
auto run_editor_asset_job(
    Editor_asset_load&              load,
    const Imported_build_job&       job,
    erhe::graphics::Device&         graphics_device,
    const bool                      release_soups,
    const Stored_vertex_streams_use stored_streams
) -> Imported_build_result
{
    class Finish
    {
    public:
        explicit Finish(Editor_asset_load& load_in) : m_load{load_in} {}
        ~Finish() noexcept { m_load.finish_job(); }
        Finish(const Finish&) = delete;
        auto operator=(const Finish&) -> Finish& = delete;
    private:
        Editor_asset_load& m_load;
    };
    const Finish finish{load};

    const Editor_asset_job asset_job = load.prepare_job(*job.primitive);
    switch (asset_job.kind) {
        case Editor_asset_job_kind::item: {
            const Imported_build_job item_job{
                .primitive          = asset_job.item_primitive.get(),
                .build_info         = job.build_info,
                .mesh               = job.mesh,
                .base_mesh_build    = job.base_mesh_build,
                .upload_byte_count  = job.upload_byte_count,
                .payload_byte_count = job.payload_byte_count
            };
            const Imported_build_result result = run_imported_build_job(item_job, graphics_device, release_soups, stored_streams);
            load.install(*job.primitive, asset_job);
            return result;
        }
        case Editor_asset_job_kind::as_before:
        default: {
            load.count_as_before(asset_job.reason);
            return run_imported_build_job(job, graphics_device, release_soups, stored_streams);
        }
    }
}

} // anonymous namespace

Gltf_load_task::Gltf_load_task(
    App_context&                                  context,
    Asset_load_request                            request,
    std::shared_ptr<Asset_load_handle>            handle,
    std::function<void(const Asset_load_result&)> on_done
)
    : Asset_load_task{std::move(handle)}
    , m_context      {context}
    , m_request      {std::move(request)}
    , m_load_path    {m_handle->get_path()}
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
    Prepared_build_formats build_formats = is_prepared_asset_path(path) ? get_prepared_build_formats(m_context) : Prepared_build_formats{};
    erhe::task::spawn(
        tick_context.executor,
        [scan_result, path, build_formats = std::move(build_formats), checked_open = std::move(m_request.prepared_open)]() mutable {
            try {
                std::filesystem::path scan_path = path;
                if (is_prepared_asset_path(path)) {
                    // R11.1: the source when it is readable, else a prepared run; checked once per open.
                    std::shared_ptr<Prepared_open> prepared_open = std::move(checked_open);
                    if (!prepared_open) {
                        prepared_open = std::make_shared<Prepared_open>(resolve_prepared_open(path, build_formats));
                    }
                    if (prepared_open->kind == Prepared_open_kind::failed) {
                        scan_result->ok    = false;
                        scan_result->error = prepared_open->error;
                        scan_result->finished.store(true, std::memory_order_release);
                        return;
                    }
                    log_prepared_open(path, *prepared_open);
                    if (prepared_open->kind == Prepared_open_kind::open_source) {
                        scan_path              = prepared_open->source_path;
                        scan_result->load_path = scan_path;
                    } else {
                        scan_result->prepared_open = std::move(prepared_open);
                        scan_result->is_erhe_scene = true;
                        scan_result->ok            = true;
                        scan_result->finished.store(true, std::memory_order_release);
                        return;
                    }
                }
                const Gltf_scan_summary summary = editor::scan_gltf(scan_path);
                scan_result->is_erhe_scene = is_erhe_scene(summary.extensions_used);
                scan_result->ok            = summary.errors.empty();
                if (!summary.errors.empty()) {
                    scan_result->error = summary.errors.front();
                }
            } catch (...) {
                scan_result->ok    = false;
                scan_result->error = "scan threw an exception";
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
        build_state->release_soups = (get_cpu_mesh_data_retention(m_context) == Cpu_mesh_data_retention::release);
        build_state->jobs = collect_imported_build_jobs(
            m_parse_result->gltf_data,
            build_state->build_info,
            build_state->skinned_build_info,
            get_base_variant_load(m_context),
            build_state->release_soups,
            build_state->primitive_count
        );
        build_state->stored_streams = ((m_context.editor_settings == nullptr) || m_context.editor_settings->load.use_gpu_vertex_streams)
            ? Stored_vertex_streams_use::use
            : Stored_vertex_streams_use::ignore;
        // R8.1 - R8.3: a source open reads its primitives from the file's editor asset and stores what it derives.
        if (m_parse_result->editor_asset && !build_state->jobs.empty()) {
            erhe::gltf::Gltf_gpu_vertex_formats gpu_vertex_formats = make_gltf_gpu_vertex_formats(*mesh_memory);
            const std::string mesh_stamp = make_running_mesh_stamp(&gpu_vertex_formats);
            build_state->editor_asset_load = std::make_shared<Editor_asset_load>(Editor_asset_load_create_info{
                .editor_asset         = m_parse_result->editor_asset,
                .catalog              = m_parse_result->catalog,
                .gpu_vertex_formats   = gpu_vertex_formats,
                .mesh_stamp           = mesh_stamp,
                .bvh_stamp            = make_running_bvh_stamp(),
                .bvh_items            = !erhe::raytrace::get_prebuilt_bvh_backend().empty(),
                .executor             = &tick_context.executor,
                .job_count            = build_state->jobs.size(),
                .default_lod_settings = m_parse_result->default_lod_settings
            });
        }
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
            [build_state = m_build_state, job_index, payload_lease, lease, mesh_memory, graphics_device, path = m_load_path]() {
                Imported_build_result result{};
                try {
                    result = build_state->editor_asset_load
                        ? run_editor_asset_job(*build_state->editor_asset_load, build_state->jobs[job_index], *graphics_device, build_state->release_soups, build_state->stored_streams)
                        : run_imported_build_job(build_state->jobs[job_index], *graphics_device, build_state->release_soups, build_state->stored_streams);
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
                if (result.stored) {
                    ++build_state->stored_count;
                } else if (!result.portable_reason.empty()) {
                    ++build_state->portable_reasons[result.portable_reason];
                }
            }
        );
    }
}

void Gltf_load_task::start_parse(Asset_load_tick_context& tick_context)
{
    const std::filesystem::path path = m_load_path;

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
        .device_options  = make_gltf_device_options(m_context, tick_context.graphics_device),
        .root_node       = parse_result->container_node,
        .mesh_layer_id   = m_mesh_layer_id,
        .path            = path,
        .parallel        = (m_context.editor_settings == nullptr) || m_context.editor_settings->load.parallel_gltf_parse,
        .fix_spot_lights = m_context.fix_gltf_spot_lights,
        // A prepared run reads its Scene item, a document of derived data; a file holds authoring data alone.
        .document               = m_prepared_open ? erhe::gltf::Gltf_document::item : erhe::gltf::Gltf_document::file,
        // R3.5; a prepared run is never exported (R11.2), so it computes none.
        .content_hashes         = m_prepared_open ? erhe::gltf::Gltf_content_hash_load::none : erhe::gltf::Gltf_content_hash_load::compute,
        .triangle_soup_reload   = get_triangle_soup_reload(m_context),
        // The build jobs read the mesh payloads, in parallel under the load
        // memory budget - only where workers may build (worker contexts).
        .mesh_payload           = tick_context.graphics_device.supports_worker_contexts()
            ? erhe::gltf::Mesh_payload_load::deferred
            : erhe::gltf::Mesh_payload_load::in_parse
    };
    // A prepared run reads its Scene item and the items it names from the
    // prepared asset by range, as a file's (on the workers too).
    const std::shared_ptr<const Prepared_open> prepared_open = m_prepared_open;
    if (prepared_open) {
        parse_arguments.glb_reader      = make_scene_item_reader(*prepared_open);
        parse_arguments.glb_size        = prepared_open->scene_item_size;
        parse_arguments.buffer_resolver = make_prepared_buffer_resolver(prepared_open->prepared_asset);
        parse_arguments.stored_bvh_use  = get_stored_bvh_use(prepared_open->format_check);
    }
    m_mesh_payload_deferred = (parse_arguments.mesh_payload == erhe::gltf::Mesh_payload_load::deferred);

    // R8.1: a scene open of a source asset uses its editor asset. Imports and
    // prefab templates do not (their items belong to the files Export assigns
    // them to), nor a prepared run or a load without build jobs.
    Editor_asset_registry* const editor_asset_registry =
        (!prepared_open && ((m_mode == Mode::open_erhe_scene) || (m_mode == Mode::open_foreign_gltf)) && (parse_arguments.mesh_payload == erhe::gltf::Mesh_payload_load::deferred))
            ? m_context.editor_asset_registry
            : nullptr;

    // An erhe scene without export settings opens with a copy of the editor's defaults (finish_open_scene_gltf()).
    std::optional<Scene_export_settings> editor_export_settings;
    if ((editor_asset_registry != nullptr) && (m_context.editor_settings != nullptr)) {
        editor_export_settings = m_context.editor_settings->export_settings;
    }

    m_parse_result = parse_result;
    erhe::task::spawn(
        tick_context.executor,
        [parse_result, parse_arguments, path, prepared_open, editor_asset_registry, editor_export_settings]() mutable {
            const std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
            try {
                parse_result->gltf_data = erhe::gltf::parse_gltf(parse_arguments);
                parse_result->ok        = parse_result->gltf_data.errors.empty();
                if (!parse_result->ok) {
                    parse_result->error = parse_result->gltf_data.errors.front();
                }
            } catch (...) {
                parse_result->ok    = false;
                parse_result->error = "parse threw an exception";
            }
            log_parsers->info(
                "async parse_gltf '{}': {} ms",
                erhe::file::to_string(path.filename()),
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
            );
            if (parse_result->ok && (editor_asset_registry != nullptr)) {
                const std::chrono::steady_clock::time_point asset_start_time = std::chrono::steady_clock::now();
                // R2.2: the editor asset the file records, else the default location (R2.4), else the fallback (R2.5).
                const std::optional<Gltf_scene_state> scene_state = parse_gltf_scene_state(parse_result->gltf_data);
                const std::filesystem::path record_path = (scene_state.has_value() && !scene_state->editor_asset.empty())
                    ? normalize_asset_path(path.parent_path() / scene_state->editor_asset)
                    : std::filesystem::path{};
                std::error_code error_code;
                const std::filesystem::path canonical_path = std::filesystem::weakly_canonical(path, error_code);
                parse_result->editor_asset = editor_asset_registry->get(error_code ? path : canonical_path, record_path);
                // The LOD request Export asks for a primitive without its own (the scene's export settings).
                if (scene_state.has_value()) {
                    Scene_settings settings{};
                    if (!scene_state->settings_json.empty()) {
                        simdjson::ondemand::parser   settings_parser;
                        simdjson::padded_string      settings_padded{scene_state->settings_json};
                        simdjson::ondemand::document settings_document;
                        simdjson::ondemand::object   settings_object;
                        if (
                            (settings_parser.iterate(settings_padded).get(settings_document) != simdjson::SUCCESS) ||
                            (settings_document.get_object().get(settings_object) != simdjson::SUCCESS) ||
                            (deserialize(settings_object, settings) != simdjson::SUCCESS)
                        ) {
                            settings = Scene_settings{};
                        }
                    }
                    const std::optional<Scene_export_settings>& export_settings = settings.export_settings.has_value() ? settings.export_settings : editor_export_settings;
                    if (export_settings.has_value()) {
                        parse_result->default_lod_settings = get_export_lod_settings(export_settings.value());
                    }
                }
                if (parse_result->editor_asset) {
                    parse_result->catalog = parse_result->editor_asset->get_item_catalog();
                    log_parsers->info(
                        "Open '{}': editor asset '{}' ({} items), {} ms",
                        erhe::file::to_string(path.filename()),
                        erhe::file::to_string(parse_result->editor_asset->get_asset_path()),
                        parse_result->catalog ? parse_result->catalog->items.size() : std::size_t{0},
                        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - asset_start_time).count()
                    );
                }
            }
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

void Gltf_load_task::update_status()
{
    // Load-time only (allocates the status text): the counts of the build
    // jobs and the texture residency, while either runs.
    std::size_t mesh_done  = 0;
    std::size_t mesh_total = 0;
    if (m_build_state) {
        const std::lock_guard<std::mutex> lock{m_build_state->mutex};
        mesh_done  = m_build_state->finished_count;
        mesh_total = m_build_state->jobs.size();
    }
    const erhe::gltf::Gltf_image_residency& residency = m_parse_result->gltf_data.image_residency;
    const std::size_t texture_total = residency.decoded_images.size();
    const std::size_t texture_done  = texture_total - std::min(texture_total, residency.get_pending_image_count());
    if ((mesh_total > 0) && (mesh_done < mesh_total)) {
        m_handle->set_status(fmt::format("Building meshes {} / {}, textures {} / {}", mesh_done, mesh_total, texture_done, texture_total));
    } else {
        m_handle->set_status(fmt::format("Uploading textures {} / {}", texture_done, texture_total));
    }
}

void Gltf_load_task::finish_editor_asset_load()
{
    const std::shared_ptr<Editor_asset_load>& load = m_build_state->editor_asset_load;
    if (!load) {
        return;
    }
    load->share_replaced_shapes(m_parse_result->gltf_data);
    const Editor_asset_load_counts counts = load->get_counts();
    log_parsers->info(
        "{}: {} primitives from items, {} derived and stored, {} BVH from items, {} built and stored, {} ms",
        erhe::file::to_string(m_load_path.filename()),
        counts.from_items,
        counts.derived_stored,
        counts.bvh_from_items,
        counts.bvh_built_stored,
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_build_state->start_time).count()
    );
    std::string reasons;
    for (const auto& [reason, count] : counts.reasons) {
        reasons += fmt::format("{}{}: {}", reasons.empty() ? "" : ", ", reason, count);
    }
    log_parsers->info(
        "{}: editor asset '{}': {} items stored ({} bytes, {} transactions); {} built without items{}{}{}",
        erhe::file::to_string(m_load_path.filename()),
        erhe::file::to_string(load->get_editor_asset()->get_asset_path()),
        counts.items_written, counts.bytes_written, counts.transactions,
        counts.built_as_before,
        reasons.empty() ? "" : " (", reasons, reasons.empty() ? "" : ")"
    );
    if ((counts.items_written > 0) && (m_context.scene_save_service != nullptr)) {
        m_context.scene_save_service->notify_stores_changed();
    }
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
        erhe::file::to_string(m_load_path.filename()),
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
        "async load '{}' images: {} Basis KTX2 transcoded, {} raw block KTX2 uploaded as stored, "
        "{} raw ASTC KTX2 decoded to RGBA8 (device lacks ASTC), {} decoded (PNG / JPEG / DDS), {} failed",
        erhe::file::to_string(m_load_path.filename()),
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
            erhe::file::to_string(m_load_path)
        );
        m_handle->request_cancel();
    }
    if (m_handle->is_cancel_requested() && (m_phase != Phase::settled)) {
        // Cooperative: an in-flight parse is left to finish into its own
        // detached result, which dies with the task once is_worker_idle().
        m_phase = Phase::settled;
        m_handle->set_state(Asset_load_state::cancelled);
        log_parsers->info("async load cancelled: '{}'", erhe::file::to_string(m_load_path));
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
                m_handle->set_status("Parsing glTF");
                return Asset_load_state::running;
            }
            start_scan(tick_context);
            m_phase = Phase::scanning;
            m_handle->set_progress(0.01f);
            m_handle->set_status("Reading file");
            return Asset_load_state::running;
        }

        case Phase::scanning: {
            if (!m_scan_result->finished.load(std::memory_order_acquire)) {
                return Asset_load_state::running;
            }
            if (!m_scan_result->ok) {
                m_phase = Phase::settled;
                m_handle->set_failed(m_scan_result->error.empty() ? std::string{"scan failed"} : m_scan_result->error);
                log_parsers->error("Open '{}' failed: {}", erhe::file::to_string(m_handle->get_path()), m_handle->get_error());
                notify_done();
                return Asset_load_state::failed;
            }
            if (!m_scan_result->load_path.empty()) {
                m_load_path = m_scan_result->load_path;
            }
            m_prepared_open = std::move(m_scan_result->prepared_open);
            // A prepared run is always opened as the scene it holds.
            const bool open_as_erhe_scene =
                m_scan_result->is_erhe_scene &&
                ((m_request.erhe_scene_open == Erhe_scene_open::full_scene) || m_prepared_open);
            m_mode = open_as_erhe_scene ? Mode::open_erhe_scene : Mode::open_foreign_gltf;
            if (m_mode == Mode::open_foreign_gltf) {
                log_parsers->info(
                    "async load '{}': {} - loading for import",
                    erhe::file::to_string(m_load_path),
                    m_scan_result->is_erhe_scene
                        ? "erhe-authored scene opened as foreign glTF"
                        : "not an erhe-authored scene"
                );
                m_foreign_gltf = true;
            }

            // Record adoption (plan 2.8): nothing to parse, and the adopt
            // path needs the record intact.
            const bool adoptable =
                !m_prepared_open &&
                (m_context.asset_manager != nullptr) &&
                static_cast<bool>(m_context.asset_manager->find_adoptable_container(m_load_path));
            if (adoptable) {
                log_parsers->info(
                    "async load '{}': adopting a loaded container record - completing inline",
                    erhe::file::to_string(m_load_path)
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
                m_scene_root = open_scene_gltf(m_context, m_load_path);
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
            m_handle->set_status("Parsing glTF");
            return Asset_load_state::running;
        }

        case Phase::parsing: {
            if (!m_parse_result->finished.load(std::memory_order_acquire)) {
                return Asset_load_state::running; // still on a worker; costs this frame nothing
            }
            if (!m_parse_result->ok) {
                m_phase = Phase::settled;
                m_handle->set_failed(m_parse_result->error.empty() ? std::string{"parse failed"} : m_parse_result->error);
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
            update_status();
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
            {
                // One line per file that stores GPU vertex streams
                // (ERHE_gpu_vertex_streams): how many were uploaded as
                // stored, how many took the soup path and why.
                std::size_t portable_count = 0;
                std::size_t no_streams     = 0;
                std::string reasons;
                for (const auto& [reason, count] : m_build_state->portable_reasons) {
                    portable_count += count;
                    if ((reason == "no stored streams") || (reason == "payload not deferred")) {
                        no_streams += count;
                    }
                    reasons += fmt::format("{}{}: {}", reasons.empty() ? "" : ", ", reason, count);
                }
                if ((m_build_state->stored_count > 0) || (portable_count > no_streams)) {
                    log_parsers->info(
                        "gpu vertex streams: {} stored, {} portable ({}) in '{}'",
                        m_build_state->stored_count, portable_count, reasons, erhe::file::to_string(m_load_path)
                    );
                }
            }
            finish_editor_asset_load();
            if (m_mesh_payload_deferred) {
                // The jobs validated the prepared mesh data: the per-file
                // summary. Every payload is read: the shared read handles
                // close (a save may now replace the file).
                erhe::gltf::finish_prepared_mesh_statistics(m_parse_result->gltf_data, m_load_path);
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
            const bool residency_done = advance_residency(tick_context);
            update_status();
            if (!residency_done) {
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
            m_handle->set_status("Publishing the scene");
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
                m_load_path,
                m_parse_result->gltf_data,
                m_parse_result->container_node,
                false,
                m_prepared_open ? m_prepared_open->prepared_asset : std::shared_ptr<Prepared_asset>{}
            );
            m_prepared_open.reset(); // the scene holds the prepared asset
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
