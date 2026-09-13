#pragma once

#include "erhe_scene/draw_mode_description.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_property/dependency_property.hpp"

#include <glm/glm.hpp>

#include <array>
#include <memory>

namespace erhe { class Item_host; }

namespace editor {

// `UsdGeomModelAPI` as an erhe attachment (doc/usd_compatibility.md, "Draw
// modes"): the request that a model prim's subtree be drawn as a proxy
// instead of by itself. The attachment holds every attribute of the schema as
// an entry property under the class name `Draw_mode`, which is the name a
// file's opinion of one addresses it by (`Draw_mode.card_geometry`,
// erhe::scene::find_override_property_target).
//
// The attachment owns two consequences of its own values:
//   - the prim's children leave render, pick and simulation while the
//     resolved mode is not `default_` (Item_base::set_prunes_children), the
//     way UsdImagingGLDrawModeAdapter prunes them;
//   - the proxy in their place, which Draw_mode_renderer submits per viewport
//     from get_extent() and the draw-mode color.
//
// The mode's own value is the prim's opinion and does NOT inherit down the
// tree: `Draw_mode::inherited` is USD's own deferral token, resolved by
// resolved_draw_mode() walking the ancestors.
class Draw_mode
    : public erhe::Item<
        erhe::Item_base,
        erhe::scene::Node_attachment,
        Draw_mode,
        erhe::Item_kind::clone_using_custom_clone_constructor
    >
{
public:
    Draw_mode();
    Draw_mode(const Draw_mode& src, erhe::for_clone);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Draw_mode"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t
    {
        return erhe::Item_type::node_attachment | erhe::Item_type::draw_mode;
    }

    // Overrides Node_attachment: the pruning state is a state of the node, so
    // it follows the attachment from one node to the other.
    void handle_node_update     (erhe::scene::Node* old_node, erhe::scene::Node* new_node) override;
    void handle_item_host_update(erhe::Item_host* old_item_host, erhe::Item_host* new_item_host) override;

    // Implements Dependency_object: a change of the mode re-applies the
    // pruning, a change of the extents hint drops the cached extent.
    void on_property_changed(const erhe::property::Property_changed_args& args) override;

    // The `UsdGeomModelAPI` attributes, one property each, named exactly as
    // the mapping table names them. Entry-stored and NOT inheriting: each is
    // the prim's own opinion, and a local value is what the file authored
    // (D32), which is what a save writes back.
    static const erhe::property::Property<erhe::scene::Draw_mode>                 draw_mode_property;
    static const erhe::property::Property<bool>                                   apply_draw_mode_property;
    static const erhe::property::Property<erhe::scene::Draw_mode_card_geometry>   card_geometry_property;
    static const erhe::property::Property<erhe::scene::Draw_mode_card_visibility> card_visibility_property;
    static const erhe::property::Property<erhe::property::Asset_path>             card_texture_x_neg_property;
    static const erhe::property::Property<erhe::property::Asset_path>             card_texture_x_pos_property;
    static const erhe::property::Property<erhe::property::Asset_path>             card_texture_y_neg_property;
    static const erhe::property::Property<erhe::property::Asset_path>             card_texture_y_pos_property;
    static const erhe::property::Property<erhe::property::Asset_path>             card_texture_z_neg_property;
    static const erhe::property::Property<erhe::property::Asset_path>             card_texture_z_pos_property;
    static const erhe::property::Property<glm::vec3>                              draw_mode_color_property;
    static const erhe::property::Property<glm::vec3>                              extents_hint_min_property;
    static const erhe::property::Property<glm::vec3>                              extents_hint_max_property;

    // The card-texture property of one face, in the record's face order.
    [[nodiscard]] static auto get_card_texture_property(erhe::scene::Draw_mode_card_face face)
        -> const erhe::property::Property<erhe::property::Asset_path>&;

    // The record this attachment holds: every value, with the authored flag
    // of each set from whether the attachment holds it as a local value. This
    // is what a save writes.
    [[nodiscard]] auto get_description() const -> erhe::scene::Draw_mode_description;

    // Takes the record's values as local values, one per authored flag; the
    // unauthored ones are left unset, so a reload authors exactly what the
    // file spelled. Used by the importers.
    void set_description(const erhe::scene::Draw_mode_description& description);

    // The mode that applies to this prim: its own value when it is not
    // `inherited`, else the nearest ancestor prim's non-inherited one, else
    // `default_`. Never returns `inherited`.
    [[nodiscard]] auto resolved_draw_mode() const -> erhe::scene::Draw_mode;

    // The card visibility that applies, resolved the same way with the root
    // fallback `full`. Never returns `inherited`.
    [[nodiscard]] auto resolved_card_visibility() const -> erhe::scene::Draw_mode_card_visibility;

    // The box the proxies are sized from, in the prim's own space: the
    // authored `extentsHint` when there is one, else the bounds of the
    // meshes at and below the prim. Returns false when neither exists (a
    // prim with no hint and no geometry has no proxy to draw). The computed
    // form is cached; invalidate_extent() drops it.
    [[nodiscard]] auto get_extent(glm::vec3& out_min, glm::vec3& out_max) const -> bool;
    void invalidate_extent();

private:
    // Writes Item_base::set_prunes_children on the node from the resolved
    // mode. Called at every change site of the mode and at attach / detach.
    void apply_pruning();
    // The mode's own value as a pruning opinion: only a prim that authors a
    // mode of its own prunes. A prim whose own value is `inherited` and whose
    // resolved mode is a proxy mode is already inside a pruned subtree, so
    // pruning it again changes nothing.
    [[nodiscard]] auto prunes_own_children() const -> bool;

    mutable glm::vec3 m_extent_min  {0.0f};
    mutable glm::vec3 m_extent_max  {0.0f};
    mutable bool      m_extent_valid{false};
    mutable bool      m_extent_known{false};
};

} // namespace editor
