#pragma once

#include "erhe_scene_renderer/draw_list.hpp"
#include "erhe_scene_renderer/draw_list_object.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace erhe::scene_renderer {

class Mesh_memory;
class Shader_variant_cache;

// Persistent, incrementally maintained rendering-side representation of a
// scene: registered objects classified into draw lists once, reused every
// frame (doc/draw_list_renderer_requirements.md, doc/draw_list_renderer_plan.md).
// One instance per Scene_root, owned like m_physics_world / m_raytrace_scene.
//
// Threading (plan section 0.3): register / unregister / set flags / rebuild /
// flush_pending / draw are main-thread only. Scene-side hooks that may run on
// worker threads use the enqueue_* API; flush_pending() applies the queue.
//
// Empty draw lists are kept (a list whose last entry was unregistered stays
// in place with zero entries and is reused when a matching primitive
// registers again); draw skips them. This keeps unregister O(entries of the
// object) and draw list indices stable (R11, P2).
class Draw_list_scene
{
public:
    Draw_list_scene(
        Mesh_memory&               mesh_memory,
        Shader_variant_cache&      shader_variant_cache,
        std::span<const uint32_t>  multiview_view_counts
    );
    ~Draw_list_scene() noexcept;

    Draw_list_scene(const Draw_list_scene&)            = delete;
    Draw_list_scene& operator=(const Draw_list_scene&) = delete;

    // --- Main-thread API -------------------------------------------------
    // Registers a mesh: classifies every renderable primitive into the draw
    // lists it belongs to (creating lists on demand) and returns the handle.
    // Registering an already-registered mesh re-registers it.
    auto register_object  (const Draw_list_object_create_info& create_info) -> Draw_list_object_id;
    void unregister_object(Draw_list_object_id id);
    void unregister_object(const erhe::scene::Mesh* mesh);
    // Unregister + register from the stored create info (R12). Returns the
    // new id (the object gets a new generation, possibly a new index).
    auto reregister_object(Draw_list_object_id id) -> Draw_list_object_id;
    // Mirror the mesh Item_flags word into every entry of the object (R12a).
    void set_object_flags (Draw_list_object_id id, uint64_t item_flag_bits);
    void set_object_flags (const erhe::scene::Mesh* mesh, uint64_t item_flag_bits);
    // Drop and recreate all draw lists from the stored object records (R1a).
    void rebuild_all      ();

    [[nodiscard]] auto find_object    (const erhe::scene::Mesh* mesh) const -> Draw_list_object_id;
    [[nodiscard]] auto get_object     (Draw_list_object_id id) const -> const Draw_list_object*;
    [[nodiscard]] auto get_draw_lists () const -> const std::vector<Draw_list>&;
    [[nodiscard]] auto get_object_count() const -> std::size_t;
    [[nodiscard]] auto get_entry_count () const -> std::size_t;
    [[nodiscard]] auto describe        () const -> std::string;

    // --- Any-thread API (hooks): only enqueue; applied by flush_pending() ---
    void enqueue_register  (const Draw_list_object_create_info& create_info);
    void enqueue_unregister(const std::shared_ptr<erhe::scene::Mesh>& mesh);
    void enqueue_reregister(const std::shared_ptr<erhe::scene::Mesh>& mesh);
    void enqueue_set_flags (const std::shared_ptr<erhe::scene::Mesh>& mesh, uint64_t item_flag_bits);
    // Main thread, once per frame before any draw. The caller (Scene_root)
    // holds its item_host_mutex around this call so registration never reads
    // a Buffer_mesh that a worker is replacing.
    void flush_pending     ();
    [[nodiscard]] auto get_pending_count() const -> std::size_t;

    // Number of registered-vs-observed negative-determinant mismatches seen
    // in flush_pending() (R10b diagnostics).
    [[nodiscard]] auto get_determinant_flip_count() const -> std::size_t { return m_determinant_flip_count; }

private:
    class Pending_op
    {
    public:
        enum class Kind : uint8_t { register_, unregister, reregister, set_flags };
        Kind                               kind      {Kind::register_};
        std::shared_ptr<erhe::scene::Mesh> mesh      {};
        Draw_mobility                      mobility  {Draw_mobility::dynamic};
        uint64_t                           flag_bits {0};
    };

    void assert_main_thread() const;
    auto allocate_object   () -> uint32_t;
    void release_object    (uint32_t object_index);
    // Classify all primitives of object into lists; fills object.locations.
    void add_entries       (uint32_t object_index);
    // Remove all entries of object from their lists; clears object.locations.
    void remove_entries    (uint32_t object_index);
    auto get_or_create_draw_list(const Draw_list_key& key) -> uint32_t;
    void remove_entry      (const Draw_list_entry_location& location);

    Mesh_memory&                                                     m_mesh_memory;
    Shader_variant_cache&                                            m_shader_variant_cache;
    std::vector<uint32_t>                                            m_multiview_view_counts;
    std::thread::id                                                  m_owner_thread_id;

    std::vector<Draw_list>                                           m_draw_lists;
    std::unordered_map<Draw_list_key, uint32_t, Draw_list_key_hash>  m_draw_list_index_by_key;
    std::vector<Draw_list_object>                                    m_objects;
    std::vector<uint32_t>                                            m_free_object_indices;
    std::unordered_map<const erhe::scene::Mesh*, uint32_t>           m_object_index_by_mesh;
    std::size_t                                                      m_alive_object_count{0};

    mutable std::mutex                                               m_pending_mutex;
    std::vector<Pending_op>                                          m_pending;
    std::size_t                                                      m_determinant_flip_count{0};
};

} // namespace erhe::scene_renderer
