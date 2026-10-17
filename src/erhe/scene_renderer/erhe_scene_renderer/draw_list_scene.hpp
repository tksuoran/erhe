#pragma once

#include "erhe_scene_renderer/draw_list.hpp"
#include "erhe_scene_renderer/draw_list_object.hpp"
#include "erhe_scene_renderer/material_set.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include "erhe_item/item.hpp"

#include <glm/glm.hpp>

#include <array>
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

class Caster_vertex_extent;
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
    // DDGI (doc/editor/ddgi.md phase 6): whether the probe volume is active
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
    // The culling view of this pass (Draw_list_scene::add_cull_view() of
    // the view frustum's inward-facing world-space planes,
    // erhe::math::extract_frustum_planes()): entries whose world AABB lies
    // fully outside one of its planes are not drawn. Skinned entries are
    // never culled (their bounds follow the joints, not the transform hook).
    // Cull_view::none: no culling (multiview passes).
    Cull_view                               cull_view           {Cull_view::none};
    std::string_view                        debug_label         {};
    // The frame being drawn: the published slot frame_index %
    // frames_in_flight is read (Draw_list_scene::publish).
    uint64_t                                frame_index         {0};
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
    // The culling view of this pass: Draw_list_scene::add_cull_view() of
    // the planes of its light frustum (one cube face for a point light),
    // same convention and same per-entry test as
    // Draw_color_parameters::cull_view. The caller leaves out the planes its
    // pipeline does not clip at (the depth planes of a depth-clamped
    // directional pass). Cull_view::none: no culling.
    Cull_view                               cull_view           {Cull_view::none};
    std::string_view                        debug_label         {};
    // The frame being drawn (Draw_color_parameters::frame_index).
    uint64_t                                frame_index         {0};
};

// The shadow pass filter: the objects that cast shadows. Shadow_renderer
// draws casters through it and the frustum fit gathers caster bounds
// through it (Shadow_bounds_selection::caster_filter).
inline constexpr erhe::Item_filter shadow_caster_filter{
    .require_all_bits_set           = erhe::Item_flags::visible | erhe::Item_flags::active | erhe::Item_flags::shadow_cast,
    .require_at_least_one_bit_set   = 0u,
    // proxy_hidden: visually replaced by a render_proxy that casts the
    // shadows instead (lightmap piece meshes).
    .require_all_bits_clear         = erhe::Item_flags::proxy_hidden,
    .require_at_least_one_bit_clear = 0u
};

// Which registered objects the shadow frustum fit reads
// (Draw_list_scene::prepare_shadow_bounds).
class Shadow_bounds_selection
{
public:
    // Layers to gather from; an EMPTY span selects every layer.
    std::span<const erhe::scene::Layer_id> layers       {};
    // Casters: objects passing this filter (the shadow pass filter) that
    // hold at least one shadow entry. Receivers: every visible, active
    // object of the layers.
    erhe::Item_filter                      caster_filter{shadow_caster_filter};
};

// What Shadow_renderer's frustum fit reads from the registered objects for
// one frame (Draw_list_scene::get_shadow_bounds): one world AABB per caster
// and per receiver, in object order, and the caster vertex rounding inputs,
// one per caster.
class Shadow_bounds
{
public:
    std::vector<erhe::math::Aabb>     caster_world_aabbs;
    std::vector<erhe::math::Aabb>     receiver_world_aabbs;
    std::vector<Caster_vertex_extent> caster_vertex_extents;
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
    // headset session is active (R19): the view configurations the first
    // color draw of a list resolves together.
    std::span<const uint32_t>  multiview_view_counts{};
    // This draw list's own material slot space
    // (doc/erhe/draw_list_material_set.md D0, D3). Separate from the forward
    // set of the owning Scene_root, because these records are CACHED and
    // consumed on later frames, so their slots must be stable in a way the
    // bucket path's per-pass records never need.
    Material_set_create_info   material_set_create_info{};
    // Initial value of set_exclude_unlit_from_shadows(), so a new instance
    // classifies its first registrations with the caller's setting.
    bool                       exclude_unlit_from_shadows{false};
};

// Persistent, incrementally maintained rendering-side representation of a
// scene: registered objects classified into draw lists once, reused every
// frame (doc/erhe/draw_list_renderer.md).
// One instance per Scene_root, owned like m_physics_world / m_raytrace_scene.
//
// Threading (doc/erhe/draw_list_renderer.md section 9.3): register / unregister /
// flush_pending / prepare_shadow_bounds / publish are main-thread only
// (flush_pending, prepare_shadow_bounds and publish in the prepare phase);
// draw_color / draw_shadow / has_drawable_entries / get_shadow_bounds run on
// the render thread in the render phase (erhe::utility::Thread_roles).
// Scene-side hooks that may run on worker threads use the enqueue_* API;
// flush_pending() applies the queue.
//
// The draw lists the prepare phase maintains are its own working copy; the
// render phase reads only the copy publish() made for its frame, one per
// frame in flight, so the prepare phase of one frame never writes what the
// render phase of another frame reads.
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
    // Material reassignment (R4, doc/erhe/draw_list_material_set.md D11).
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
    // so changing this rebuilds every list at the next flush_pending();
    // setting the current value is a no-op. Main thread (owner thread),
    // called from the change sites of the application setting. Initial
    // value: Draw_list_scene_create_info::exclude_unlit_from_shadows.
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
    // The mesh node's world transform changed (Mesh::handle_transform_update
    // hook). Applied in flush_pending(): the object's records get the new
    // world_from_node / normal_transform once per frame, however many
    // updates the node saw (dedup by Node_transforms::world_from_node_serial).
    void enqueue_transform_update(const std::shared_ptr<erhe::scene::Mesh>& mesh);
    // A per-primitive upload value that does not affect draw list identity
    // changed (Mesh_primitive::lightmap_uv_scale_offset): rewrite the object's
    // records in flush_pending() without re-classification.
    void enqueue_refresh   (const std::shared_ptr<erhe::scene::Mesh>& mesh);
    // A primitive of the mesh grew its bounds in place (the live
    // mesh-component drag; Scene_host::on_mesh_bounds_changed): rewrite the
    // object's entry AABBs and node extent in flush_pending(), nothing else.
    void enqueue_bounds_update(const std::shared_ptr<erhe::scene::Mesh>& mesh);
    // Main thread, prepare phase, once per frame before any draw. The caller (Scene_root)
    // holds its item_host_mutex around this call so registration never reads
    // a Buffer_mesh that a worker is replacing.
    void flush_pending     ();
    // Main thread, prepare phase, once per frame after flush_pending() and
    // after the frame's joint poses were captured (Joint_poses::capture
    // assigns each skin's Skin_data::joint_buffer_index): first rewrites the
    // joint slot fields of the skinned objects whose skin moved to another
    // slot, then copies every draw list whose change serial moved since the
    // slot frame_index % frames_in_flight last published it into that slot.
    // A frame in which nothing changed copies nothing; a change is copied
    // into each slot once. The caller holds item_host_mutex, as for
    // flush_pending() (the joint slot rewrite reads the meshes).
    void publish           (uint64_t frame_index, std::size_t frames_in_flight);
    [[nodiscard]] auto get_pending_count() const -> std::size_t;

    // Number of registered-vs-observed negative-determinant mismatches seen
    // in flush_pending() (R10b diagnostics).
    [[nodiscard]] auto get_determinant_flip_count() const -> std::size_t { return m_determinant_flip_count; }

    // --- Drawing (render thread, render phase, inside the owning renderer's pass)
    // Draws every list matching layers / blending, filtering entries by
    // parameters.filter against their mirrored flag bits (R7a). Cached
    // resolutions are used (R20); a not-yet-resolved (view config,
    // sub-variant) resolves once, lazily. Returns what was drawn.
    // Both read the lists publish() published for parameters.frame_index and
    // verify that slot was published for that frame.
    auto draw_color (const Draw_color_parameters&  parameters) -> Draw_statistics;
    auto draw_shadow(const Draw_shadow_parameters& parameters) -> Draw_statistics;
    // Culling views (doc/erhe/draw_list_renderer.md Q6; render thread,
    // render phase): adds the inward-facing world-space planes (at most 6)
    // of one pass of frame_index to the frame's view set and returns its
    // view, for Draw_color_parameters / Draw_shadow_parameters::cull_view.
    // A view with the same purpose and the same planes as one already in
    // the set returns that view; empty planes return Cull_view::none. The
    // first call for a new frame_index starts that frame's set. The first
    // draw that names a view not yet classified classifies every entry of
    // every published list of the view's purpose against all views added
    // since the previous classification, in one pass (one uint64_t mask bit
    // per entry and view), so a caller that adds all of its views before
    // its first draw gets one classification for all of them.
    [[nodiscard]] auto add_cull_view(uint64_t frame_index, Draw_purpose purpose, std::span<const glm::vec4> planes) -> Cull_view;
    // The same for a view whose first test is a range sphere (a point light
    // cube face, doc/plans/culling.md phase 2): an entry whose box lies
    // wholly outside the sphere is outside the view; otherwise the planes
    // decide. Sharing also compares the sphere. Views naming bitwise the
    // same sphere one after the other share its test, so the six faces of a
    // light added in a row test it once per entry.
    [[nodiscard]] auto add_cull_view(uint64_t frame_index, Draw_purpose purpose, std::span<const glm::vec4> planes, const Cull_sphere& range_sphere) -> Cull_view;
    // The shadow frustum fit's inputs from the registered objects, prepared
    // once per frame in the prepare phase (main thread, after
    // flush_pending()) into the frame-in-flight slot frame_index %
    // frames_in_flight, so the prepare phase of one frame never writes the
    // slot the render phase of another frame reads. Per object: the entries'
    // world AABB, or the posed bounds for a skinned object (its entries
    // carry none); per caster, the node's world transform and the node-space
    // extent kept on the object. Objects whose bounds are invalid contribute
    // nothing. The slot is gathered again only when the records changed since
    // it was last gathered (registration, flags, transform and bounds hooks)
    // or the selection differs; otherwise only the skinned objects' posed
    // bounds are rewritten.
    void prepare_shadow_bounds(const Shadow_bounds_selection& selection, uint64_t frame_index, std::size_t frames_in_flight);
    // The slot prepare_shadow_bounds() filled for frame_index (render
    // thread, render phase). Verifies that the slot was prepared for that
    // frame.
    [[nodiscard]] auto get_shadow_bounds(uint64_t frame_index) const -> const Shadow_bounds&;
    // Diagnostics: full gathers prepare_shadow_bounds() ran (a frame in
    // which nothing changed runs none).
    [[nodiscard]] auto get_shadow_bounds_gather_count() const -> std::size_t { return m_shadow_bounds_gather_count; }
    // True when at least one entry would be drawn for the selection: lets a
    // caller skip its per-pass prologue (buffer uploads / binds) entirely,
    // the way Forward_renderer::render() early-outs on empty mesh spans.
    [[nodiscard]] auto has_drawable_entries(
        uint64_t                               frame_index,
        Draw_purpose                           purpose,
        std::span<const erhe::scene::Layer_id> layers,
        Draw_blending_selection                blending,
        const erhe::Item_filter&               filter
    ) const -> bool;

    // Byte stride of one record in Draw_list::primitive_records
    // (Primitive_interface::primitive_struct size).
    [[nodiscard]] auto get_primitive_record_stride() const -> std::size_t { return m_primitive_record_stride; }

    // The material GPU slot one entry's CACHED record was written with
    // (doc/erhe/draw_list_material_set.md, phase 1). This is what a draw
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
    // the transform hook / refresh hook / joint slot sync (publish).
    [[nodiscard]] auto get_transform_update_count        () const -> std::size_t { return m_transform_update_count; }
    [[nodiscard]] auto get_refresh_count                 () const -> std::size_t { return m_refresh_count; }
    [[nodiscard]] auto get_slot_sync_count               () const -> std::size_t { return m_slot_sync_count; }
    // Draw lists publish() copied into a frame slot, summed over all calls
    // (a frame in which nothing changed adds none).
    [[nodiscard]] auto get_published_list_copy_count     () const -> std::size_t { return m_published_list_copy_count; }

private:
    class Pending_op
    {
    public:
        enum class Kind : uint8_t { rebuild_all, register_, unregister, reregister, material_update, set_flags, transform, refresh, bounds };
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

    // Defers a full rebuild to the next flush_pending(), ahead of that
    // flush's queued ops (D1d).
    void enqueue_rebuild_all();

    void assert_main_thread() const;
    static void assert_render_phase();
    auto allocate_object   () -> uint32_t;
    void release_object    (uint32_t object_index);
    // Classify all primitives of object into lists; fills object.locations.
    void add_entries       (uint32_t object_index);
    // Remove all entries of object from their lists; clears object.locations.
    void remove_entries    (uint32_t object_index);
    auto get_or_create_draw_list(const Draw_list_key& key) -> uint32_t;
    void remove_entry      (const Draw_list_entry_location& location);
    // The working copy of one list for writing: bumps its change serial, so
    // the next publish() copies it. Every mutation of a list's key, entries
    // or records goes through here.
    [[nodiscard]] auto edit_draw_list(uint32_t draw_list_index) -> Draw_list&;
    // --- Primitive records (doc/erhe/draw_list_performance_improvements.md) ---
    // For writing (through edit_draw_list()).
    [[nodiscard]] auto get_record(const Draw_list_entry_location& location) -> std::byte*;
    // Full record from the live mesh / node / primitive for one entry.
    void write_entry_record      (const Draw_list_object& object, const Draw_list_entry& entry, std::byte* record) const;
    // world_from_node / normal_transform of every record of the object from
    // its node; records object.transform_serial.
    void write_object_transform  (uint32_t object_index);
    // material_index / skinning_factor / base_joint_index of every record of
    // the object from its material slots and its skin's joint slot; records
    // object.joint_slot.
    void write_object_gpu_slots  (uint32_t object_index);
    // Rewrite every record of the object (refresh hook).
    void refresh_object_records  (uint32_t object_index);
    // Transform hook application (dedup by node transform serial).
    void update_object_transform (uint32_t object_index);
    // publish(): the joint slot of a skinned object's skin changed since its
    // records were written -> rewrite that object's slot fields.
    void sync_joint_slots        ();
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
    // --- Render phase (render thread) ---
    // The lists publish() published for frame_index; verifies the slot was
    // published for that frame.
    [[nodiscard]] auto get_published_draw_lists(uint64_t frame_index) const -> const std::vector<Draw_list>&;
    // The render phase's resolution cache entry of a list, reset when the
    // list at that index is a different list (identity) than last time.
    [[nodiscard]] auto get_resolutions(std::size_t draw_list_index, const Draw_list& draw_list) -> Draw_list_resolutions&;
    void resolve_color_list   (Draw_list_resolutions& resolutions, const Draw_list& draw_list); // all enumerated view configs
    auto resolve_color_stages (Draw_list_resolutions& resolutions, const Draw_list& draw_list, uint16_t multiview_count) -> const erhe::graphics::Reloadable_shader_stages*;
    auto resolve_shadow_stages(Draw_list_resolutions& resolutions, const Draw_list& draw_list, Shadow_sub_variant sub_variant) -> const erhe::graphics::Reloadable_shader_stages*;
    void set_color_environment(const Color_environment& environment);
    // Draw one list in chunks of <= max primitives per multi-draw (P3a).
    void draw_list_chunks(
        const Draw_list&                         draw_list,
        erhe::graphics::Render_command_encoder&  render_encoder,
        erhe::graphics::Render_pipeline&         render_pipeline,
        Primitive_buffer&                        primitive_buffer,
        Draw_indirect_buffer&                    draw_indirect_buffer,
        const Primitive_interface_settings&      primitive_settings,
        const erhe::Item_filter&                 filter,
        std::size_t                              draw_list_index,
        Cull_view                                cull_view,
        Draw_statistics&                         statistics
    );
    // Classifies the published lists of frame_index against the views added
    // since the last classification when cull_view is one of them
    // (add_cull_view()); verifies cull_view belongs to frame_index.
    void classify_cull_views(uint64_t frame_index, Cull_view cull_view);
    enum class Cull_view_kind : uint8_t
    {
        planes,            // the planes alone
        sphere_and_planes  // the range sphere first, then the planes
    };
    [[nodiscard]] auto insert_cull_view(uint64_t frame_index, Draw_purpose purpose, Cull_view_kind kind, std::span<const glm::vec4> planes, const Cull_sphere& sphere) -> Cull_view;
    // The bounds the entries and the object carry, from the mesh's
    // primitives: at registration (add_entries) and on the bounds hook
    // (Pending_op::Kind::bounds). The transform hook rewrites the entries'
    // world AABB alone (write_object_transform): the node extent does not
    // move with the node.
    void write_object_bounds(uint32_t object_index);

    // A skinned object in a shadow bounds slot: where its posed bounds sit
    // in the slot's vectors, rewritten by every prepare_shadow_bounds().
    class Shadow_bounds_skinned_object
    {
    public:
        static constexpr uint32_t none = UINT32_MAX;
        uint32_t object_index     {0};
        uint32_t caster_position  {none};
        uint32_t receiver_position{none};
        // Whether the posed bounds were valid at the gather: a change drops
        // or adds the object, which takes a full gather.
        bool     valid            {false};
    };
    class Shadow_bounds_slot
    {
    public:
        Shadow_bounds                            bounds;
        std::vector<Shadow_bounds_skinned_object> skinned_objects;
        // The selection and m_shadow_bounds_serial of the last full gather.
        std::vector<erhe::scene::Layer_id>       layers;
        erhe::Item_filter                        caster_filter{};
        uint64_t                                 serial       {0};
        // The frame the slot was last prepared for.
        uint64_t                                 frame_index  {0};
        bool                                     prepared     {false};
    };
    // Something the shadow bounds gather reads changed: object membership,
    // entries, flags, world AABBs, node extents or node transforms.
    void mark_shadow_bounds_changed() { ++m_shadow_bounds_serial; }
    void gather_shadow_bounds        (const Shadow_bounds_selection& selection, Shadow_bounds_slot& slot);
    // Rewrites the skinned objects' posed bounds in the slot; false when an
    // object's bounds became valid or invalid (the caller gathers again).
    [[nodiscard]] auto update_skinned_shadow_bounds(Shadow_bounds_slot& slot) const -> bool;

    // The draw lists published for one frame in flight: parallel to
    // m_draw_lists (same indices), each the copy of the working list as of
    // its serial.
    class Draw_list_slot
    {
    public:
        std::vector<Draw_list> lists;
        // m_draw_list_serial as of the slot's last publish: equal means no
        // list changed since, and the publish compares nothing.
        uint64_t               serial     {0};
        // The frame the slot was last published for.
        uint64_t               frame_index{0};
        bool                   published  {false};
    };

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

    // The working copy (main thread, prepare phase).
    std::vector<Draw_list>                                           m_draw_lists;
    // Last value handed out as a Draw_list::serial / Draw_list::identity.
    uint64_t                                                         m_draw_list_serial  {0};
    uint64_t                                                         m_draw_list_identity{0};
    std::unordered_map<Draw_list_key, uint32_t, Draw_list_key_hash>  m_draw_list_index_by_key;
    std::vector<Draw_list_object>                                    m_objects;
    std::vector<uint32_t>                                            m_free_object_indices;
    std::unordered_map<const erhe::scene::Mesh*, uint32_t>           m_object_index_by_mesh;
    std::size_t                                                      m_alive_object_count{0};
    // Alive objects with mobility skinned (joint GPU-slot sync scans only these).
    std::vector<uint32_t>                                            m_skinned_object_indices;

    mutable std::mutex                                               m_pending_mutex;
    std::vector<Pending_op>                                          m_pending;
    // flush_pending() swaps m_pending with this and clears it after the
    // ops ran, so both vectors keep their capacity across frames.
    std::vector<Pending_op>                                          m_flushing;
    // Debug label of one draw list, cleared at use (capacity kept).
    std::string                                                      m_list_label;
    // Per-entry pass mask of the draw list being drawn (filter and culling
    // view), resized at use (capacity kept).
    std::vector<std::uint8_t>                                        m_entry_passes;
    std::size_t                                                      m_determinant_flip_count{0};
    std::size_t                                                      m_transform_update_count{0};
    std::size_t                                                      m_refresh_count{0};
    std::size_t                                                      m_slot_sync_count{0};
    std::size_t                                                      m_published_list_copy_count{0};
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

    // Render phase (render thread) state: the resolution cache, one per list
    // index, and the color environment it was resolved against.
    std::vector<Draw_list_resolutions>                               m_resolutions;
    Color_environment                                                m_color_environment{};
    bool                                                             m_color_environment_set{false};

    // Render phase (render thread) culling state of the frame being
    // rendered (Q6). The render phase renders one frame at a time, so one
    // set serves every frame in flight: the first add_cull_view() of a new
    // frame index clears it (capacity kept).
    class Cull_view_tests
    {
    public:
        Draw_purpose             purpose    {Draw_purpose::color};
        Cull_view_kind           kind       {Cull_view_kind::planes};
        uint32_t                 plane_count{0};
        std::array<glm::vec4, 6> planes     {};
        Cull_sphere              sphere     {}; // sphere_and_planes only
    };
    std::vector<Cull_view_tests>                                     m_cull_views;
    // Classification scratch, one element per entry of the list being
    // classified (capacity kept): the entry has no bounds; the entry is
    // inside the view being classified; the entry is inside the range sphere
    // last tested; the squared distance from that sphere's center to the
    // entry's box.
    std::vector<uint32_t>                                            m_cull_unbounded;
    std::vector<uint32_t>                                            m_cull_inside;
    std::vector<uint32_t>                                            m_cull_sphere_inside;
    std::vector<float>                                               m_cull_sphere_distance2;
    // Views [0, m_classified_cull_view_count) have their mask bits written.
    std::size_t                                                      m_classified_cull_view_count{0};
    uint64_t                                                         m_cull_frame_index{0};
    bool                                                             m_cull_frame_started{false};
    // Per published list index: one uint64_t per entry per group of 64
    // views, group-major (mask of entry i, view v at
    // [(v / 64) * entry_count + i], bit v % 64). Cleared when a frame
    // starts and grown with zeros at classification (capacity kept).
    std::vector<std::vector<uint64_t>>                               m_cull_masks;
    std::size_t                                                      m_color_environment_change_count{0};
    std::size_t                                                      m_lazy_resolution_count{0};

    // One per frame in flight (sized by the first prepare_shadow_bounds()).
    std::vector<Shadow_bounds_slot>                                  m_shadow_bounds_slots;
    // One per frame in flight (sized by the first publish()).
    std::vector<Draw_list_slot>                                      m_draw_list_slots;
    uint64_t                                                         m_shadow_bounds_serial{1};
    std::size_t                                                      m_shadow_bounds_gather_count{0};
};

} // namespace erhe::scene_renderer
