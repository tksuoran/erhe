#include "renderers/lightmap_streamer.hpp"

#include "app_context.hpp"
#include "app_settings.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "editor_log.hpp"
#include "renderers/lightmap_baker.hpp"
#include "renderers/lightmap_partitioner.hpp"
#include "renderers/lightmap_report.hpp"
#include "scene/scene_root.hpp"

#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <fmt/format.h>
#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace editor {

Lightmap_streamer::Lightmap_streamer(erhe::graphics::Device& graphics_device, App_context& context)
    : m_graphics_device{graphics_device}
    , m_context        {context}
{
}

Lightmap_streamer::~Lightmap_streamer() noexcept = default;

void Lightmap_streamer::set_budget(const int resident_tile_budget)
{
    m_budget = std::max(1, resident_tile_budget);
}

void Lightmap_streamer::invalidate()
{
    reset();
}

void Lightmap_streamer::reset()
{
    // A pending worker read keeps its shared state alive on its own; it is
    // simply dropped on completion (m_pending no longer matches).
    m_pending.reset();
    m_manifest_loaded         = false;
    m_manifest_missing_logged = false;
    m_piece_hint_logged       = false;
    m_foreign_rejected        = false;
    m_stale                   = false;
    m_manifest                = {};
    m_tiles.clear();
    m_slot_to_tile.clear();
    m_resident_count = 0;
    m_slot_grid      = 1;
    m_texture.reset();
}

auto Lightmap_streamer::get_slot_origin(const int slot) const -> glm::ivec2
{
    const int sx = (m_slot_grid > 0) ? (slot % m_slot_grid) : 0;
    const int sy = (m_slot_grid > 0) ? (slot / m_slot_grid) : 0;
    return glm::ivec2{sx * m_manifest.tile_size, sy * m_manifest.tile_size};
}

auto Lightmap_streamer::try_load_manifest(Scene_root& scene_root) -> bool
{
    m_directory = Lightmap_tile_io::directory_for_scene(scene_root.get_source_path());
    std::error_code ec;
    if (!std::filesystem::exists(m_directory / "manifest.json", ec) || ec) {
        return false;
    }
    std::string error;
    if (!Lightmap_tile_io::read_manifest(m_directory, m_manifest, &error)) {
        if (!m_manifest_missing_logged) {
            m_manifest_missing_logged = true;
            log_render->warn("Lightmap_streamer: {}", error);
            if (m_context.lightmap_report != nullptr) {
                m_context.lightmap_report->add_error(Lightmap_report::Stage::stream, "manifest.json", error);
            }
        }
        return false;
    }
    if ((m_manifest.tile_size <= 0) || m_manifest.tiles.empty()) {
        return false;
    }
    // Owning-scene check: a set stamped with a different scene_id is
    // foreign (unsaved scenes share the untitled.lightmap directory) -
    // never stream another scene's data. m_foreign_rejected caches the
    // verdict so the manifest is not re-parsed every frame; invalidate()
    // (a fresh save from THIS scene rewrites the stamp) re-checks.
    if (m_manifest.scene_id != scene_root.get_scene_id()) {
        m_foreign_rejected = true;
        m_manifest         = {};
        log_render->info(
            "Lightmap_streamer: tile set in {} belongs to a different scene - ignored",
            m_directory.string()
        );
        return false;
    }
    m_tiles.assign(m_manifest.tiles.size(), Tile_runtime{});
    // Incremental manifests (save-on-evict / Save All) list every layout
    // tile before all payloads exist; stat them once so the ranking skips
    // not-yet-baked tiles without per-frame filesystem probes or errors.
    for (std::size_t tile = 0; tile < m_manifest.tiles.size(); ++tile) {
        std::error_code payload_ec;
        m_tiles[tile].on_disk =
            std::filesystem::exists(m_directory / m_manifest.tiles[tile].payload, payload_ec) && !payload_ec;
    }
    const int desired_slots = std::clamp(m_budget, 1, static_cast<int>(m_manifest.tiles.size()));
    m_slot_grid = 1;
    while (m_slot_grid * m_slot_grid < desired_slots) {
        ++m_slot_grid;
    }
    m_slot_to_tile.assign(static_cast<std::size_t>(m_slot_grid * m_slot_grid), -1);
    m_resident_count  = 0;
    m_manifest_loaded = true;

    // Stale-bake detection: the manifest's parameter hash against the
    // current settings. Stale tiles still stream (better than nothing);
    // the window shows the notice.
    if (m_context.lightmap_baker != nullptr) {
        const uint64_t current_hash = m_context.lightmap_baker->get_bake_parameters_hash();
        m_stale = (current_hash != m_manifest.bake_hash);
        if (m_stale && (m_context.lightmap_report != nullptr)) {
            m_context.lightmap_report->add_warning(
                Lightmap_report::Stage::stream,
                "manifest.json",
                "baked tiles are stale (bake parameters changed since the bake) - rebake to refresh"
            );
        }
    }
    log_render->info(
        "Lightmap_streamer: manifest loaded from {}: {} tiles of {}^2, {} resident slots",
        m_directory.string(), m_manifest.tiles.size(), m_manifest.tile_size, m_slot_grid * m_slot_grid
    );
    return true;
}

void Lightmap_streamer::ensure_texture()
{
    using namespace erhe::graphics;
    const int atlas_size = m_slot_grid * m_manifest.tile_size;
    if (m_texture && (m_texture->get_width() == atlas_size) && (m_texture->get_height() == atlas_size)) {
        return;
    }
    m_texture = std::make_shared<Texture>(
        m_graphics_device,
        Texture_create_info{
            .device      = m_graphics_device,
            .usage_mask  =
                Image_usage_flag_bit_mask::sampled |
                Image_usage_flag_bit_mask::transfer_dst,
            .type        = Texture_type::texture_2d,
            .pixelformat = erhe::dataformat::Format::format_16_vec4_float,
            .width       = atlas_size,
            .height      = atlas_size,
            .debug_label = erhe::utility::Debug_label{"lightmap stream atlas"}
        }
    );
    // Defined content + layout before the first slot upload.
    constexpr unsigned int bake_thread_slot = 6;
    Command_buffer& command_buffer = m_graphics_device.get_command_buffer(bake_thread_slot);
    command_buffer.begin();
    command_buffer.clear_texture(*m_texture, {0.0, 0.0, 0.0, 0.0});
    command_buffer.transition_texture_layout(*m_texture, Image_layout::shader_read_only_optimal);
    command_buffer.end();
    Command_buffer* command_buffers[] = { &command_buffer };
    m_graphics_device.submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
    m_graphics_device.wait_idle();
}

void Lightmap_streamer::apply_tile_regions(Scene_root& scene_root, const int tile)
{
    if ((tile < 0) || (tile >= static_cast<int>(m_manifest.tiles.size()))) {
        return;
    }
    const Lightmap_tile_io::Tile_entry& entry = m_manifest.tiles[static_cast<std::size_t>(tile)];
    const int slot = m_tiles[static_cast<std::size_t>(tile)].slot;

    // Reload-stable identity: node index path (unique even for duplicated
    // names) with node path + mesh name as the fallback. Built fresh per
    // residency change (infrequent); one scan of the content meshes.
    std::unordered_map<std::string, std::shared_ptr<erhe::scene::Mesh>> lookup_by_name;
    std::unordered_map<std::string, std::shared_ptr<erhe::scene::Mesh>> lookup_by_index;
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root.layers().content()->meshes) {
        if (!mesh) {
            continue;
        }
        lookup_by_name.emplace(
            Lightmap_tile_io::node_path(mesh->get_node()) + '\n' + mesh->get_name(),
            mesh
        );
        lookup_by_index.emplace(
            Lightmap_tile_io::node_index_path(mesh->get_node()) + '\n' + mesh->get_name(),
            mesh
        );
    }

    const float     atlas_size  = static_cast<float>(m_slot_grid * m_manifest.tile_size);
    const float     tile_size   = static_cast<float>(m_manifest.tile_size);
    const glm::vec2 slot_origin = (slot >= 0) ? glm::vec2{get_slot_origin(slot)} : glm::vec2{0.0f};
    std::size_t unresolved_pieces = 0;
    for (const Lightmap_tile_io::Region_entry& region : entry.regions) {
        erhe::scene::Mesh* mesh            = nullptr;
        std::size_t        primitive_index = region.primitive_index;
        const bool         is_piece        = region.piece_ordinal >= 0;
        if (is_piece) {
            // World-space tile piece: resolve through the live partition
            // (the manifest stores the SOURCE mesh identity; the piece
            // meshes are re-created by Prepare World-Space Tiles).
            Lightmap_partitioner* const partitioner = m_context.lightmap_partitioner;
            if ((partitioner == nullptr) || !partitioner->is_prepared() || (partitioner->get_scene_root() != &scene_root)) {
                ++unresolved_pieces;
                continue;
            }
            const std::pair<erhe::scene::Mesh*, std::size_t> piece = partitioner->find_piece(
                region.node_path,
                region.node_index_path,
                region.mesh_name,
                region.primitive_index,
                entry.id,
                region.piece_ordinal
            );
            if (piece.first == nullptr) {
                ++unresolved_pieces;
                continue;
            }
            mesh            = piece.first;
            primitive_index = piece.second;
        } else {
            const auto index_it = lookup_by_index.find(region.node_index_path + '\n' + region.mesh_name);
            if (!region.node_index_path.empty() && (index_it != lookup_by_index.end())) {
                mesh = index_it->second.get();
            } else {
                const auto name_it = lookup_by_name.find(region.node_path + '\n' + region.mesh_name);
                if (name_it == lookup_by_name.end()) {
                    continue; // renamed / removed since the bake: stays unlit
                }
                mesh = name_it->second.get();
            }
        }
        std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_mutable_primitives();
        if (primitive_index >= primitives.size()) {
            continue;
        }
        primitives[primitive_index].lightmap_uv_scale_offset = (slot >= 0)
            ? glm::vec4{
                region.uv_scale_offset.x * tile_size / atlas_size,
                region.uv_scale_offset.y * tile_size / atlas_size,
                (region.uv_scale_offset.z * tile_size + slot_origin.x) / atlas_size,
                (region.uv_scale_offset.w * tile_size + slot_origin.y) / atlas_size
            }
            // Evicted: pieces fall back to flat white (scale.x < 0 sentinel,
            // standard.frag); ordinary regions gate the lightmap off.
            : (is_piece ? glm::vec4{-1.0f, 0.0f, 0.0f, 0.0f} : glm::vec4{0.0f});
    }
    if ((unresolved_pieces > 0) && !m_piece_hint_logged) {
        m_piece_hint_logged = true;
        const std::string message = fmt::format(
            "{} baked regions are world-space tile pieces with no live partition - run Prepare World-Space Tiles to bind them",
            unresolved_pieces
        );
        log_render->info("Lightmap_streamer: {}", message);
        if (m_context.lightmap_report != nullptr) {
            m_context.lightmap_report->add_warning(Lightmap_report::Stage::stream, "manifest.json", message);
        }
    }
}

void Lightmap_streamer::start_load(const int tile, const int slot)
{
    auto pending = std::make_shared<Pending_load>();
    pending->tile = tile;
    pending->slot = slot;
    m_pending     = pending;
    m_tiles[static_cast<std::size_t>(tile)].loading = true;
    const std::filesystem::path file_path = m_directory / m_manifest.tiles[static_cast<std::size_t>(tile)].payload;
    const auto load = [pending, file_path]() {
        std::string error;
        if (!Lightmap_tile_io::read_tile_payload(file_path, pending->width, pending->height, pending->pixels, nullptr, nullptr, &error)) {
            pending->failed = true;
            pending->error  = error;
        }
        pending->ready.store(true, std::memory_order_release);
    };
    if (m_context.executor != nullptr) {
        m_context.executor->silent_async(load);
    } else {
        load();
    }
}

void Lightmap_streamer::upload_pending(Scene_root& scene_root)
{
    using namespace erhe::graphics;
    if (!m_pending || !m_pending->ready.load(std::memory_order_acquire)) {
        return;
    }
    const std::shared_ptr<Pending_load> pending = std::move(m_pending);
    const int tile = pending->tile;
    if ((tile < 0) || (tile >= static_cast<int>(m_tiles.size()))) {
        return;
    }
    Tile_runtime& runtime = m_tiles[static_cast<std::size_t>(tile)];
    runtime.loading = false;
    if (pending->failed) {
        runtime.failed = true;
        log_render->warn("Lightmap_streamer: tile {} load failed: {}", tile, pending->error);
        if (m_context.lightmap_report != nullptr) {
            m_context.lightmap_report->add_error(
                Lightmap_report::Stage::stream,
                m_manifest.tiles[static_cast<std::size_t>(tile)].payload,
                pending->error
            );
        }
        return;
    }
    if ((pending->width != m_manifest.tile_size) || (pending->height != m_manifest.tile_size)) {
        runtime.failed = true;
        if (m_context.lightmap_report != nullptr) {
            m_context.lightmap_report->add_error(
                Lightmap_report::Stage::stream,
                m_manifest.tiles[static_cast<std::size_t>(tile)].payload,
                fmt::format("payload is {}x{}, manifest tile size is {}", pending->width, pending->height, m_manifest.tile_size)
            );
        }
        return;
    }

    ensure_texture();

    // Staging buffer -> slot sub-rect; standalone submit (the copy must
    // complete before the mapping below makes the slot visible).
    const std::size_t byte_count = pending->pixels.size() * sizeof(uint16_t);
    Buffer staging{
        m_graphics_device,
        Buffer_create_info{
            .capacity_byte_count                    = byte_count,
            .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
            .usage                                  = Buffer_usage::transfer_src,
            .required_memory_property_bit_mask      =
                Memory_property_flag_bit_mask::host_read |
                Memory_property_flag_bit_mask::host_write,
            .preferred_memory_property_bit_mask     =
                Memory_property_flag_bit_mask::host_coherent,
            .debug_label = erhe::utility::Debug_label{"lightmap stream staging"}
        }
    };
    {
        const std::span<std::byte> mapped = staging.map_bytes(0, byte_count);
        std::memcpy(mapped.data(), pending->pixels.data(), byte_count);
        staging.unmap();
    }
    const glm::ivec2 slot_origin = get_slot_origin(pending->slot);
    constexpr unsigned int bake_thread_slot = 6;
    Command_buffer& command_buffer = m_graphics_device.get_command_buffer(bake_thread_slot);
    command_buffer.begin();
    command_buffer.transition_texture_layout(*m_texture, Image_layout::transfer_dst_optimal);
    {
        Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
        const std::uintptr_t bytes_per_row = static_cast<std::uintptr_t>(m_manifest.tile_size) * 8u; // RGBA16F
        blit.copy_from_buffer(
            &staging,
            0,
            bytes_per_row,
            bytes_per_row * static_cast<std::uintptr_t>(m_manifest.tile_size),
            glm::ivec3{m_manifest.tile_size, m_manifest.tile_size, 1},
            m_texture.get(),
            0, 0,
            glm::ivec3{slot_origin.x, slot_origin.y, 0}
        );
    }
    command_buffer.transition_texture_layout(*m_texture, Image_layout::shader_read_only_optimal);
    command_buffer.end();
    Command_buffer* command_buffers[] = { &command_buffer };
    m_graphics_device.submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
    m_graphics_device.wait_idle();

    runtime.slot = pending->slot;
    m_slot_to_tile[static_cast<std::size_t>(pending->slot)] = tile;
    ++m_resident_count;
    apply_tile_regions(scene_root, tile);
    log_render->info("Lightmap_streamer: tile {} resident in slot {}", tile, pending->slot);
}

void Lightmap_streamer::reapply_regions(Scene_root& scene_root)
{
    if (!m_manifest_loaded || (m_scene_root != &scene_root)) {
        return; // nothing applied yet; the next update() publishes fresh
    }
    for (int tile = 0; tile < static_cast<int>(m_manifest.tiles.size()); ++tile) {
        apply_tile_regions(scene_root, tile);
    }
    log_render->info("Lightmap_streamer: region mappings re-applied ({} tiles)", m_manifest.tiles.size());
}

void Lightmap_streamer::update(Scene_root& scene_root, const glm::vec3* camera_position)
{
    if (m_scene_root != &scene_root) {
        reset();
        m_scene_root = &scene_root;
    }
    if (m_foreign_rejected) {
        return; // another scene's set; re-checked on invalidate()
    }
    if (!m_manifest_loaded) {
        if (!try_load_manifest(scene_root)) {
            return;
        }
    }

    upload_pending(scene_root);

    if (m_pending) {
        return; // one load in flight at a time
    }

    // Rank tiles by camera XZ distance to their world bounds; without a
    // camera keep the current residency (but fill free slots in id order).
    const int tile_count = static_cast<int>(m_manifest.tiles.size());
    const auto distance_of = [this, camera_position](const int tile) -> float {
        if (camera_position == nullptr) {
            return static_cast<float>(tile); // id order
        }
        const Lightmap_tile_io::Tile_entry& entry = m_manifest.tiles[static_cast<std::size_t>(tile)];
        const glm::vec2 p{camera_position->x, camera_position->z};
        const glm::vec2 lo{entry.bounds_min.x, entry.bounds_min.z};
        const glm::vec2 hi{entry.bounds_max.x, entry.bounds_max.z};
        const glm::vec2 clamped = glm::clamp(p, lo, hi);
        return glm::distance(p, clamped);
    };

    // Best non-resident candidate and worst resident victim.
    int   best_candidate  = -1;
    float best_distance   = std::numeric_limits<float>::max();
    for (int tile = 0; tile < tile_count; ++tile) {
        const Tile_runtime& runtime = m_tiles[static_cast<std::size_t>(tile)];
        if ((runtime.slot >= 0) || runtime.loading || runtime.failed || !runtime.on_disk) {
            continue;
        }
        const float d = distance_of(tile);
        if (d < best_distance) {
            best_distance  = d;
            best_candidate = tile;
        }
    }
    if (best_candidate < 0) {
        return;
    }

    // Free slot first.
    for (std::size_t slot = 0; slot < m_slot_to_tile.size(); ++slot) {
        if (m_slot_to_tile[slot] < 0) {
            start_load(best_candidate, static_cast<int>(slot));
            return;
        }
    }

    // All slots taken: evict the furthest resident tile, with hysteresis so
    // a camera sitting on a boundary does not thrash 32 MiB loads - the
    // incoming tile must be closer by a quarter of its own XZ extent.
    int   victim_tile     = -1;
    float victim_distance = -1.0f;
    for (std::size_t slot = 0; slot < m_slot_to_tile.size(); ++slot) {
        const int tile = m_slot_to_tile[slot];
        if (tile < 0) {
            continue;
        }
        const float d = distance_of(tile);
        if (d > victim_distance) {
            victim_distance = d;
            victim_tile     = tile;
        }
    }
    if (victim_tile < 0) {
        return;
    }
    const Lightmap_tile_io::Tile_entry& candidate_entry = m_manifest.tiles[static_cast<std::size_t>(best_candidate)];
    const float candidate_extent = std::max(
        candidate_entry.bounds_max.x - candidate_entry.bounds_min.x,
        candidate_entry.bounds_max.z - candidate_entry.bounds_min.z
    );
    const float margin = 0.25f * std::max(candidate_extent, 1.0f);
    if (best_distance + margin >= victim_distance) {
        return;
    }
    Tile_runtime& victim = m_tiles[static_cast<std::size_t>(victim_tile)];
    const int freed_slot = victim.slot;
    victim.slot = -1;
    m_slot_to_tile[static_cast<std::size_t>(freed_slot)] = -1;
    --m_resident_count;
    apply_tile_regions(scene_root, victim_tile); // zeroes the mappings
    start_load(best_candidate, freed_slot);
}

}
