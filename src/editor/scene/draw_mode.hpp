#pragma once

#include "erhe_scene/draw_mode_description.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_property/dependency_property.hpp"

#include <glm/glm.hpp>

#include <array>
#include <filesystem>
#include <memory>

namespace erhe { class Item_host; }
namespace erhe::scene { class Mesh; }

namespace editor {

class App_context;

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
//   - the proxy in their place: Draw_mode_renderer submits the lines of
//     `bounds` and `origin` per viewport from get_extent() and the draw-mode
//     color, and a `cards` mode owns generated quad geometry - a Mesh child
//     prim of the model prim, flagged Item_flags::draw_mode_proxy (so the
//     pruning does not reach it) and Item_flags::session_only (so no exporter
//     writes it), rebuilt whenever a card property or the extent changes.
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
    explicit Draw_mode(App_context& context);
    Draw_mode(const Draw_mode& src, erhe::for_clone);
    ~Draw_mode() noexcept override;

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

    // Overrides Item_base: an inactive prim draws nothing, so it owns no card
    // proxy either; the proxy is built when the derived Item_flags::active
    // bit comes back.
    void handle_flag_bits_update(uint64_t old_flag_bits, uint64_t new_flag_bits) override;

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

    // The directory a relative card-texture path is resolved against: the
    // directory of the file that authored the value. A card texture a
    // `Usd_draw_mode` record carries is already absolute (the reader resolves
    // it), while one a variant block authors travels as the text the file
    // spelled - which is what a save writes back, so it stays relative here
    // and is resolved at the moment the image is read. Set by the importer,
    // once, for the file the attachment came out of; a clone keeps its
    // template's.
    void               set_source_directory(const std::filesystem::path& directory);
    [[nodiscard]] auto get_source_directory() const -> const std::filesystem::path&;

    // The image of one card face as a path that can be opened: the value as
    // it stands when it is absolute, and otherwise resolved against the
    // source directory of the attachment that supplies the value - which for
    // an instance is the template's attachment, reached through the reference
    // layer. Empty when the face names no image.
    [[nodiscard]] auto resolve_card_texture_path(erhe::scene::Draw_mode_card_face face) const -> std::filesystem::path;

    // The card proxy this attachment owns, null unless the resolved mode is
    // `cards` and the attachment is in a scene. It is not a prim the file
    // says anything about: a save writes the attributes, never the proxy.
    [[nodiscard]] auto get_card_proxy() const -> const std::shared_ptr<erhe::scene::Mesh>&;

    // Builds the card proxy the current values ask for and puts it under the
    // node, taking the previous one out. An inactive prim - one its own
    // opinion, or the pruning of an ancestor's draw mode, took out of render,
    // pick and simulation - builds none: it is drawn by nothing, so a proxy
    // of it would be geometry, materials and textures nobody sees. Main
    // thread only, and never from a change site: the build inserts a prim, so
    // the change sites queue the attachment with their Scene_root and
    // App_scenes::rebuild_draw_mode_proxies() is what calls this.
    void rebuild_card_proxy();

private:
    // Writes Item_base::set_prunes_children on the node from the resolved
    // mode. Called at every change site of the mode and at attach / detach.
    void apply_pruning();
    // The mode's own value as a pruning opinion: only a prim that authors a
    // mode of its own prunes. A prim whose own value is `inherited` and whose
    // resolved mode is a proxy mode is already inside a pruned subtree, so
    // pruning it again changes nothing.
    [[nodiscard]] auto prunes_own_children() const -> bool;

    // Asks this attachment's scene root for a rebuild on the next tick. The
    // call sites are the value changes, the attach / detach and
    // set_description().
    void queue_card_proxy_rebuild();
    void remove_card_proxy      ();

    App_context&                       m_context;
    std::shared_ptr<erhe::scene::Mesh> m_card_proxy{};
    std::filesystem::path              m_source_directory{};

    mutable glm::vec3 m_extent_min  {0.0f};
    mutable glm::vec3 m_extent_max  {0.0f};
    mutable bool      m_extent_valid{false};
    mutable bool      m_extent_known{false};
};

// How a `Draw_mode` is made for a prim a file applies `GeomModelAPI` to
// without erhe having made the attachment yet
// (erhe::scene::register_applied_schema_attachment). Called once from
// startup, while the process is still single threaded.
void register_draw_mode_applied_schema(App_context& context);

} // namespace editor
