#pragma once

#include "erhe_item/item.hpp"
#include "erhe_item/item_flags.hpp"
#include "erhe_item/item_type.hpp"
#include "erhe_property/dependency_property.hpp"

#include <cstdint>

namespace editor {

// The editor's Item_flags bits, in the application range erhe::Item_flags
// reserves (doc/erhe/item.md "Application bits"). The library never names
// one of them: what it needs to know - labels, the names the glTF ERHE_*
// extensions serialize the persistent ones by, which are transient and
// which imply Purpose::guide - register_editor_item_bits() registers at
// startup, before any item exists.
class Editor_item_flags
{
public:
    // Editor-only content kinds, each one separating one render pass (the
    // tool pass, the brush pass, the rendertarget overlay pass); any of them
    // makes an item's default purpose `guide`.
    static constexpr uint64_t tool                      = erhe::Item_flags::application_bit( 0);
    static constexpr uint64_t brush                     = erhe::Item_flags::application_bit( 1);
    static constexpr uint64_t controller                = erhe::Item_flags::application_bit( 2);
    static constexpr uint64_t rendertarget              = erhe::Item_flags::application_bit( 3);
    // Item tree row expanded by default.
    static constexpr uint64_t expand                    = erhe::Item_flags::application_bit( 4);
    static constexpr uint64_t show_in_developer_ui      = erhe::Item_flags::application_bit( 5);
    static constexpr uint64_t show_debug_visualizations = erhe::Item_flags::application_bit( 6);
    static constexpr uint64_t lock_viewport_selection   = erhe::Item_flags::application_bit( 7);
    static constexpr uint64_t lock_viewport_transform   = erhe::Item_flags::application_bit( 8);
    static constexpr uint64_t invisible_parent          = erhe::Item_flags::application_bit( 9);
    static constexpr uint64_t render_wireframe          = erhe::Item_flags::application_bit(10); // TODO
    static constexpr uint64_t render_bounding_volume    = erhe::Item_flags::application_bit(11); // TODO
    // Transient hover state (Hover_tool, the item trees).
    static constexpr uint64_t hovered_in_viewport       = erhe::Item_flags::application_bit(12);
    static constexpr uint64_t hovered_in_item_tree      = erhe::Item_flags::application_bit(13);
    // Transient companion of hovered_in_viewport, maintained by Hover_tool on
    // every ancestor of the viewport-hovered node (and refreshed when the
    // scene tree structure changes). Lets item trees highlight the closest
    // visible ancestor of a hovered node folded out of view with a plain
    // per-row flag test instead of walking the hierarchy.
    static constexpr uint64_t descendant_hovered_in_viewport = erhe::Item_flags::application_bit(14);
    // Graph-editor hover (maintained by the geometry graph window): the scene
    // node referenced by the graph node under the mouse on the node-editor
    // canvas. Exactly zero or one node carries hovered_in_graph at a time;
    // when it changes, every ancestor gets child_hovered_in_graph and every
    // descendant gets ancestor_hovered_in_graph (all three cleared and
    // re-derived together, and refreshed when the scene tree structure
    // changes). Lets item trees highlight graph hovering with plain per-row
    // flag tests, like viewport hovering.
    static constexpr uint64_t hovered_in_graph          = erhe::Item_flags::application_bit(15);
    static constexpr uint64_t child_hovered_in_graph    = erhe::Item_flags::application_bit(16);
    static constexpr uint64_t ancestor_hovered_in_graph = erhe::Item_flags::application_bit(17);
    // Transient, set by the shadow frustum fit debug visualization: this
    // shadow caster's world bounds intersect the selected light's shadow
    // caster volume (F_shadow), i.e. it can contribute to that light's shadow
    // map. Recomputed each frame the visualization runs; not authored or
    // serialized (like selected / hovered_*).
    static constexpr uint64_t affects_shadow            = erhe::Item_flags::application_bit(18);
    // Editor-generated pick/display proxy for a bone: a Mesh in the scene's bone
    // layer, parented under the joint node it represents. Content-adjacent but
    // not content - excluded from the item tree, save, export and prefabs, and
    // never selectable as itself (picking it resolves to the joint Node).
    static constexpr uint64_t bone_proxy                = erhe::Item_flags::application_bit(19);
    // The one item of the editor-wide selection that is the reference item
    // for commands and the one the UI highlights (doc/editor/active_item.md).
    // Written only by editor::Selection; at most one item carries it, and it
    // is independent of the selected bit - an item can be active while
    // unselected. Transient session state, never serialized.
    static constexpr uint64_t active_item               = erhe::Item_flags::application_bit(20);
    // The item is anchored to one scene view and drawn only in that view
    // (the editor hotbar quad follows the hovered view's camera). The mesh
    // keeps one visible state for the whole frame; the render pass that
    // draws view anchored items decides per view whether it runs. Session
    // state of editor furniture, never serialized.
    static constexpr uint64_t view_anchored             = erhe::Item_flags::application_bit(21);

    // The bits register_editor_item_bits() reports as transient
    // (erhe::Item_flags::get_transient_bits()) and as purpose inputs
    // (erhe::Item_flags::get_purpose_guide_when_set_bits()).
    static constexpr uint64_t transient =
        hovered_in_viewport | hovered_in_item_tree | descendant_hovered_in_viewport |
        hovered_in_graph | child_hovered_in_graph | ancestor_hovered_in_graph |
        affects_shadow | active_item;
    static constexpr uint64_t purpose_guide_when_set = tool | brush | controller | rendertarget;
    // Hover bits the renderers color an entry as hovered by
    // (erhe::scene_renderer::Primitive_interface_settings::hovered_flag_bits).
    static constexpr uint64_t hovered = hovered_in_viewport | hovered_in_item_tree;
};

// The editor's Item_type indices, in the application range
// erhe::Item_type reserves (the low one: an editor class is more specific
// than the library levels it derives from, and the icon of the lowest set
// type bit wins). Labels are the C++ class names; the item trees name
// their drag and drop payloads by them.
class Editor_item_types
{
public:
    static constexpr uint64_t index_brush                  = erhe::Item_type::application_index( 0);
    static constexpr uint64_t index_composer               = erhe::Item_type::application_index( 1);
    static constexpr uint64_t index_grid                   = erhe::Item_type::application_index( 2);
    static constexpr uint64_t index_composition_pass       = erhe::Item_type::application_index( 3);
    static constexpr uint64_t index_rendertarget           = erhe::Item_type::application_index( 4);
    static constexpr uint64_t index_asset_folder           = erhe::Item_type::application_index( 5);
    static constexpr uint64_t index_asset_file_gltf        = erhe::Item_type::application_index( 6);
    static constexpr uint64_t index_asset_file_geogram     = erhe::Item_type::application_index( 7);
    static constexpr uint64_t index_asset_file_other       = erhe::Item_type::application_index( 8);
    static constexpr uint64_t index_content_library_folder = erhe::Item_type::application_index( 9);
    static constexpr uint64_t index_content_library_node   = erhe::Item_type::application_index(10);
    // The `editor::Joint` prim (doc/erhe/property_system.md section 4.17). The
    // exporters leave a joint prim out of the prims they write
    // (Gltf_export_arguments::excluded_item_type_bits): a joint is written
    // as the physics joint of the body it joins, from the physics
    // description.
    static constexpr uint64_t index_joint                  = erhe::Item_type::application_index(11);
    static constexpr uint64_t index_raytrace               = erhe::Item_type::application_index(12);
    static constexpr uint64_t index_render_style           = erhe::Item_type::application_index(13);
    static constexpr uint64_t index_graph                  = erhe::Item_type::application_index(14);
    static constexpr uint64_t index_graph_link             = erhe::Item_type::application_index(15);
    static constexpr uint64_t index_asset_file_scene       = erhe::Item_type::application_index(16);
    static constexpr uint64_t index_graph_texture          = erhe::Item_type::application_index(17);
    static constexpr uint64_t index_graph_mesh             = erhe::Item_type::application_index(18);
    static constexpr uint64_t index_asset_file_texture     = erhe::Item_type::application_index(19);
    static constexpr uint64_t index_style                  = erhe::Item_type::application_index(20);
    static constexpr uint64_t index_asset_file_usd         = erhe::Item_type::application_index(21);

    static constexpr uint64_t brush                  = (uint64_t{1} << index_brush                 );
    static constexpr uint64_t composer               = (uint64_t{1} << index_composer              );
    static constexpr uint64_t grid                   = (uint64_t{1} << index_grid                  );
    static constexpr uint64_t composition_pass       = (uint64_t{1} << index_composition_pass      );
    static constexpr uint64_t rendertarget           = (uint64_t{1} << index_rendertarget          );
    static constexpr uint64_t asset_folder           = (uint64_t{1} << index_asset_folder          );
    static constexpr uint64_t asset_file_gltf        = (uint64_t{1} << index_asset_file_gltf       );
    static constexpr uint64_t asset_file_geogram     = (uint64_t{1} << index_asset_file_geogram    );
    static constexpr uint64_t asset_file_other       = (uint64_t{1} << index_asset_file_other      );
    static constexpr uint64_t content_library_folder = (uint64_t{1} << index_content_library_folder);
    static constexpr uint64_t content_library_node   = (uint64_t{1} << index_content_library_node  );
    static constexpr uint64_t joint                  = (uint64_t{1} << index_joint                 );
    static constexpr uint64_t raytrace               = (uint64_t{1} << index_raytrace              );
    static constexpr uint64_t render_style           = (uint64_t{1} << index_render_style          );
    static constexpr uint64_t graph                  = (uint64_t{1} << index_graph                 );
    static constexpr uint64_t graph_link             = (uint64_t{1} << index_graph_link            );
    static constexpr uint64_t asset_file_scene       = (uint64_t{1} << index_asset_file_scene      );
    static constexpr uint64_t graph_texture          = (uint64_t{1} << index_graph_texture         );
    static constexpr uint64_t graph_mesh             = (uint64_t{1} << index_graph_mesh            );
    static constexpr uint64_t asset_file_texture     = (uint64_t{1} << index_asset_file_texture    );
    static constexpr uint64_t style                  = (uint64_t{1} << index_style                 );
    static constexpr uint64_t asset_file_usd         = (uint64_t{1} << index_asset_file_usd        );
};

// The persistent editor flag bits that are authored as bridged boolean
// properties of every item (erhe::Item_base::register_flag_bit_property,
// doc/erhe/property_system.md D18): the Properties window's "Locks" rows and
// the developer-only visibility rows.
class Editor_item_properties
{
public:
    static const erhe::property::Property<bool> lock_viewport_transform_property;
    static const erhe::property::Property<bool> lock_viewport_selection_property;
    static const erhe::property::Property<bool> show_debug_visualizations_property;
    static const erhe::property::Property<bool> show_in_developer_ui_property;
};

// Whether the item carries any Editor_item_flags::hovered bit (the viewport
// or an item tree hovers it).
[[nodiscard]] inline auto is_hovered(const erhe::Item_base& item) -> bool
{
    return (item.get_flag_bits() & Editor_item_flags::hovered) != 0u;
}

// Registers the editor's flag bits and type indices with erhe::Item_flags /
// erhe::Item_type. Called once at startup (run_editor) and by the editor
// test executables, before any item is created.
void register_editor_item_bits();

} // namespace editor
