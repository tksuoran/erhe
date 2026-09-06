#pragma once

#include "erhe_item/unique_id.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_utility/debug_label.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <string>

namespace erhe {

class Item_host;

// USD purpose token (doc/usd-compatibility-plan.md M3): what an item is
// drawn for. `default_` is ordinary content; `render` is the high-quality
// stand-in of a pair; `proxy` is reserved for the low-cost stand-in a
// future proxy mesh provides; `guide` is editor-only content the user
// works WITH rather than ON - a tool, a brush preview, a controller, a
// rendertarget panel. An item that authors none of it gets the value its
// editor-only flag bits imply (Item_base::derive_purpose_from_flags),
// which is the property's per-object default (D31).
enum class Purpose : unsigned int {
    default_ = 0,
    render,
    proxy,
    guide
};

// Enumerator table for Purpose properties.
extern const erhe::property::Enum_info c_purpose_enum_info;

class Item_flags
{
public:
    static constexpr uint64_t none                      = 0u;
    static constexpr uint64_t no_message                = (1u <<  0);
    static constexpr uint64_t no_transform_update       = (1u <<  1);
    static constexpr uint64_t transform_world_normative = (1u <<  2);
    static constexpr uint64_t show_in_ui                = (1u <<  3);
    static constexpr uint64_t show_debug_visualizations = (1u <<  4);
    static constexpr uint64_t shadow_cast               = (1u <<  5);
    static constexpr uint64_t selected                  = (1u <<  6);
    static constexpr uint64_t lock_viewport_selection   = (1u <<  7);
    static constexpr uint64_t lock_viewport_transform   = (1u <<  8);
    static constexpr uint64_t visible                   = (1u <<  9);
    static constexpr uint64_t invisible_parent          = (1u << 10);
    static constexpr uint64_t render_wireframe          = (1u << 11); // TODO
    static constexpr uint64_t render_bounding_volume    = (1u << 12); // TODO
    static constexpr uint64_t content                   = (1u << 13);
    static constexpr uint64_t id                        = (1u << 14);
    static constexpr uint64_t tool                      = (1u << 15);
    static constexpr uint64_t brush                     = (1u << 16);
    static constexpr uint64_t controller                = (1u << 17);
    static constexpr uint64_t rendertarget              = (1u << 18);
    static constexpr uint64_t expand                    = (1u << 19);
    static constexpr uint64_t hovered_in_viewport       = (1u << 20);
    static constexpr uint64_t hovered_in_item_tree      = (1u << 21);
    static constexpr uint64_t negative_determinant      = (1u << 22);
    static constexpr uint64_t lock_edit                 = (1u << 23);
    static constexpr uint64_t show_in_developer_ui      = (1u << 24);
    // Transient, set by the shadow frustum fit debug visualization: this
    // shadow caster's world bounds intersect the selected light's shadow
    // caster volume (F_shadow), i.e. it can contribute to that light's shadow
    // map. Recomputed each frame the visualization runs; not authored or
    // serialized (like selected / hovered_*).
    static constexpr uint64_t affects_shadow            = (1u << 25);
    // The item is not part of prefab content: the flag persists in node
    // extras when the scene is saved, and instantiating a prefab template
    // filters flagged items out of the instance. Set on editor-generated
    // helpers (e.g. the default camera / lights import_gltf adds to a scene
    // that has none) so they never leak into prefab instances.
    static constexpr uint64_t exclude_from_prefab       = (1u << 26);
    // Implicit container node created when a glTF file is opened/imported,
    // holding the file's scene roots. Not part of the file content: glTF
    // export writes its children in its place (composing its transform),
    // and import re-creates it -- so open/save cycles do not nest one more
    // wrapper node per cycle.
    static constexpr uint64_t import_root               = (1u << 27);
    // Skeleton bone: set on a Node that a Skin lists in skin_data.joints.
    // Item_type is per-CLASS (Item<>::get_type() returns Self::get_static_type()),
    // so a plain Node can never report Item_type::bone - joint-ness has to be a
    // per-instance flag. Drives the item tree's bone icon and is_bone().
    static constexpr uint64_t bone                      = (1u << 28);
    // Editor-generated pick/display proxy for a bone: a Mesh in the scene's bone
    // layer, parented under the joint node it represents. Content-adjacent but
    // not content - excluded from the item tree, save, export and prefabs, and
    // never selectable as itself (picking it resolves to the joint Node).
    static constexpr uint64_t bone_proxy                = (1u << 29);
    // Static geometry that participates in lightmap baking: gets automatic
    // lightmap UVs (texcoord channel 2), an atlas region, and baked lighting.
    // Authored + serialized (by name, like all flags). See
    // doc/lightmap_baking_plan.md.
    static constexpr uint64_t lightmapped               = (1u << 30);
    // Editor-generated render-only stand-in for another item (e.g. the
    // lightmap partitioner's world-space piece meshes). Renders (and casts
    // shadows) in place of its proxy_hidden source but is never user-facing:
    // no show_in_ui, no Item_flags::id, raytrace mask 0
    // (raytrace_node_mask), skipped by glTF export and not serialized -
    // proxies are derived data, rebuilt by their owner.
    static constexpr uint64_t render_proxy              = (uint64_t{1} << 31);
    // The item is visually replaced by a render_proxy: excluded from the
    // visual and shadow render passes, but still fully live - visible flag
    // set, ID-rendered, raytrace-pickable, selectable, editable and
    // exported. Not serialized (the proxy owner re-applies it).
    static constexpr uint64_t proxy_hidden              = (uint64_t{1} << 32);
    // Transient companion of hovered_in_viewport, maintained by Hover_tool on
    // every ancestor of the viewport-hovered node (and refreshed when the
    // scene tree structure changes). Lets item trees highlight the closest
    // visible ancestor of a hovered node folded out of view with a plain
    // per-row flag test instead of walking the hierarchy.
    static constexpr uint64_t descendant_hovered_in_viewport = (uint64_t{1} << 33);
    // Graph-editor hover (maintained by the geometry graph window): the scene
    // node referenced by the graph node under the mouse on the node-editor
    // canvas. Exactly zero or one node carries hovered_in_graph at a time;
    // when it changes, every ancestor gets child_hovered_in_graph and every
    // descendant gets ancestor_hovered_in_graph (all three cleared and
    // re-derived together, and refreshed when the scene tree structure
    // changes). Lets item trees highlight graph hovering with plain per-row
    // flag tests, like viewport hovering.
    static constexpr uint64_t hovered_in_graph          = (uint64_t{1} << 34);
    static constexpr uint64_t child_hovered_in_graph    = (uint64_t{1} << 35);
    static constexpr uint64_t ancestor_hovered_in_graph = (uint64_t{1} << 36);
    // Masks a bone from IK: dragging a bone with the translate tool solves the
    // chain of ancestor bones up to (and including, as the fixed-position
    // root) the first ik_lock bone. Dragging an ik_lock bone itself falls
    // back to plain FK translation. Authored + serialized (by name; see
    // gltf_item_flags.cpp). See doc/fabrik-ik-requirements.md.
    static constexpr uint64_t ik_lock                   = (uint64_t{1} << 37);
    static constexpr uint64_t count                     = 38;

    // High-frequency presentation-state bits (selection, hover, per-frame debug
    // visualization, transform-derived state) that never affect item tree row
    // structure or filtering. Changes to only these bits do not bump the item
    // mutation serial, so they do not invalidate cached item tree rows.
    static constexpr uint64_t transient =
        selected | hovered_in_viewport | hovered_in_item_tree | descendant_hovered_in_viewport |
        hovered_in_graph | child_hovered_in_graph | ancestor_hovered_in_graph |
        negative_determinant | affects_shadow;

    // Derived bits (D23 in doc/property-system.md): the effective value
    // of the visible property (Item_base) and of the shadow_cast /
    // lightmapped properties (erhe::scene::Mesh), written only by the
    // property changed callbacks. set_flag_bits rejects them; write the
    // property instead (set_visible, set_value(Mesh::shadow_cast_property, ...)).
    static constexpr uint64_t derived = visible | shadow_cast | lightmapped;

    // The flag bits an item's default Purpose is derived from
    // (Item_base::derive_purpose_from_flags): any of these set, or
    // show_in_ui clear, means editor-only content (Purpose::guide). A
    // change of one of them refreshes the purpose property's default layer.
    static constexpr uint64_t purpose_guide_when_set   = tool | brush | controller | rendertarget;
    static constexpr uint64_t purpose_guide_when_clear = show_in_ui;
    static constexpr uint64_t purpose_inputs           = purpose_guide_when_set | purpose_guide_when_clear;

    static constexpr const char* c_bit_labels[] =
    {
        "No Message",
        "No Transform Update",
        "Transform World Normative",
        "Show In UI",
        "Show Debug",
        "Shadow Cast",
        "Selected",
        "Lock Selection",
        "Lock Transform",
        "Visible",
        "Invisible Parent",
        "Render Wireframe",
        "Render Bounding Volume",
        "Content",
        "ID",
        "Tool",
        "Brush",
        "Controller",
        "Rendertarget",
        "Expand",
        "Hovered in Viewport",
        "Hovered in Item Tree",
        "Negative Determinant",
        "Lock Edit",
        "Show In Developer UI",
        "Affects Shadow",
        "Exclude From Prefab",
        "Import Root",
        "Bone",
        "Bone Proxy",
        "Lightmapped",
        "Render Proxy",
        "Proxy Hidden",
        "Descendant Hovered in Viewport",
        "Hovered in Graph",
        "Child Hovered in Graph",
        "Ancestor Hovered in Graph",
        "IK Lock",
    };

    [[nodiscard]] static auto to_string(uint64_t mask) -> std::string;
};

class Item_type
{
public:
    static constexpr uint64_t index_animation              =  1;
    static constexpr uint64_t index_animation_channel      =  2;
    static constexpr uint64_t index_animation_sampler      =  3;
    static constexpr uint64_t index_bone                   =  4;
    static constexpr uint64_t index_brush                  =  5;
    static constexpr uint64_t index_camera                 =  6;
    static constexpr uint64_t index_composer               =  7;
    static constexpr uint64_t index_frame_controller       =  8;
    static constexpr uint64_t index_grid                   =  9;
    static constexpr uint64_t index_light                  = 10;
    static constexpr uint64_t index_light_layer            = 11;
    static constexpr uint64_t index_material               = 12;
    static constexpr uint64_t index_mesh                   = 13;
    static constexpr uint64_t index_mesh_layer             = 14;
    static constexpr uint64_t index_composition_pass       = 15;
    static constexpr uint64_t index_rendertarget           = 16;
    static constexpr uint64_t index_scene                  = 17;
    static constexpr uint64_t index_skin                   = 18;
    static constexpr uint64_t index_texture                = 19;
    static constexpr uint64_t index_xformable              = 20;
    static constexpr uint64_t index_asset_folder           = 21;
    static constexpr uint64_t index_asset_file_gltf        = 22;
    static constexpr uint64_t index_asset_file_geogram     = 23;
    static constexpr uint64_t index_asset_file_other       = 24;
    static constexpr uint64_t index_content_library_folder = 25;
    static constexpr uint64_t index_content_library_node   = 26;
    static constexpr uint64_t index_physics                = 27;
    static constexpr uint64_t index_raytrace               = 28;
    static constexpr uint64_t index_node_attachment        = 29;
    static constexpr uint64_t index_brush_placement        = 30;
    static constexpr uint64_t index_render_style           = 31;
    static constexpr uint64_t index_graph                  = 32;
    static constexpr uint64_t index_graph_node             = 33;
    static constexpr uint64_t index_graph_link             = 34;
    static constexpr uint64_t index_rendergraph_node       = 35;
    static constexpr uint64_t index_layout                 = 36;
    static constexpr uint64_t index_layout_item            = 37;
    static constexpr uint64_t index_physics_material       = 38;
    static constexpr uint64_t index_collision_filter       = 39;
    static constexpr uint64_t index_physics_joint_settings = 40;
    static constexpr uint64_t index_asset_file_scene       = 41;
    static constexpr uint64_t index_graph_texture          = 42;
    static constexpr uint64_t index_graph_mesh             = 43;
    static constexpr uint64_t index_geometry_graph_mesh    = 44;
    static constexpr uint64_t index_prefab_instance        = 45;
    static constexpr uint64_t index_asset_file_texture     = 46;
    static constexpr uint64_t index_style                  = 47;
    static constexpr uint64_t index_asset_file_usd         = 48;
    static constexpr uint64_t index_typed                  = 49;
    static constexpr uint64_t index_scope                  = 50;
    static constexpr uint64_t index_imageable              = 51;
    static constexpr uint64_t index_xform                  = 52;
    static constexpr uint64_t index_boundable              = 53;
    static constexpr uint64_t index_gprim                  = 54;
    static constexpr uint64_t count                        = 55;

    static constexpr uint64_t none                   =  uint64_t{0};
    static constexpr uint64_t animation              = (uint64_t{1} << index_animation             );
    static constexpr uint64_t animation_channel      = (uint64_t{1} << index_animation_channel     );
    static constexpr uint64_t animation_sampler      = (uint64_t{1} << index_animation_sampler     );
    static constexpr uint64_t bone                   = (uint64_t{1} << index_bone                  );
    static constexpr uint64_t brush                  = (uint64_t{1} << index_brush                 );
    static constexpr uint64_t camera                 = (uint64_t{1} << index_camera                );
    static constexpr uint64_t composer               = (uint64_t{1} << index_composer              );
    static constexpr uint64_t frame_controller       = (uint64_t{1} << index_frame_controller      );
    static constexpr uint64_t grid                   = (uint64_t{1} << index_grid                  );
    static constexpr uint64_t light                  = (uint64_t{1} << index_light                 );
    static constexpr uint64_t light_layer            = (uint64_t{1} << index_light_layer           );
    static constexpr uint64_t material               = (uint64_t{1} << index_material              );
    static constexpr uint64_t mesh                   = (uint64_t{1} << index_mesh                  );
    static constexpr uint64_t mesh_layer             = (uint64_t{1} << index_mesh_layer            );
    static constexpr uint64_t composition_pass       = (uint64_t{1} << index_composition_pass      );
    static constexpr uint64_t rendertarget           = (uint64_t{1} << index_rendertarget          );
    static constexpr uint64_t scene                  = (uint64_t{1} << index_scene                 );
    static constexpr uint64_t skin                   = (uint64_t{1} << index_skin                  );
    static constexpr uint64_t texture                = (uint64_t{1} << index_texture               );
    static constexpr uint64_t xformable              = (uint64_t{1} << index_xformable             );
    static constexpr uint64_t asset_folder           = (uint64_t{1} << index_asset_folder          );
    static constexpr uint64_t asset_file_gltf        = (uint64_t{1} << index_asset_file_gltf       );
    static constexpr uint64_t asset_file_geogram     = (uint64_t{1} << index_asset_file_geogram    );
    static constexpr uint64_t asset_file_other       = (uint64_t{1} << index_asset_file_other      );
    static constexpr uint64_t content_library_folder = (uint64_t{1} << index_content_library_folder);
    static constexpr uint64_t content_library_node   = (uint64_t{1} << index_content_library_node  );
    static constexpr uint64_t physics                = (uint64_t{1} << index_physics               );
    static constexpr uint64_t raytrace               = (uint64_t{1} << index_raytrace              );
    static constexpr uint64_t node_attachment        = (uint64_t{1} << index_node_attachment       );
    static constexpr uint64_t brush_placement        = (uint64_t{1} << index_brush_placement       );
    static constexpr uint64_t render_style           = (uint64_t{1} << index_render_style          );
    static constexpr uint64_t graph                  = (uint64_t{1} << index_graph                 );
    static constexpr uint64_t graph_node             = (uint64_t{1} << index_graph_node            );
    static constexpr uint64_t graph_link             = (uint64_t{1} << index_graph_link            );
    static constexpr uint64_t rendergraph_node       = (uint64_t{1} << index_rendergraph_node      );
    static constexpr uint64_t layout                 = (uint64_t{1} << index_layout                );
    static constexpr uint64_t layout_item            = (uint64_t{1} << index_layout_item           );
    static constexpr uint64_t physics_material       = (uint64_t{1} << index_physics_material      );
    static constexpr uint64_t collision_filter       = (uint64_t{1} << index_collision_filter      );
    static constexpr uint64_t physics_joint_settings = (uint64_t{1} << index_physics_joint_settings);
    static constexpr uint64_t asset_file_scene       = (uint64_t{1} << index_asset_file_scene      );
    static constexpr uint64_t graph_texture          = (uint64_t{1} << index_graph_texture         );
    static constexpr uint64_t graph_mesh             = (uint64_t{1} << index_graph_mesh            );
    static constexpr uint64_t geometry_graph_mesh    = (uint64_t{1} << index_geometry_graph_mesh   );
    static constexpr uint64_t prefab_instance        = (uint64_t{1} << index_prefab_instance       );
    static constexpr uint64_t asset_file_texture     = (uint64_t{1} << index_asset_file_texture    );
    static constexpr uint64_t style                  = (uint64_t{1} << index_style                 );
    static constexpr uint64_t asset_file_usd         = (uint64_t{1} << index_asset_file_usd        );
    static constexpr uint64_t typed                  = (uint64_t{1} << index_typed                 );
    static constexpr uint64_t scope                  = (uint64_t{1} << index_scope                 );
    static constexpr uint64_t imageable              = (uint64_t{1} << index_imageable             );
    static constexpr uint64_t xform                  = (uint64_t{1} << index_xform                 );
    static constexpr uint64_t boundable              = (uint64_t{1} << index_boundable             );
    static constexpr uint64_t gprim                  = (uint64_t{1} << index_gprim                 );

    // NOTE: The names here must match the C++ class names
    static constexpr const char* c_bit_labels[] = {
        "none",
        "Animation",
        "Animation_channel",
        "Animation_sampler",
        "Bone",
        "Brush",
        "Camera",
        "Composer",
        "Frame_controller",
        "Grid",
        "Light",
        "Light_layer",
        "Material",
        "Mesh",
        "Mesh_layer",
        "Composition_pass",
        "Rendertarget",
        "Scene",
        "Skin",
        "Texture",
        "Xformable",
        "Asset_folder",
        "Asset_file_gltf",
        "Asset_file_geogram",
        "Asset_file_other",
        "Content_library_folder",
        "Content_library_node",
        "Physics",
        "Raytrace",
        "Node_attachment",
        "Brush_placement",
        "Render_style",
        "Graph",
        "Graph_node",
        "Graph_link",
        "Rendergraph_node",
        "Layout",
        "Layout_item",
        "Physics_material",
        "Collision_filter",
        "Physics_joint_settings",
        "Asset_file_scene",
        "Graph_texture",
        "Graph_mesh",
        "Geometry_graph_mesh",
        "Prefab_instance",
        "Asset_file_texture",
        "Style",
        "Asset_file_usd",
        "Typed",
        "Scope",
        "Imageable",
        "Xform",
        "Boundable",
        "Gprim"
    };
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

// Monotonic counter incremented whenever item state that can affect item tree
// rows changes: hierarchy children, node attachments, item names, and
// non-transient flag bits (see Item_flags::transient). Consumers (the editor
// item tree) compare it across frames to detect when cached row lists are
// stale. Item mutations are main-thread only; the counter is intentionally a
// plain (non-atomic) integer.
[[nodiscard]] auto get_item_mutation_serial() -> uint64_t;
void bump_item_mutation_serial();

// https://herbsutter.com/2019/10/03/gotw-ish-solution-the-clonable-pattern/

enum class Item_kind : unsigned int {
    clone_using_copy_constructor = 0,
    clone_using_custom_clone_constructor,
    not_clonable
};
using for_clone = bool;

template <typename T>
class Clonable_base
{
public:
    virtual ~Clonable_base() = default;
    [[nodiscard]] virtual auto clone() const -> std::shared_ptr<T> {
        return std::make_shared<T>(static_cast<const T&>(*this));
    }
};

template <typename Base, typename Intermediate, typename Self, Item_kind kind = Item_kind::clone_using_copy_constructor>
class Item : public Intermediate
{
public:
    using Intermediate::Intermediate;
    auto clone() const -> std::shared_ptr<Base> override {
        if constexpr (kind == Item_kind::clone_using_copy_constructor) {
            return std::make_shared<Self>(static_cast<const Self&>(*this));
        } else if constexpr(kind == Item_kind::clone_using_custom_clone_constructor) {
            return std::make_shared<Self>(static_cast<const Self&>(*this), for_clone{});
        } else { // if constexpr(kind == Item_kind::not_clonable) {
            return std::shared_ptr<Base>{};
        }
    }
    auto get_type     () const -> uint64_t         override { return Self::get_static_type(); }
    auto get_type_name() const -> std::string_view override { return Self::static_type_name; }

    // Property owner type id of Self, allocated under Intermediate's id on
    // first use (erhe_property/owner_type.hpp).
    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type
    {
        static const erhe::property::Owner_type s_id = erhe::property::allocate_owner_type(
            Intermediate::property_owner_type(), Self::static_type_name
        );
        return s_id;
    }
    auto get_property_owner_type() const -> erhe::property::Owner_type override { return Self::property_owner_type(); }
};

class Item_base
    : public std::enable_shared_from_this<Item_base>
    , public Clonable_base<Item_base>
    , public erhe::property::Dependency_object
{
public:
    Item_base();

    explicit Item_base(std::string_view name);
    explicit Item_base(const Item_base& other);
    Item_base& operator=(const Item_base& other);
    ~Item_base() noexcept override;

    [[nodiscard]] virtual auto get_type     () const -> uint64_t         { return 0; };
    [[nodiscard]] virtual auto get_type_name() const -> std::string_view { return "Item_base"; };
    [[nodiscard]] virtual auto get_item_host() const -> Item_host*       { return m_item_host; }

    // Implements erhe::property::Dependency_object: the owner type id is
    // the item's class (Item<> overrides it per class; this is the id every
    // item class descends from), and an expression reference path (D22) is
    // this item (""), its inheritance parent ("..") or an item of the same
    // host by path or by name (Item_host::find_hosted_item).
    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;
    [[nodiscard]] auto get_property_owner_type () const -> erhe::property::Owner_type override { return property_owner_type(); }
    [[nodiscard]] auto resolve_expression_object(std::string_view path) const -> erhe::property::Dependency_object* override;
    // Object references (D28): the item name is the reference text, and
    // the owning pointer is shared_from_this (null for an item not owned by
    // a shared_ptr, which no reference can then hold). Hierarchy overrides
    // the text with the item's path (Hierarchy::get_path()).
    [[nodiscard]] auto get_reference_path  () const -> std::string override;
    [[nodiscard]] auto get_shared_reference() const -> std::shared_ptr<erhe::property::Dependency_object> override;

    // Property sub-objects (doc/property-system.md D29): Dependency_objects
    // this item owns by value that are not items themselves and that the
    // editor addresses as (item, index) - a Mesh's primitives. The index is
    // stable for the item's current list; a label names the sub-object in
    // the UI. The default is no sub-objects.
    [[nodiscard]] virtual auto get_property_sub_object_count() const -> std::size_t { return 0; }
    [[nodiscard]] virtual auto get_property_sub_object      (std::size_t index) -> erhe::property::Dependency_object* { static_cast<void>(index); return nullptr; }
    [[nodiscard]] virtual auto get_property_sub_object      (std::size_t index) const -> const erhe::property::Dependency_object* { static_cast<void>(index); return nullptr; }
    [[nodiscard]] virtual auto get_property_sub_object_label(std::size_t index) const -> std::string { static_cast<void>(index); return {}; }

    // For items whose host is tracked by an owning container (e.g. the
    // editor's content library): the container maintains this pointer on
    // add / remove / owner change. Types that derive their host from scene
    // structure (Node, Node_attachment, Scene) override get_item_host()
    // and do not use this member. Virtual so an item that owns further
    // items outside the container's view forwards the host to them (a
    // graph asset to its nodes).
    virtual void set_item_host(Item_host* item_host);

    // Inheritance container (doc/content-library-folders.md D1): the object
    // an item with no structural parent inherits property values from - the
    // content-library node wrapping it. Maintained by the container; null
    // outside one; not copied by copy / clone. Classes whose parent comes
    // from scene structure (Hierarchy, Node_attachment) override
    // get_inheritance_parent() themselves and never read it.
    void set_inheritance_container(erhe::property::Dependency_object* container);
    [[nodiscard]] auto get_inheritance_container() const -> erhe::property::Dependency_object*;
    [[nodiscard]] auto get_inheritance_parent   () const -> const erhe::property::Dependency_object* override;

    virtual void handle_flag_bits_update(const uint64_t old_flag_bits, const uint64_t new_flag_bits) {
        static_cast<void>(old_flag_bits);
        static_cast<void>(new_flag_bits);
    }

    [[nodiscard]] auto get_id                      () const -> std::size_t;
    [[nodiscard]] auto get_flag_bits               () const -> uint64_t;
    [[nodiscard]] auto is_no_transform_update      () const -> bool;
    [[nodiscard]] auto is_transform_world_normative() const -> bool;
    [[nodiscard]] auto is_selected                 () const -> bool;
    [[nodiscard]] auto is_hovered                  () const -> bool;
    [[nodiscard]] auto is_visible                  () const -> bool;
    [[nodiscard]] auto is_shown_in_ui              () const -> bool;
    [[nodiscard]] auto is_hidden                   () const -> bool;
    [[nodiscard]] auto is_lock_edit                 () const -> bool;
    [[nodiscard]] auto is_lock_viewport_selection   () const -> bool;
    [[nodiscard]] auto is_lock_viewport_transform   () const -> bool;
    [[nodiscard]] auto get_source_path             () const -> const std::filesystem::path*;
    [[nodiscard]] auto get_gltf_uid                () const -> const std::string&;
    [[nodiscard]] auto get_tags                    () const -> const std::set<std::string>&;
    [[nodiscard]] auto has_tag                     (std::string_view tag) const -> bool;
    [[nodiscard]] auto get_name                    () const -> const std::string&;
    [[nodiscard]] auto get_debug_label             () const -> erhe::utility::Debug_label;

    // Sibling-unique names (doc/usd-compatibility-plan.md M2): true when this
    // item may be renamed to `name`. An item that is not itself in a hierarchy
    // still shares one namespace when a content-library entry node wraps it
    // (the item's inheritance container, doc/content-library-folders.md D1):
    // the entry node's siblings are the item's. Every other item has no
    // namespace of its own, so every name is available. Hierarchy overrides
    // this with its own siblings.
    [[nodiscard]] virtual auto is_name_available(std::string_view name) const -> bool;
    [[nodiscard]] auto describe                    (int level = 0) const -> std::string;

    // Inherited flag (D23): the closest ancestor with a local value wins;
    // the effective value is mirrored into the flag word (Item_flags::derived)
    // so filters and readers stay bit tests. shadow_cast and lightmapped
    // are erhe::scene::Mesh properties mirrored the same way.
    static const erhe::property::Property<bool> visible_property;
    // USD purpose vocabulary (doc/usd-compatibility-plan.md M3): an
    // inherited enumeration whose default layer is derived from the
    // editor-only flag bits (D31), so an item that authors nothing reports
    // the purpose its flags imply and an authored value - local, from a
    // style, or inherited from an ancestor - overrides it.
    static const erhe::property::Property<Purpose> purpose_property;
    // The Purpose the flag bits imply: guide when any of
    // Item_flags::purpose_guide_when_set is set or show_in_ui is clear,
    // default_ otherwise. A pure function of the bits - no state, no
    // allocation.
    [[nodiscard]] static constexpr auto derive_purpose_from_flags(const uint64_t flag_bits) -> Purpose
    {
        const bool guide =
            ((flag_bits & Item_flags::purpose_guide_when_set) != 0u) ||
            ((flag_bits & Item_flags::purpose_guide_when_clear) == 0u);
        return guide ? Purpose::guide : Purpose::default_;
    }
    // The item's effective purpose. Non-allocating: an enumeration value
    // through the property layers, so an authored value wins and an
    // unauthored item answers from its own flag bits.
    [[nodiscard]] auto get_purpose() const -> Purpose;
    // The item's style source (doc/style-library.md D3): a bridged object
    // reference over Dependency_object::set_style / get_style, so the
    // Properties window shows a "Style" row with the picker; style_applies
    // is the rule the setter and the picker's candidate list share.
    static const erhe::property::Property<erhe::property::Object_reference> style_property;
    // Item-level authored state as bridged properties (D18), drawn by the
    // Properties window's registered path: the name, the tags (one
    // comma-separated string, see tags_to_string / tags_from_string) and
    // the persistent flag bits that are authored. lock_edit_property is
    // writable on a sealed item (Property_flags::writable_when_sealed) so
    // the seal can be lifted through it.
    static const erhe::property::Property<std::string> name_property;
    static const erhe::property::Property<std::string> tags_property;
    static const erhe::property::Property<bool> lock_viewport_transform_property;
    static const erhe::property::Property<bool> lock_edit_property;
    static const erhe::property::Property<bool> lock_viewport_selection_property;
    static const erhe::property::Property<bool> show_in_ui_property;
    static const erhe::property::Property<bool> show_debug_visualizations_property;
    static const erhe::property::Property<bool> exclude_from_prefab_property;
    static const erhe::property::Property<bool> no_message_property;
    static const erhe::property::Property<bool> no_transform_update_property;
    static const erhe::property::Property<bool> transform_world_normative_property;
    static const erhe::property::Property<bool> show_in_developer_ui_property;
    static const erhe::property::Property<bool> ik_lock_property;
    [[nodiscard]] static auto tags_to_string  (const std::set<std::string>& tags) -> std::string;
    static void               tags_from_string(std::string_view text, std::set<std::string>& out_tags);
    // True when `object` can use `source` as its style: the source's
    // secondary owner type is on the object's owner chain, or is the
    // object's own secondary owner type or a descendant of it. An editor
    // Style item names the root type and so applies to every object.
    [[nodiscard]] static auto style_applies(const erhe::property::Dependency_object& source, const erhe::property::Dependency_object& object) -> bool;

    // Rejects Item_flags::derived bits in mask (logged, dropped). A change
    // of Item_flags::lock_edit seals / unseals the property store (D24):
    // is_lock_edit() and is_sealed() agree.
    void set_flag_bits    (uint64_t mask, bool value);
    void enable_flag_bits (uint64_t mask);
    void disable_flag_bits(uint64_t mask);
    void set_name         (std::string_view name);
    void set_selected     (bool value);
    void set_visible      (bool value);
    void show             ();
    void hide             ();
    void set_source_path  (const std::filesystem::path& path);
    void set_gltf_uid     (std::string_view uid);
    void set_lock_edit    (bool value);
    void add_tag          (std::string_view tag);
    void remove_tag       (std::string_view tag);
    void clear_tags       ();
    void set_tags         (const std::set<std::string>& tags);

protected:
    // Writes one Item_flags::derived bit from its property's effective
    // value (the property changed callback of visible here, of
    // shadow_cast / lightmapped in Mesh).
    void        set_derived_flag_bit    (uint64_t bit, bool value);

private:
    static void on_flag_property_changed(erhe::property::Dependency_object& object, const erhe::property::Property_changed_args& args);
    void        rederive_flag_bits      ();
    void        sync_seal_with_lock_edit();

protected:
    // Not copied: a copy / clone starts outside any owning container.
    Item_host*                             m_item_host  {nullptr};
    erhe::property::Dependency_object*     m_inheritance_container{nullptr};
    Unique_id<Item_base>                   m_id         {};
    uint64_t                               m_flag_bits  {Item_flags::visible}; // derived bits start at the property defaults
    std::string                            m_name       {};
    erhe::utility::Debug_label             m_debug_label{};
    std::unique_ptr<std::filesystem::path> m_source_path{};
    // glTF 2.1 unique ID (KhronosGroup/glTF#2597): the item's persistent
    // file-scoped identity. Empty until assigned - at import when the
    // source file carries one, or generated exactly once at first export
    // (see erhe::gltf) - and never changed afterwards, so external
    // references to the item stay valid across re-saves. Not copied by
    // copy / clone: a clone is a new object and must not claim the
    // original's identity (uids are unique within a file).
    std::string                            m_gltf_uid   {};
    std::set<std::string>                  m_tags       {};
};

template <typename T>
auto is(const erhe::Item_base* const item) -> bool
{
    if (item == nullptr) {
        return false;
    }
    return (item->get_type() & T::get_static_type()) == T::get_static_type();
}

template <typename T>
auto is(const std::shared_ptr<erhe::Item_base>& item) -> bool
{
    return erhe::is<T>(item.get());
}

} // namespace erhe
