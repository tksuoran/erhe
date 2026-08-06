#pragma once

#include "renderers/lightmap_tile_io.hpp"

#include <glm/glm.hpp>

#include <atomic>
#include <filesystem>
#include <memory>
#include <vector>

namespace erhe::graphics {
    class Device;
    class Texture;
}
namespace erhe::scene { class Mesh; }

namespace editor {

class App_context;
class Scene_root;

// Streams baked lightmap tiles (Lightmap_tile_io sets on disk) into a
// resident atlas around the camera: every frame the tiles are ranked by
// camera distance to their world bounds and the nearest N (the resident
// budget, default 9 = 3x3 slots) hold a slot in one RGBA16F atlas texture -
// the same single-texture binding the forward renderer already samples, so
// no shader or renderer changes are needed. Streaming a tile in remaps its
// regions' Mesh_primitive::lightmap_uv_scale_offset from tile-local to the
// slot's atlas rect; evicting zeroes them (the renderer's no-lightmap
// gate). File reads run on a worker thread; the GPU upload is a standalone
// staging-buffer submit on the main thread, at most one tile per frame.
//
// Memory is bounded by budget * tile_size^2 * 8 bytes regardless of world
// extents - the end goal of the tiled lightmap design.
class Lightmap_streamer
{
public:
    Lightmap_streamer(erhe::graphics::Device& graphics_device, App_context& context);
    ~Lightmap_streamer() noexcept;

    // Resident-tile budget (display slots); takes effect on the next
    // manifest (re)load.
    void set_budget(int resident_tile_budget);

    // Drop the loaded manifest and all residency (e.g. after a new bake
    // wrote fresh tiles); the next update() reloads from disk.
    void invalidate();

    // Per-frame: (re)load the manifest for the scene when needed, poll the
    // worker-thread file read, upload / evict, and keep the affected mesh
    // primitives' lightmap mappings in sync. No-op while the interactive or
    // offline baker owns the lightmap binding (the caller gates on that).
    void update(Scene_root& scene_root, const glm::vec3* camera_position);

    // Re-push every tile's mapping (resident slots and evicted zero/white
    // fallbacks) onto the mesh primitives. The caller invokes this when the
    // streamer REGAINS the lightmap binding from the interactive baker
    // (whose publish_regions overwrote the mappings with baker slots);
    // residency itself is untouched. No-op without a loaded manifest.
    void reapply_regions(Scene_root& scene_root);

    [[nodiscard]] auto is_available      () const -> bool { return m_manifest_loaded; }
    [[nodiscard]] auto has_resident_tiles() const -> bool { return m_resident_count > 0; }
    [[nodiscard]] auto is_stale          () const -> bool { return m_stale; }
    [[nodiscard]] auto get_texture       () const -> const std::shared_ptr<erhe::graphics::Texture>& { return m_texture; }
    [[nodiscard]] auto get_resident_count() const -> int  { return m_resident_count; }
    [[nodiscard]] auto get_tile_count    () const -> int  { return static_cast<int>(m_tiles.size()); }

private:
    class Tile_runtime
    {
    public:
        int  slot    {-1};   // resident atlas slot; -1 = not resident
        bool loading {false};
        bool failed  {false}; // payload read failed; do not retry every frame
        // Payload file existed when the manifest was loaded. Incremental
        // saves (save-on-evict / Save All) write manifests that list every
        // tile of the layout before all payloads exist; absent ones are
        // simply not-yet-baked, not errors. invalidate() re-checks.
        bool on_disk {true};
    };

    class Pending_load
    {
    public:
        int                   tile{-1};
        int                   slot{-1};
        int                   width{0};
        int                   height{0};
        std::vector<uint16_t> pixels;
        std::string           error;
        std::atomic<bool>     ready {false};
        bool                  failed{false};
    };

    void reset();
    auto try_load_manifest(Scene_root& scene_root) -> bool;
    void ensure_texture();
    auto get_slot_origin(int slot) const -> glm::ivec2;
    // Push the tile's regions' display mapping to the scene's mesh
    // primitives (slot >= 0) or zero it (evict).
    void apply_tile_regions(Scene_root& scene_root, int tile);
    void upload_pending(Scene_root& scene_root);
    void start_load(int tile, int slot);

    erhe::graphics::Device&                  m_graphics_device;
    App_context&                             m_context;
    int                                      m_budget{9};
    bool                                     m_manifest_loaded{false};
    bool                                     m_manifest_missing_logged{false};
    bool                                     m_piece_hint_logged{false};
    // The on-disk set belongs to a different scene (manifest scene_id
    // mismatch); skip it without re-parsing every frame. Cleared by
    // invalidate()/reset().
    bool                                     m_foreign_rejected{false};
    bool                                     m_stale{false};
    std::filesystem::path                    m_directory;
    Scene_root*                              m_scene_root{nullptr};
    Lightmap_tile_io::Manifest               m_manifest;
    std::vector<Tile_runtime>                m_tiles;
    std::vector<int>                         m_slot_to_tile; // -1 = free
    int                                      m_resident_count{0};
    int                                      m_slot_grid{1};
    std::shared_ptr<erhe::graphics::Texture> m_texture;
    std::shared_ptr<Pending_load>            m_pending;
};

}
