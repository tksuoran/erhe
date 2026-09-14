#pragma once

#include "erhe_scene_renderer/draw_list.hpp"
#include "erhe_scene_renderer/draw_list_object.hpp"
#include "erhe_scene_renderer/material_set.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include "erhe_item/item.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace erhe::graphics {
    class Base_render_pipeline;
    class Color_blend_state;
    class Render_command_encoder;
    class Render_pass;
    class Render_pipeline;
}

namespace erhe::scene_renderer {

class Draw_indirect_buffer;
class Mesh_memory;
class Primitive_buffer;
class Shader_variant_cache;

// Environment configuration of the color purpose (R18): the pass-derived
// Shader_key components that are identical for every color pass of a scene in
// a frame. Recomputed cheaply by the caller per pass and compared; a change
// invalidates the cached color resolutions of every list.
class Color_environment
{
public:
    Light_layer_partition light_partition{};
    uint32_t              shadow_filter    {0};
    uint32_t              shadow_bias      {1};
    uint32_t              shadow_technique {0};
    uint32_t              shadow_depth_bits{0};
    // DDGI (doc/ddgi-plan.md phase 6): whether the probe volume is active
    // this frame. Toggling it changes the shader variant of every color
    // draw, so it belongs in the environment the cached resolutions key on.
    bool                  ddgi_enabled     {false};

    [[nodiscard]] auto operator==(const Color_environment& other) const -> bool;
    // The environment Shader_key exactly as Forward_renderer::render() builds
    // it (light counts, shadow axes; SHADER_DEBUG 0), before the multiview
    // axis is added per view configuration.
    [[nodiscard]] auto make_environment_key() const -> Shader_key;
};

// Everything draw_color() needs beyond what the lists carry (R6/R7/R8/R8a).
// Not the material set: that is this scene's own (R8a as amended), reached
// through get_material_set() by the pass that binds it.
// GPU buffers are the caller's (Forward_renderer); the caller has already
// done its per-pass camera / light / material / joint / texture-heap
// update + bind sequence.
class Draw_color_parameters
{
public:
    erhe::graphics::Render_command_encoder& render_encoder;
    const erhe::graphics::Render_pass*      render_pass         {nullptr};
    erhe::graphics::Base_render_pipeline&   base_render_pipeline;
    Primitive_buffer&                       primitive_buffer;
    Draw_indirect_buffer&                   draw_indirect_buffer;
    Primitive_interface_settings            primitive_settings  {};
    erhe::Item_filter                       filter              {};
    std::span<const erhe::scene::Layer_id>  layers              {};
    Draw_blending_selection                 blending            {Draw_blending_selection::opaque_only};
    // 0 for single view passes, N >= 2 for multiview (R19).
    uint16_t                                multiview_count     {0};
    Color_environment                       environment         {};
    // nullptr: pick color_blend_disabled / color_blend_premultiplied by the
    // list's blending class, as Forward_renderer::render() does.
    const erhe::graphics::Color_blend_state* color_blend_override{nullptr};
    std::string_view                        debug_label         {};
};

class Draw_shadow_parameters
{
public:
    erhe::graphics::Render_command_encoder& render_encoder;
    const erhe::graphics::Render_pass*      render_pass         {nullptr};
    erhe::graphics::Base_render_pipeline&   base_render_pipeline;
    const erhe::graphics::Color_blend_state* color_blend        {nullptr};
    Primitive_buffer&                       primitive_buffer;
    Draw_indirect_buffer&                   draw_indirect_buffer;
    erhe::Item_filter                       filter              {};
    std::span<const erhe::scene::Layer_id>  layers              {};
    Shadow_sub_variant                      sub_variant         {Shadow_sub_variant::depth_only};
    std::string_view                        debug_label         {};
};


// What one Draw_list_scene is built from.
class Draw_list_scene_create_info
{
public:
    Mesh_memory*               mesh_memory         {nullptr};
    Shader_variant_cache*      shader_variant_cache{nullptr};
    // Defines the layout (stride + field offsets) of the per-entry primitive
    // records (Draw_list::primitive_records); it must be the same interface
    // the drawing Primitive_buffers were built with.
    const Primitive_interface* primitive_interface {nullptr};
    // 0 for single view passes, plus the XR view count when a multiview
    // headset session is active (R19).
    std::span<const uint32_t>  multiview_view_counts{};
    // This draw list's own material slot space
    // (doc/draw_list_material_set_plan.md D0, D3). Separate from the forward
    // set of the owning Scene_root, because these records are CACHED and
    // consumed on later frames, so their slots must be stable in a way the
    // bucket path's per-pass records never need.
    Material_set_create_info   material_set_create_info{};
};

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
    explicit Draw_list_scene(const Draw_list_scene_create_info& create_info);
    ~Draw_list_scene() noexcept;

    Draw_list_scene(const Draw_list_scene&)            = delete;
    Draw_list_scene& operator=(const Draw_list_scene&) = delete;

    // This draw list's own material slot space (D0). A cached record's
    // material_index is an index into THIS set and no other.
    [[nodiscard]] auto get_material_set()       -> Material_set&       { return m_material_set; }
    [[nodiscard]] auto get_material_set() const -> const Material_set& { return m_material_set; }

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
    // Material reassignment (R4, doc/draw_list_material_set_plan.md D11).
    // Enqueued by Scene_root::on_mesh_material_changed. At flush time it takes
    // the cheap path when the object's entries still classify into exactly the
    // lists they are already in - rewriting only their slot fields - and falls
    // back to a full re-register when they do not.
    void enqueue_material_update(const std::shared_ptr<erhe::scene::Mesh>& mesh);
    // Mirror the mesh Item_flags word into every entry of the object (R12a).
    void set_object_flags (Draw_list_object_id id, uint64_t item_flag_bits);
    void set_object_flags (const erhe::scene::Mesh* mesh, uint64_t item_flag_bits);
    // Drop and recreate all draw lists from the stored object records (R1a).
    void rebuild_all      ();
    // Whether unlit (KHR_materials_unlit) primitives are kept out of the
    // shadow draw lists. Shadow membership is baked into the cached entries,
    // so changing this rebuilds every list; setting the current value is a
    // no-op. Default: unlit primitives DO cast (the historical behavior).
    void set_exclude_unlit_from_shadows(bool value);
    [[nodiscard]] auto get_exclude_unlit_from_shadows() const -> bool { return m_exclude_unlit_from_shadows; }

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
    // The mesh node's world transform changed (Mesh::handle_node_transform_update
    // hook). Applied in flush_pending(): the object's records get the new
    // world_from_node / normal_transform once per frame, however many
    // updates the node saw (dedup by Node_transforms::world_from_node_serial).
    void enqueue_transform_update(const std::shared_ptr<erhe::scene::Mesh>& mesh);
    // A per-primitive upload value that does not affect draw list identity
    // changed (Mesh_primitive::lightmap_uv_scale_offset): rewrite the object's
    // records in flush_pending() without re-classification.
    void enqueue_refresh   (const std::shared_ptr<erhe::scene::Mesh>& mesh);
    // Main thread, once per frame before any draw. The caller (Scene_root)
    // holds its item_host_mutex around this call so registration never reads
    // a Buffer_mesh that a worker is replacing.
    void flush_pending     ();
    [[nodiscard]] auto get_pending_count() const -> std::size_t;

    // Number of registered-vs-observed negative-determinant mismatches seen
    // in flush_pending() (R10b diagnostics).
    [[nodiscard]] auto get_determinant_flip_count() const -> std::size_t { return m_determinant_flip_count; }

    // --- Drawing (main thread, inside the owning renderer's pass) ------------
    // Draws every list matching layers / blending, filtering entries by
    // parameters.filter against their mirrored flag bits (R7a). Cached
    // resolutions are used (R20); a not-yet-resolved (view config,
    // sub-variant) resolves once, lazily. Returns what was drawn.
    auto draw_color (const Draw_color_parameters&  parameters) -> Draw_statistics;
    auto draw_shadow(const Draw_shadow_parameters& parameters) -> Draw_statistics;
    // True when at least one entry would be drawn for the selection: lets a
    // caller skip its per-pass prologue (buffer uploads / binds) entirely,
    // the way Forward_renderer::render() early-outs on empty mesh spans.
    [[nodiscard]] auto has_drawable_entries(
        Draw_purpose                           purpose,
        std::span<const erhe::scene::Layer_id> layers,
        Draw_blending_selection                blending,
        const erhe::Item_filter&               filter
    ) const -> bool;

    // Object mesh lookup for per-entry upload (Primitive_buffer slow path).
    [[nodiscard]] auto get_object_mesh(uint32_t object_index) const -> erhe::scene::Mesh*;
    // Byte stride of one record in Draw_list::primitive_records
    // (Primitive_interface::primitive_struct size).
    [[nodiscard]] auto get_primitive_record_stride() const -> std::size_t { return m_primitive_record_stride; }

    // The material GPU slot one entry's CACHED record was written with
    // (doc/draw_list_material_set_plan.md, phase 1). This is what a draw
    // resolves the material through, and it is not derivable from the mesh:
    // a stale record keeps naming a slot while the Mesh_primitive already
    // names a different material. The regression test for that class of bug
    // has to compare the record against this scene's Material_set, so it
    // needs to read it.
    [[nodiscard]] auto get_entry_material_index(const Draw_list_entry_location& location) const -> uint32_t;

    // Diagnostics: how many times the color environment changed (each change
    // re-resolves every color list) and how many lazy resolutions happened.
    [[nodiscard]] auto get_color_environment_change_count() const -> std::size_t { return m_color_environment_change_count; }
    [[nodiscard]] auto get_lazy_resolution_count         () const -> std::size_t { return m_lazy_resolution_count; }
    [[nodiscard]] auto get_material_change_count         () const -> std::size_t { return m_material_change_count; }
    // Record maintenance counters: objects whose records were rewritten by
    // the transform hook / refresh hook / GPU-slot sync.
    [[nodiscard]] auto get_transform_update_count        () const -> std::size_t { return m_transform_update_count; }
    [[nodiscard]] auto get_refresh_count                 () const -> std::size_t { return m_refresh_count; }
    [[nodiscard]] auto get_slot_sync_count               () const -> std::size_t { return m_slot_sync_count; }

private:
    class Pending_op
    {
    public:
        enum class Kind : uint8_t { rebuild_all, register_, unregister, reregister, material_update, set_flags, transform, refresh };
        Kind                               kind      {Kind::register_};
        std::shared_ptr<erhe::scene::Mesh> mesh      {};
        Draw_mobility                      mobility  {Draw_mobility::dynamic};
        uint64_t                           flag_bits {0};
    };

    // One watched material's shader-variant identity and the change serial it
    // was derived at. check_material_changes() re-derives only where the
    // serial moved, so a frame in which no material changed costs one integer
    // compare per watched material.
    class Material_identity
    {
    public:
        uint64_t hash  {0};
        uint64_t serial{0};
    };

    // D1d: set_exclude_unlit_from_shadows() is reached from inside the
    // rendergraph, after this frame's Material_set::update(), so its rebuild
    // is deferred to the next flush.
    void enqueue_rebuild_all();

    void assert_main_thread() const;
    auto allocate_object   () -> uint32_t;
    void release_object    (uint32_t object_index);
    // Classify all primitives of object into lists; fills object.locations.
    void add_entries       (uint32_t object_index);
    // Remove all entries of object from their lists; clears object.locations.
    void remove_entries    (uint32_t object_index);
    auto get_or_create_draw_list(const Draw_list_key& key) -> uint32_t;
    void remove_entry      (const Draw_list_entry_location& location);
    // --- Primitive records (doc/draw_list_performance_improvements.md) ---
    [[nodiscard]] auto get_record(const Draw_list_entry_location& location) -> std::byte*;
    // Full record from the live mesh / node / primitive for one entry.
    void write_entry_record      (const Draw_list_object& object, const Draw_list_entry& entry, std::byte* record) const;
    // world_from_node / normal_transform of every record of the object from
    // its node; records object.transform_serial.
    void write_object_transform  (uint32_t object_index);
    // material_index / skinning_factor / base_joint_index of every record of
    // the object from the live GPU slots; records object.joint_slot.
    void write_object_gpu_slots  (uint32_t object_index);
    // Rewrite every record of the object (refresh hook).
    void refresh_object_records  (uint32_t object_index);
    // Transform hook application (dedup by node transform serial).
    void update_object_transform (uint32_t object_index);
    // Draw-time: material / joint GPU slots changed since the records were
    // written -> rewrite the affected objects' slot fields.
    void sync_gpu_slots          ();
    // Resolution (R17): compile / look up the shader stages for a list.
    // R12 material-content edits: identity hash = Shader_key{}.derive(material,
    // nullptr, false).get_hash(), i.e. exactly the material-derived key
    // components. Watched per distinct registered material (use-counted);
    // check_material_changes() runs in flush_pending(), re-derives the hash of
    // a watched material only when its change serial moved, and re-registers
    // every object using a material whose hash changed.
    // Membership of this object's materials in this draw list's Material_set,
    // taken BEFORE any record write (R3, D1a). sync_object_materials applies
    // the DIFFERENCE between the object's previous and current material lists,
    // so a material shared between the two keeps a positive count throughout
    // and never loses its slot in between.
    void sync_object_materials   (uint32_t object_index);
    void release_object_materials(uint32_t object_index);
    // True when every entry of the object still classifies into the draw list
    // it already occupies, in which case the object's materials are re-synced
    // and only the slot fields of its existing records are rewritten. False
    // leaves the object untouched for the caller to re-register.
    //
    // It compares the WHOLE key, not the three fields a material can move
    // (blending, double_sided, primitive_key): a mis-predicted "unchanged" key
    // leaves an entry in the wrong draw list, which is a wrong pipeline rather
    // than a wrong slot, and recomputing the classification is what it would
    // cost to compare those three fields anyway.
    [[nodiscard]] auto try_material_slot_update(uint32_t object_index) -> bool;
    void check_material_changes  ();
    void resolve_color_list  (Draw_list& draw_list);                    // all enumerated view configs
    auto resolve_color_stages(Draw_list& draw_list, uint16_t multiview_count) -> const erhe::graphics::Reloadable_shader_stages*;
    auto resolve_shadow_stages(Draw_list& draw_list, Shadow_sub_variant sub_variant) -> const erhe::graphics::Reloadable_shader_stages*;
    void set_color_environment(const Color_environment& environment);
    // Draw one list in chunks of <= max primitives per multi-draw (P3a).
    void draw_list_chunks(
        Draw_list&                               draw_list,
        erhe::graphics::Render_command_encoder&  render_encoder,
        erhe::graphics::Render_pipeline&         render_pipeline,
        Primitive_buffer&                        primitive_buffer,
        Draw_indirect_buffer&                    draw_indirect_buffer,
        const Primitive_interface_settings&      primitive_settings,
        const erhe::Item_filter&                 filter,
        Draw_statistics&                         statistics
    );

    // Declared BEFORE m_objects: a Draw_list_object holds Material_slot_ids
    // into this set, so the set must outlive the objects that name its slots.
    Material_set                                                     m_material_set;
    Mesh_memory&                                                     m_mesh_memory;
    Shader_variant_cache&                                            m_shader_variant_cache;
    const Primitive_interface&                                       m_primitive_interface;
    std::size_t                                                      m_primitive_record_stride{0};
    std::vector<uint32_t>                                            m_multiview_view_counts;
    std::thread::id                                                  m_owner_thread_id;
    bool                                                             m_exclude_unlit_from_shadows{false};

    std::vector<Draw_list>                                           m_draw_lists;
    std::unordered_map<Draw_list_key, uint32_t, Draw_list_key_hash>  m_draw_list_index_by_key;
    std::vector<Draw_list_object>                                    m_objects;
    std::vector<uint32_t>                                            m_free_object_indices;
    std::unordered_map<const erhe::scene::Mesh*, uint32_t>           m_object_index_by_mesh;
    std::size_t                                                      m_alive_object_count{0};
    // Alive objects with mobility skinned (joint GPU-slot sync scans only these).
    std::vector<uint32_t>                                            m_skinned_object_indices;

    mutable std::mutex                                               m_pending_mutex;
    std::vector<Pending_op>                                          m_pending;
    std::size_t                                                      m_determinant_flip_count{0};
    std::size_t                                                      m_transform_update_count{0};
    std::size_t                                                      m_refresh_count{0};
    std::size_t                                                      m_slot_sync_count{0};
    // Material reassignments that took the cheap path (D11).
    std::size_t                                                      m_material_slot_update_count{0};

    // Shader-variant identity of each material any registered object uses,
    // with the material's change serial as of the derivation that produced it.
    // NOT the material set's concern: a change here re-registers objects
    // (their draw list identity may have moved), while a change to what a
    // record is written from only dirties the material buffer. Conflating the
    // two would re-register the world on every colour tweak.
    std::unordered_map<const erhe::primitive::Material*, Material_identity> m_material_identity_hashes;

    std::size_t                                                      m_material_change_count{0};

    Color_environment                                                m_color_environment{};
    bool                                                             m_color_environment_set{false};
    std::size_t                                                      m_color_environment_change_count{0};
    std::size_t                                                      m_lazy_resolution_count{0};
};

} // namespace erhe::scene_renderer
