#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace erhe {

// One application flag bit's registration (Item_flags::register_application_flags).
class Item_flag_info
{
public:
    uint64_t    bit            {0};       // one of Item_flags::application_bit(i)
    const char* label          {nullptr}; // UI label, as Item_flags::c_bit_labels for the library bits
    // The name the glTF ERHE_* extensions serialize the bit by (names,
    // never bit positions: doc/editor/gltf_scene_roundtrip.md phase 3).
    // nullptr: transient session state, never serialized.
    const char* persistent_name{nullptr};
};

// Item state bits. The library bits (0 to count - 1) are the ones the
// erhe::* libraries read; the application defines its own in the reserved
// range application_first_bit..63 and registers their labels, persistent
// names and masks once at startup (register_application_flags), so
// to_string(), the glTF flag serialization and the purpose derivation know
// them without the library naming any of them.
class Item_flags
{
public:
    static constexpr uint64_t none                      = 0u;
    static constexpr uint64_t no_message                = (uint64_t{1} <<  0);
    static constexpr uint64_t no_transform_update       = (uint64_t{1} <<  1);
    static constexpr uint64_t transform_world_normative = (uint64_t{1} <<  2);
    static constexpr uint64_t show_in_ui                = (uint64_t{1} <<  3);
    static constexpr uint64_t shadow_cast               = (uint64_t{1} <<  4);
    static constexpr uint64_t selected                  = (uint64_t{1} <<  5);
    static constexpr uint64_t visible                   = (uint64_t{1} <<  6);
    static constexpr uint64_t content                   = (uint64_t{1} <<  7);
    static constexpr uint64_t id                        = (uint64_t{1} <<  8);
    static constexpr uint64_t negative_determinant      = (uint64_t{1} <<  9);
    static constexpr uint64_t lock_edit                 = (uint64_t{1} << 10);
    // The item is not part of prefab content: the flag persists in node
    // extras when the scene is saved, and instantiating a prefab template
    // filters flagged items out of the instance. Set on editor-generated
    // helpers (e.g. the default camera / lights import_gltf adds to a scene
    // that has none) so they never leak into prefab instances.
    static constexpr uint64_t exclude_from_prefab       = (uint64_t{1} << 11);
    // Implicit container node created when a glTF file is opened/imported,
    // holding the file's scene roots. Not part of the file content: glTF
    // export writes its children in its place (composing its transform),
    // and import re-creates it -- so open/save cycles do not nest one more
    // wrapper node per cycle.
    static constexpr uint64_t import_root               = (uint64_t{1} << 12);
    // Skeleton bone: authored and persistent (saved by name like the other
    // persistent flags; doc/plans/rigging/skeleton_editing.md R1). A Skin
    // entering a scene sets it on the nodes it lists in skin_data.joints, and
    // a node keeps it when no skin lists it. Item_type is per-CLASS
    // (Item<>::get_type() returns Self::get_static_type()), so a plain Node
    // can never report a bone type - bone-ness has to be a per-instance
    // flag. Drives the item tree's bone icon and is_bone().
    static constexpr uint64_t bone                      = (uint64_t{1} << 13);
    // Static geometry that participates in lightmap baking: gets automatic
    // lightmap UVs (texcoord channel 2), an atlas region, and baked lighting.
    // Authored + serialized (by name, like all flags). See
    // doc/editor/lightmap_baking.md.
    static constexpr uint64_t lightmapped               = (uint64_t{1} << 14);
    // Editor-generated render-only stand-in for another item (e.g. the
    // lightmap partitioner's world-space piece meshes). Renders (and casts
    // shadows) in place of its proxy_hidden source but is never user-facing:
    // no show_in_ui, no Item_flags::id, raytrace mask 0
    // (raytrace_node_mask), skipped by glTF export and not serialized -
    // proxies are derived data, rebuilt by their owner.
    static constexpr uint64_t render_proxy              = (uint64_t{1} << 15);
    // The item is visually replaced by a render_proxy: excluded from the
    // visual and shadow render passes, but still fully live - visible flag
    // set, ID-rendered, raytrace-pickable, selectable, editable and
    // exported. Not serialized (the proxy owner re-applies it).
    static constexpr uint64_t proxy_hidden              = (uint64_t{1} << 16);
    // Masks a bone from IK: dragging a bone with the translate tool solves the
    // chain of ancestor bones up to (and including, as the fixed-position
    // root) the first ik_lock bone. Dragging an ik_lock bone itself falls
    // back to plain FK translation. Authored + serialized (by name; see
    // gltf_item_flags.cpp). See doc/plans/rigging/fabrik_ik.md.
    static constexpr uint64_t ik_lock                   = (uint64_t{1} << 17);
    // Effective USD `active` state (doc/erhe/usd_compatibility_design.md X2): the
    // item's own active property AND its own defined property AND the bit of
    // its parent. USD prunes the whole subtree of an inactive prim, and its
    // default traversal predicate reaches neither an undefined prim (composed
    // specifier `over`) nor anything below one, regardless of a descendant's
    // own opinion - so both subtree effects are carried by this derived bit
    // rather than by property inheritance. Clear means the item and
    // everything below it is out of rendering, picking, simulation and every
    // consumer that walks content; the item tree still shows the row, dimmed.
    static constexpr uint64_t active                    = (uint64_t{1} << 18);
    // Content the editor injects into a scene for the duration of the
    // session, so that a file which authors none of it is still usable: the
    // default camera a camera-less file is looked at through. It is not part
    // of what the file says, so every exporter leaves it out and it is never
    // serialized - the next open injects it again. The user's own content
    // never carries the bit, so a camera the user creates is saved.
    static constexpr uint64_t session_only              = (uint64_t{1} << 19);
    // The generated proxy geometry a `cards` draw mode supplies in place of
    // the subtree it replaces (doc/erhe/usd_compatibility.md, "Draw modes"): a
    // child prim of the pruning model prim that the pruning itself must not
    // reach, since it is the replacement. Carried by the proxy mesh and by
    // the materials it owns; the item tree never shows it, no exporter
    // writes it (it is session_only as well), and a viewport pick of it
    // selects the model prim.
    static constexpr uint64_t draw_mode_proxy           = (uint64_t{1} << 20);
    // Per-component transform channel locks (Blender protectflag
    // equivalent): a locked component of the LOCAL (parent-from-node)
    // transform is not changed by interactive editing - the Transform
    // tool, numeric transform fields, and IK (a locked rotation axis acts
    // as an IK DOF lock). Not enforced against animation, physics, or
    // programmatic set_* calls. Authored + serialized by name (see
    // gltf_item_flags.cpp). See doc/plans/rigging/ik_settings.md.
    static constexpr uint64_t lock_translation_x        = (uint64_t{1} << 21);
    static constexpr uint64_t lock_translation_y        = (uint64_t{1} << 22);
    static constexpr uint64_t lock_translation_z        = (uint64_t{1} << 23);
    static constexpr uint64_t lock_rotation_x           = (uint64_t{1} << 24);
    static constexpr uint64_t lock_rotation_y           = (uint64_t{1} << 25);
    static constexpr uint64_t lock_rotation_z           = (uint64_t{1} << 26);
    static constexpr uint64_t lock_scale_x              = (uint64_t{1} << 27);
    static constexpr uint64_t lock_scale_y              = (uint64_t{1} << 28);
    static constexpr uint64_t lock_scale_z              = (uint64_t{1} << 29);
    // The number of library bits (c_bit_labels has exactly this many entries).
    static constexpr uint64_t count                     = 30;

    static constexpr uint64_t lock_translation_mask     = lock_translation_x | lock_translation_y | lock_translation_z;
    static constexpr uint64_t lock_rotation_mask        = lock_rotation_x    | lock_rotation_y    | lock_rotation_z;
    static constexpr uint64_t lock_scale_mask           = lock_scale_x       | lock_scale_y       | lock_scale_z;
    static constexpr uint64_t lock_channel_mask         = lock_translation_mask | lock_rotation_mask | lock_scale_mask;

    // --- The application range ------------------------------------------
    // Bits application_first_bit..63 belong to the application. It names
    // them in a class of its own (the editor: Editor_item_flags) as
    // application_bit(i) and registers them once at startup, before any
    // item is created, with register_application_flags(); the library never
    // names one of them.
    static constexpr uint64_t application_first_bit = 32;
    static constexpr uint64_t application_bit_count = 64 - application_first_bit;
    static constexpr uint64_t application_mask      = ~uint64_t{0} << application_first_bit;
    [[nodiscard]] static constexpr auto application_bit(const unsigned int index) -> uint64_t
    {
        return uint64_t{1} << (application_first_bit + index);
    }
    // Registers the application bits: their labels and persistent names,
    // the transient ones (presentation state that never bumps the item
    // mutation serial, see transient below) and the ones whose presence
    // makes an item's default purpose `guide` (purpose_guide_when_set).
    // Every bit must be in the application range, listed once, and the
    // span must stay alive for the process (the application keeps it in a
    // constexpr table). Registering again replaces the previous set.
    static void register_application_flags(std::span<const Item_flag_info> flags, uint64_t transient_bits, uint64_t purpose_guide_when_set_bits);
    [[nodiscard]] static auto get_application_flags() -> std::span<const Item_flag_info>;
    // The label of one bit position: a library label, a registered
    // application label, or nullptr for a bit neither defines.
    [[nodiscard]] static auto label(uint64_t bit_position) -> const char*;

    // High-frequency presentation-state bits (selection, transform-derived
    // state, and the registered application ones: hover, per-frame debug
    // visualization) that never affect item tree row structure or
    // filtering. Changes to only these bits do not bump the item mutation
    // serial, so they do not invalidate cached item tree rows. `transient`
    // is the library part; get_transient_bits() adds the registered part.
    static constexpr uint64_t transient = selected | negative_determinant;
    [[nodiscard]] static auto get_transient_bits() -> uint64_t;

    // Derived bits (D23 in doc/erhe/property_system.md): the effective value
    // of the visible and active properties (Item_base) and of the
    // shadow_cast / lightmapped properties (erhe::scene::Mesh), written
    // only by the property changed callbacks. set_flag_bits rejects them;
    // write the property instead (set_visible,
    // set_value(Item_base::active_property, ...),
    // set_value(Mesh::shadow_cast_property, ...)).
    static constexpr uint64_t derived = visible | active | shadow_cast | lightmapped;

    // The flag bits an item's default Purpose is derived from
    // (Item_base::derive_purpose_from_flags): any of the registered
    // purpose_guide_when_set bits set, or show_in_ui clear, means
    // editor-only content (Purpose::guide). A change of one of them
    // refreshes the purpose property's default layer.
    static constexpr uint64_t purpose_guide_when_clear = show_in_ui;
    [[nodiscard]] static auto get_purpose_guide_when_set_bits() -> uint64_t;
    [[nodiscard]] static auto get_purpose_inputs() -> uint64_t;

    static constexpr const char* c_bit_labels[] =
    {
        "No Message",
        "No Transform Update",
        "Transform World Normative",
        "Show In UI",
        "Shadow Cast",
        "Selected",
        "Visible",
        "Content",
        "ID",
        "Negative Determinant",
        "Lock Edit",
        "Exclude From Prefab",
        "Import Root",
        "Bone",
        "Lightmapped",
        "Render Proxy",
        "Proxy Hidden",
        "IK Lock",
        "Active",
        "Session Only",
        "Draw Mode Proxy",
        "Lock Translation X",
        "Lock Translation Y",
        "Lock Translation Z",
        "Lock Rotation X",
        "Lock Rotation Y",
        "Lock Rotation Z",
        "Lock Scale X",
        "Lock Scale Y",
        "Lock Scale Z",
    };

    [[nodiscard]] static auto to_string(uint64_t mask) -> std::string;
};

class Item_filter
{
public:
    [[nodiscard]] auto operator()(uint64_t filter_bits) const -> bool;
    [[nodiscard]] auto operator==(const Item_filter&) const -> bool = default;

    [[nodiscard]] auto describe() const -> std::string;

    uint64_t require_all_bits_set          {0};
    uint64_t require_at_least_one_bit_set  {0};
    uint64_t require_all_bits_clear        {0};
    uint64_t require_at_least_one_bit_clear{0};
};

} // namespace erhe
