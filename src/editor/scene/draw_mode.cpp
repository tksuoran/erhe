#include "scene/draw_mode.hpp"
#include "scene/draw_mode_cards.hpp"
#include "scene/scene_root.hpp"

#include "erhe_math/math_util.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/instance_override.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

namespace editor {

using erhe::property::Asset_path;
using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Value_source;
using erhe::scene::Draw_mode_card_face;
using erhe::scene::Draw_mode_card_geometry;
using erhe::scene::Draw_mode_card_visibility;
using erhe::scene::Draw_mode_description;
using erhe::scene::c_draw_mode_card_face_count;

namespace {

constexpr std::string_view c_group  = "Draw Mode";
constexpr std::string_view c_cards  = "Cards";
constexpr std::string_view c_extent = "Extents Hint";

const erhe::property::Owner_type c_owner = Draw_mode::property_owner_type();

// Evaluated on Draw_mode objects only: the card rows are of use only while
// the prim asks for cards.
auto is_cards(const erhe::property::Dependency_object& object) -> bool
{
    return static_cast<const Draw_mode&>(object).resolved_draw_mode() == erhe::scene::Draw_mode::cards;
}

auto card_texture_ui(const std::string_view label) -> Property_ui
{
    return Property_ui{
        .group        = c_cards,
        .tooltip      = "The image of this card face; an empty path draws the face in the draw-mode color",
        .label        = label,
        .visible_when = is_cards
    };
}

} // anonymous namespace

const Property<erhe::scene::Draw_mode> Draw_mode::draw_mode_property = Property<erhe::scene::Draw_mode>::register_property(
    "draw_mode", c_owner, erhe::scene::c_draw_mode_enum_info,
    Property_metadata{
        .default_value = erhe::property::make_value(erhe::scene::Draw_mode::inherited),
        .ui            = Property_ui{.group = c_group, .tooltip = "How this prim's subtree is drawn; 'inherited' defers to the nearest ancestor asking for one", .label = "Draw Mode"}
    }
);
const Property<bool> Draw_mode::apply_draw_mode_property = Property<bool>::register_property(
    "apply_draw_mode", c_owner,
    Property_metadata{
        .default_value = false,
        .ui            = Property_ui{.group = c_group, .tooltip = "Carried for the file; the draw mode applies without it, the way the imaging adapter honours it on every model prim", .label = "Apply Draw Mode"}
    }
);
const Property<Draw_mode_card_geometry> Draw_mode::card_geometry_property = Property<Draw_mode_card_geometry>::register_property(
    "card_geometry", c_owner, erhe::scene::c_draw_mode_card_geometry_enum_info,
    Property_metadata{
        .default_value = erhe::property::make_value(Draw_mode_card_geometry::cross),
        .ui            = Property_ui{.group = c_cards, .label = "Card Geometry", .visible_when = is_cards}
    }
);
const Property<Draw_mode_card_visibility> Draw_mode::card_visibility_property = Property<Draw_mode_card_visibility>::register_property(
    "card_visibility", c_owner, erhe::scene::c_draw_mode_card_visibility_enum_info,
    Property_metadata{
        .default_value = erhe::property::make_value(Draw_mode_card_visibility::inherited),
        .ui            = Property_ui{.group = c_cards, .tooltip = "'simple' leaves out the cards of the stage's up axis", .label = "Card Visibility", .visible_when = is_cards}
    }
);
const Property<Asset_path> Draw_mode::card_texture_x_neg_property = Property<Asset_path>::register_property(
    "card_texture_x_neg", c_owner, Property_metadata{.default_value = Asset_path{}, .ui = card_texture_ui("Card Texture X-")}
);
const Property<Asset_path> Draw_mode::card_texture_x_pos_property = Property<Asset_path>::register_property(
    "card_texture_x_pos", c_owner, Property_metadata{.default_value = Asset_path{}, .ui = card_texture_ui("Card Texture X+")}
);
const Property<Asset_path> Draw_mode::card_texture_y_neg_property = Property<Asset_path>::register_property(
    "card_texture_y_neg", c_owner, Property_metadata{.default_value = Asset_path{}, .ui = card_texture_ui("Card Texture Y-")}
);
const Property<Asset_path> Draw_mode::card_texture_y_pos_property = Property<Asset_path>::register_property(
    "card_texture_y_pos", c_owner, Property_metadata{.default_value = Asset_path{}, .ui = card_texture_ui("Card Texture Y+")}
);
const Property<Asset_path> Draw_mode::card_texture_z_neg_property = Property<Asset_path>::register_property(
    "card_texture_z_neg", c_owner, Property_metadata{.default_value = Asset_path{}, .ui = card_texture_ui("Card Texture Z-")}
);
const Property<Asset_path> Draw_mode::card_texture_z_pos_property = Property<Asset_path>::register_property(
    "card_texture_z_pos", c_owner, Property_metadata{.default_value = Asset_path{}, .ui = card_texture_ui("Card Texture Z+")}
);
const Property<glm::vec3> Draw_mode::draw_mode_color_property = Property<glm::vec3>::register_property(
    "draw_mode_color", c_owner,
    Property_metadata{
        .default_value = glm::vec3{0.18f, 0.18f, 0.18f},
        .ui            = Property_ui{.presentation = Property_ui::Presentation::color, .group = c_group, .tooltip = "The line color of 'origin' and 'bounds', and the fallback card color", .label = "Draw Mode Color"}
    }
);
const Property<glm::vec3> Draw_mode::extents_hint_min_property = Property<glm::vec3>::register_property(
    "extents_hint_min", c_owner,
    Property_metadata{
        .default_value = glm::vec3{0.0f},
        .ui            = Property_ui{.step = 0.01f, .group = c_extent, .tooltip = "The model bounds the proxies are sized from, in this prim's space; without one the bounds of the meshes below are used", .label = "Minimum"}
    }
);
const Property<glm::vec3> Draw_mode::extents_hint_max_property = Property<glm::vec3>::register_property(
    "extents_hint_max", c_owner,
    Property_metadata{
        .default_value = glm::vec3{0.0f},
        .ui            = Property_ui{.step = 0.01f, .group = c_extent, .label = "Maximum"}
    }
);

auto Draw_mode::get_card_texture_property(const Draw_mode_card_face face) -> const Property<Asset_path>&
{
    switch (face) {
        case Draw_mode_card_face::x_neg: return card_texture_x_neg_property;
        case Draw_mode_card_face::x_pos: return card_texture_x_pos_property;
        case Draw_mode_card_face::y_neg: return card_texture_y_neg_property;
        case Draw_mode_card_face::y_pos: return card_texture_y_pos_property;
        case Draw_mode_card_face::z_neg: return card_texture_z_neg_property;
        case Draw_mode_card_face::z_pos: return card_texture_z_pos_property;
        default                        : return card_texture_x_neg_property;
    }
}

Draw_mode::Draw_mode(App_context& context)
    : m_context{context}
{
    set_name("Draw Mode");
    enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
}

Draw_mode::Draw_mode(const Draw_mode& src, erhe::for_clone)
    : Item{src, erhe::for_clone{}} // the property entries copy with the base (D10)
    , m_context{src.m_context}
    , m_source_directory{src.m_source_directory}
{
}

Draw_mode::~Draw_mode() noexcept
{
    remove_card_proxy();
}

void Draw_mode::handle_node_update(erhe::scene::Node* const old_node, erhe::scene::Node* const new_node)
{
    // The pruning is a state of the node the proxy stands for, so it leaves
    // the node the attachment leaves and reaches the one it arrives at.
    if (old_node != nullptr) {
        old_node->set_prunes_children(false);
    }
    Node_attachment::handle_node_update(old_node, new_node);
    m_extent_known = false;
    apply_pruning();
    queue_card_proxy_rebuild();
}

void Draw_mode::handle_item_host_update(erhe::Item_host* const old_item_host, erhe::Item_host* const new_item_host)
{
    Node_attachment::handle_item_host_update(old_item_host, new_item_host);
    // The old host may hold the last reference; keep this alive across the move.
    const std::shared_ptr<Draw_mode> shared_this = std::static_pointer_cast<Draw_mode>(shared_from_this());
    if (old_item_host != nullptr) {
        static_cast<Scene_root*>(old_item_host)->unregister_draw_mode(shared_this);
    }
    if (new_item_host != nullptr) {
        static_cast<Scene_root*>(new_item_host)->register_draw_mode(shared_this);
    }
    // The transforms the computed extent was measured from are the host's, so
    // a move to another host measures again.
    m_extent_known = false;
    apply_pruning();
    // Queued with the host that is still registered, so that a detach is
    // answered too: at flush time the attachment's own host says whether the
    // proxy is rebuilt somewhere else or only taken out.
    Scene_root* const rebuild_host = static_cast<Scene_root*>((old_item_host != nullptr) ? old_item_host : new_item_host);
    if (rebuild_host != nullptr) {
        rebuild_host->queue_draw_mode_proxy_rebuild(shared_this);
    } else {
        remove_card_proxy();
    }
}

void Draw_mode::on_property_changed(const erhe::property::Property_changed_args& args)
{
    if (!erhe::property::is_owner_type_or_descendant(c_owner, args.property.get_owner_type())) {
        return;
    }
    const erhe::property::Dependency_property* const changed = &args.property;
    if (changed == draw_mode_property.get_ptr()) {
        apply_pruning();
    } else if (
        (changed == extents_hint_min_property.get_ptr()) ||
        (changed == extents_hint_max_property.get_ptr())
    ) {
        m_extent_known = false;
    }
    // Every value of the attachment reaches the cards - the mode and the
    // visibility say which faces exist, the geometry and the extent where
    // they are, the textures and the color what they show - so any of them
    // rebuilds the proxy, and only a change does.
    queue_card_proxy_rebuild();
}

auto Draw_mode::prunes_own_children() const -> bool
{
    const erhe::scene::Draw_mode mode = get_value(draw_mode_property);
    return (mode != erhe::scene::Draw_mode::inherited) && (mode != erhe::scene::Draw_mode::default_);
}

void Draw_mode::apply_pruning()
{
    erhe::scene::Node* const node = get_node();
    if (node == nullptr) {
        return;
    }
    node->set_prunes_children(prunes_own_children());
}

auto Draw_mode::resolved_draw_mode() const -> erhe::scene::Draw_mode
{
    const erhe::scene::Draw_mode own = get_value(draw_mode_property);
    if (own != erhe::scene::Draw_mode::inherited) {
        return own;
    }
    const erhe::scene::Node* const node = get_node();
    std::shared_ptr<erhe::scene::Xformable> ancestor = (node != nullptr)
        ? node->get_parent_node()
        : std::shared_ptr<erhe::scene::Xformable>{};
    while (ancestor) {
        const std::shared_ptr<Draw_mode> draw_mode = erhe::scene::get_attachment<Draw_mode>(ancestor.get());
        if (draw_mode) {
            const erhe::scene::Draw_mode mode = draw_mode->get_value(draw_mode_property);
            if (mode != erhe::scene::Draw_mode::inherited) {
                return mode;
            }
        }
        ancestor = ancestor->get_parent_node();
    }
    return erhe::scene::Draw_mode::default_;
}

auto Draw_mode::resolved_card_visibility() const -> Draw_mode_card_visibility
{
    const Draw_mode_card_visibility own = get_value(card_visibility_property);
    if (own != Draw_mode_card_visibility::inherited) {
        return own;
    }
    const erhe::scene::Node* const node = get_node();
    std::shared_ptr<erhe::scene::Xformable> ancestor = (node != nullptr)
        ? node->get_parent_node()
        : std::shared_ptr<erhe::scene::Xformable>{};
    while (ancestor) {
        const std::shared_ptr<Draw_mode> draw_mode = erhe::scene::get_attachment<Draw_mode>(ancestor.get());
        if (draw_mode) {
            const Draw_mode_card_visibility visibility = draw_mode->get_value(card_visibility_property);
            if (visibility != Draw_mode_card_visibility::inherited) {
                return visibility;
            }
        }
        ancestor = ancestor->get_parent_node();
    }
    return Draw_mode_card_visibility::full;
}

void Draw_mode::invalidate_extent()
{
    m_extent_known = false;
    queue_card_proxy_rebuild();
}

auto Draw_mode::get_card_proxy() const -> const std::shared_ptr<erhe::scene::Mesh>&
{
    return m_card_proxy;
}

void Draw_mode::remove_card_proxy()
{
    if (!m_card_proxy) {
        return;
    }
    m_card_proxy->set_parent(std::shared_ptr<erhe::Hierarchy>{});
    m_card_proxy.reset();
}

void Draw_mode::queue_card_proxy_rebuild()
{
    erhe::Item_host* const item_host = get_item_host();
    if (item_host == nullptr) {
        // Out of every scene: the proxy leaves with the attachment, and there
        // is no tick that would take it out later.
        remove_card_proxy();
        return;
    }
    static_cast<Scene_root*>(item_host)->queue_draw_mode_proxy_rebuild(
        std::static_pointer_cast<Draw_mode>(shared_from_this())
    );
}

void Draw_mode::rebuild_card_proxy()
{
    remove_card_proxy();
    erhe::scene::Node* const node = get_node();
    if ((node == nullptr) || (get_item_host() == nullptr)) {
        return;
    }
    m_card_proxy = build_draw_mode_card_proxy(m_context, *this);
    if (m_card_proxy) {
        erhe::scene::set_mesh_parent(m_card_proxy, node->shared_node_from_this());
    }
}

auto Draw_mode::get_extent(glm::vec3& out_min, glm::vec3& out_max) const -> bool
{
    if (!m_extent_known) {
        m_extent_known = true;
        m_extent_valid = false;
        const bool has_hint =
            (get_value_source(extents_hint_min_property) != Value_source::default_value) ||
            (get_value_source(extents_hint_max_property) != Value_source::default_value);
        if (has_hint) {
            m_extent_min   = get_value(extents_hint_min_property);
            m_extent_max   = get_value(extents_hint_max_property);
            m_extent_valid = glm::all(glm::lessThanEqual(m_extent_min, m_extent_max));
        } else {
            const erhe::scene::Node* const node = get_node();
            if (node != nullptr) {
                // The meshes at and below the prim, in the prim's own space:
                // the same box `extentsHint` would state. Measured once, not
                // per frame; invalidate_extent() asks for it again.
                const glm::mat4  node_from_world = node->node_from_world();
                erhe::math::Aabb bounds{};
                const_cast<erhe::scene::Node*>(node)->for_each<erhe::scene::Mesh>(
                    [&bounds, &node_from_world](erhe::scene::Mesh& mesh) -> bool {
                        // The proxy is what the extent produced; measuring it
                        // back would make the box feed itself.
                        if ((mesh.get_flag_bits() & erhe::Item_flags::draw_mode_proxy) != 0) {
                            return true;
                        }
                        const glm::mat4 node_from_mesh = node_from_world * mesh.world_from_node();
                        for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh.get_primitives()) {
                            if (!mesh_primitive.primitive) {
                                continue;
                            }
                            const erhe::math::Aabb primitive_bounds = mesh_primitive.primitive->get_bounding_box();
                            if (!primitive_bounds.is_valid()) {
                                continue;
                            }
                            bounds.include(primitive_bounds.transformed_by(node_from_mesh));
                        }
                        return true;
                    }
                );
                if (bounds.is_valid()) {
                    m_extent_min   = bounds.min;
                    m_extent_max   = bounds.max;
                    m_extent_valid = true;
                }
            }
        }
    }
    if (!m_extent_valid) {
        return false;
    }
    out_min = m_extent_min;
    out_max = m_extent_max;
    return true;
}

auto Draw_mode::get_description() const -> Draw_mode_description
{
    Draw_mode_description description{};
    description.draw_mode                = get_value(draw_mode_property);
    description.draw_mode_authored       = (get_value_source(draw_mode_property)       == Value_source::local);
    description.apply_draw_mode          = get_value(apply_draw_mode_property);
    description.apply_draw_mode_authored = (get_value_source(apply_draw_mode_property) == Value_source::local);
    description.card_geometry            = get_value(card_geometry_property);
    description.card_geometry_authored   = (get_value_source(card_geometry_property)   == Value_source::local);
    description.card_visibility          = get_value(card_visibility_property);
    description.card_visibility_authored = (get_value_source(card_visibility_property) == Value_source::local);
    for (std::size_t i = 0; i < c_draw_mode_card_face_count; ++i) {
        const Draw_mode_card_face face = static_cast<Draw_mode_card_face>(i);
        // A card texture is stated the way every other value is (D32): only
        // what this attachment holds locally is what the prim authored. One
        // the reference layer supplies is the template's opinion, written
        // where the template writes it. The path is stated resolved, so the
        // writer names the image relative to the file it writes whatever
        // spelling the value arrived in.
        if (get_value_source(get_card_texture_property(face)) != Value_source::local) {
            continue;
        }
        description.card_textures[i] = resolve_card_texture_path(face).generic_string();
    }
    description.draw_mode_color          = get_value(draw_mode_color_property);
    description.draw_mode_color_authored = (get_value_source(draw_mode_color_property) == Value_source::local);
    description.extents_hint_min         = get_value(extents_hint_min_property);
    description.extents_hint_max         = get_value(extents_hint_max_property);
    description.extents_hint_authored    =
        (get_value_source(extents_hint_min_property) == Value_source::local) ||
        (get_value_source(extents_hint_max_property) == Value_source::local);
    return description;
}

void Draw_mode::set_description(const Draw_mode_description& description)
{
    if (description.draw_mode_authored) {
        set_value(draw_mode_property, description.draw_mode);
    }
    if (description.apply_draw_mode_authored) {
        set_value(apply_draw_mode_property, description.apply_draw_mode);
    }
    if (description.card_geometry_authored) {
        set_value(card_geometry_property, description.card_geometry);
    }
    if (description.card_visibility_authored) {
        set_value(card_visibility_property, description.card_visibility);
    }
    for (std::size_t i = 0; i < c_draw_mode_card_face_count; ++i) {
        if (description.card_textures[i].empty()) {
            continue;
        }
        const Draw_mode_card_face face = static_cast<Draw_mode_card_face>(i);
        set_value(get_card_texture_property(face), Asset_path{description.card_textures[i]});
    }
    if (description.draw_mode_color_authored) {
        set_value(draw_mode_color_property, description.draw_mode_color);
    }
    if (description.extents_hint_authored) {
        set_value(extents_hint_min_property, description.extents_hint_min);
        set_value(extents_hint_max_property, description.extents_hint_max);
    }
    m_extent_known = false;
    apply_pruning();
    queue_card_proxy_rebuild();
}

void Draw_mode::set_source_directory(const std::filesystem::path& directory)
{
    m_source_directory = directory;
}

auto Draw_mode::get_source_directory() const -> const std::filesystem::path&
{
    return m_source_directory;
}

auto Draw_mode::resolve_card_texture_path(const Draw_mode_card_face face) const -> std::filesystem::path
{
    const erhe::property::Property<Asset_path>& property = get_card_texture_property(face);
    const Asset_path                            value    = get_value(property);
    if (value.path.empty()) {
        return std::filesystem::path{};
    }
    std::filesystem::path path{value.path};
    if (!path.is_relative()) {
        return path;
    }
    // The directory of the file that authored the value: the attachment's own
    // when the value is its own, and otherwise the one of the attachment the
    // reference layer reads it from, which is the template a file's variant
    // block authored it in.
    const Draw_mode* supplier = this;
    while (supplier->get_value_source(property) == Value_source::reference) {
        const std::shared_ptr<const erhe::property::Dependency_object> counterpart = supplier->get_reference();
        const Draw_mode* const next = dynamic_cast<const Draw_mode*>(counterpart.get());
        if (next == nullptr) {
            break;
        }
        supplier = next;
    }
    const std::filesystem::path& directory = supplier->get_source_directory();
    if (directory.empty()) {
        return path;
    }
    return (directory / path).lexically_normal();
}

void register_draw_mode_applied_schema(App_context& context)
{
    // `prepend apiSchemas = ["GeomModelAPI"]` in a variant block is what makes
    // the schema present on the prim, so an opinion of one of its attributes
    // arriving at a prim with no attachment makes the attachment.
    erhe::scene::register_applied_schema_attachment(
        Draw_mode::static_type_name,
        Draw_mode::property_owner_type(),
        [&context]() -> std::shared_ptr<erhe::scene::Node_attachment> {
            return std::make_shared<Draw_mode>(context);
        }
    );
}

} // namespace editor
