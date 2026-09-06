#include "content_library/content_library.hpp"
#include "content_library/style.hpp"

#include "assets/asset_manager.hpp"
#include "assets/asset_reference.hpp"
#include "editor_log.hpp"
#include "brushes/brush.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "texture_graph/graph_texture.hpp"

#include "erhe_graphics/texture.hpp"
#include "erhe_physics/collision_filter.hpp"
#include "erhe_physics/physics_joint_settings.hpp"
#include "erhe_physics/physics_material.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/skin.hpp"

#include <algorithm>
#include <array>

namespace editor {

namespace {

// The resource kinds the library indexes, one `erhe::Item_type` bit each.
constexpr std::array<uint64_t, 11> c_kind_type_bits{
    erhe::Item_type::material,
    erhe::Item_type::texture,
    erhe::Item_type::brush,
    erhe::Item_type::style,
    erhe::Item_type::physics_material,
    erhe::Item_type::collision_filter,
    erhe::Item_type::physics_joint_settings,
    erhe::Item_type::animation,
    erhe::Item_type::skin,
    erhe::Item_type::graph_mesh,
    erhe::Item_type::graph_texture
};

constexpr auto make_all_kind_bits() -> uint64_t
{
    uint64_t bits = 0;
    for (const uint64_t type_bit : c_kind_type_bits) {
        bits |= type_bit;
    }
    return bits;
}

constexpr uint64_t c_all_kind_bits = make_all_kind_bits();

} // anonymous namespace

auto Content_library::get_kind_type_bits() -> std::span<const uint64_t>
{
    return std::span<const uint64_t>{c_kind_type_bits};
}

auto Content_library::get_kind_type_bit_of_type(const uint64_t item_type) -> uint64_t
{
    const uint64_t kind = item_type & c_all_kind_bits;
    // Exactly one kind bit names a kind; a mask carrying several names none.
    return ((kind != 0) && ((kind & (kind - 1)) == 0)) ? kind : 0;
}

auto Content_library::get_kind_type_bit(const erhe::Item_base& item) -> uint64_t
{
    return get_kind_type_bit_of_type(item.get_type());
}

Content_library_node::Content_library_node(const Content_library_node& other)
    : Item               {other}
    , type_code          {other.type_code}
    , type_name          {other.type_name}
    , category_owner_type{other.category_owner_type}
    , item               {other.item ? other.item->clone() : std::shared_ptr<erhe::Item_base>{}}
{
}

Content_library_node& Content_library_node::operator=(const Content_library_node& other)
{
    // Mirrors the copy constructor: the wrapped item is cloned and the
    // usership is deliberately not copied (a clone is a new object; the
    // manager's attach hook books a fresh usership when the node enters an
    // armed library).
    if (this == &other) {
        return *this;
    }
    Item::operator=(other);
    type_code           = other.type_code;
    type_name           = other.type_name;
    category_owner_type = other.category_owner_type;
    item                = other.item ? other.item->clone() : std::shared_ptr<erhe::Item_base>{};
    asset_usership.reset();
    return *this;
}

Content_library_node::~Content_library_node() noexcept
{
    // The wrapped item can outlive its node (a mesh keeps its material, the
    // clipboard keeps a cut entry): drop the inheritance link before the
    // node's storage goes away.
    if (item && (item->get_inheritance_container() == this)) {
        item->set_inheritance_container(nullptr);
    }
}

Content_library_node::Content_library_node(
    const std::string_view                          folder_name,
    const uint64_t                                  type_code,
    const std::string_view                          type_name,
    const std::optional<erhe::property::Owner_type> category_owner_type
)
    : Item               {folder_name}
    , type_code          {type_code}
    , type_name          {type_name}
    , category_owner_type{category_owner_type}
{
    // Folders default closed in the tree (recursively - type folders and
    // their subfolders alike); only the library root opens by default, see
    // Content_library::Content_library().
}

Content_library_node::Content_library_node(const std::shared_ptr<erhe::Item_base>& in_item)
    : Item{in_item->get_name()}
    , item{in_item}
{
}

namespace {

// Claims ownership of every item wrapped by an OWNING node in the subtree:
// the item's Item_host becomes the library owner. Verifies the membership
// invariant - an owned item belongs to exactly one content library, so it
// must not already be hosted elsewhere. Reference entries (is_reference) are
// listings of items owned elsewhere (e.g. prefab template resources shared
// across scenes) and never touch the item's host.
//
// R5.6 flip: manager-owned asset types (brush, material, animation) never
// claim an Item_host - their runtime ownership lives in the owning scene's
// container record, maintained through the manager hook below, which also
// books a declared usership on every asset-typed entry (owning and
// reference alike). Libraries whose scene is not registered (previews, the
// tool scene, the Scene_builder template palette) have no armed manager;
// their asset-typed entries are pinned by the node's item pointer alone.
void claim_host_for_subtree(Content_library_node& subtree_root, erhe::Item_host* const owner, Asset_manager* const asset_manager)
{
    subtree_root.for_each<Content_library_node>(
        [owner, asset_manager](Content_library_node& node) -> bool {
            if (!node.item) {
                return true;
            }
            const bool manager_owned = is_manager_owned_asset_type(asset_type_from_item(*node.item));
            if (!manager_owned && !node.is_reference) {
                erhe::Item_host* const current_host = node.item->get_item_host();
                ERHE_VERIFY((current_host == nullptr) || (current_host == owner));
                node.item->set_item_host(owner);
            }
            if (manager_owned && (asset_manager != nullptr)) {
                asset_manager->on_library_node_attached(owner, node);
            }
            if (asset_manager != nullptr) {
                // Cancels a pending removal note: a library folder move is a
                // detach immediately followed by this attach, and must not be
                // announced as a removal
                // (doc/import-undo-reference-clearing.md).
                asset_manager->note_item_attached(node.item.get());
            }
            return true;
        }
    );
}

// Reverse of claim_host_for_subtree: detaches owned items from the given
// owner and (for manager-owned asset types) drops the record entry and the
// entry usership. Items hosted by someone else (never expected) and
// reference entries are left alone host-wise.
void release_host_for_subtree(Content_library_node& subtree_root, erhe::Item_host* const owner, Asset_manager* const asset_manager)
{
    subtree_root.for_each<Content_library_node>(
        [owner, asset_manager](Content_library_node& node) -> bool {
            if (!node.item) {
                return true;
            }
            const bool manager_owned = is_manager_owned_asset_type(asset_type_from_item(*node.item));
            if (!manager_owned && !node.is_reference && (node.item->get_item_host() == owner)) {
                node.item->set_item_host(nullptr);
            }
            if (manager_owned && (asset_manager != nullptr)) {
                asset_manager->on_library_node_detached(owner, node);
            }
            if (asset_manager != nullptr) {
                // EVERY entry type, not just the manager-owned ones above:
                // the graph editor windows hold Graph_mesh / Graph_texture
                // assets, which the same undo removes
                // (doc/import-undo-reference-clearing.md).
                asset_manager->note_item_detached(node.item);
            }
            return true;
        }
    );
}

} // anonymous namespace

auto Content_library_node::get_library() const -> Content_library*
{
    const Content_library_node* node = this;
    while (node != nullptr) {
        if (node->m_library != nullptr) {
            return node->m_library;
        }
        const std::shared_ptr<erhe::Hierarchy> parent = node->get_parent().lock();
        node = dynamic_cast<const Content_library_node*>(parent.get());
    }
    return nullptr;
}

void Content_library_node::handle_add_child(const std::shared_ptr<erhe::Hierarchy>& child_node, std::size_t position)
{
    const std::shared_ptr<Content_library_node> child = std::dynamic_pointer_cast<Content_library_node>(child_node);

    // Owning entries win the name over reference entries: an owning entry
    // wraps an item this library owns and the user authored, a reference
    // entry lists an item owned by another container. When both want one
    // name, the reference ENTRY NODE takes the numeric suffix (its item is
    // owned elsewhere and handle_sibling_unique_rename leaves it alone), so
    // a scene's authored names survive a reload whatever order the entries
    // attach in - the file's own materials before, or after, the materials
    // of an external asset it instances. Between two owning entries, or two
    // reference entries, the first-come rule of Hierarchy::handle_add_child
    // decides.
    if (child && !child->is_reference) {
        const std::string wanted_name = child->get_name();
        for (const std::shared_ptr<erhe::Hierarchy>& sibling : get_children()) {
            Content_library_node* const entry = dynamic_cast<Content_library_node*>(sibling.get());
            if ((entry == nullptr) || !entry->is_reference || (entry->get_name() != wanted_name)) {
                continue;
            }
            // exclude = nullptr, so the reference entry's own name counts as
            // taken and the first free '<base>_<number>' comes back.
            const std::string free_name = make_sibling_unique_name(this, wanted_name, nullptr);
            log_scene->info(
                "renaming reference entry '{}' to '{}': an owning entry of '{}' takes that name",
                wanted_name, free_name, describe()
            );
            entry->handle_sibling_unique_rename(free_name);
            break;
        }
    }

    Hierarchy::handle_add_child(child_node, position);

    // D1: an owning entry's item inherits from the entry node. Set here,
    // between Hierarchy::set_parent's snapshot capture and apply, so the
    // item is notified of the values it now inherits. A reference entry
    // lists an item owned by another scene, which this library's folders
    // must not affect.
    if (child && child->item && !child->is_reference) {
        child->item->set_inheritance_container(child.get());
    }

    Content_library* const library = get_library();
    if ((library != nullptr) && child) {
        // The index lists every entry of the attached subtree, whether or not
        // the library has an owner: a library with no scene (the
        // Scene_builder template palette) answers the same queries.
        child->for_each<Content_library_node>(
            [library](Content_library_node& node) -> bool {
                if (node.item) {
                    library->index_insert(
                        std::static_pointer_cast<Content_library_node>(node.shared_from_this())
                    );
                }
                return true;
            }
        );
    }
    erhe::Item_host* const owner = (library != nullptr) ? library->get_owner() : nullptr;
    if ((owner != nullptr) && child) {
        claim_host_for_subtree(*child.get(), owner, library->get_asset_manager());
    }
}

void Content_library_node::handle_sibling_unique_rename(const std::string& unique_name)
{
    erhe::Hierarchy::handle_sibling_unique_rename(unique_name);
    if (item && !is_reference) {
        item->set_name(unique_name);
    }
}

void Content_library_node::for_each_inheritance_child(const std::function<void(erhe::property::Dependency_object&)>& callback)
{
    Hierarchy::for_each_inheritance_child(callback);
    if (item && (item->get_inheritance_container() == this)) {
        callback(*item);
    }
}

auto Content_library_node::resolve_expression_object(const std::string_view path) const -> erhe::property::Dependency_object*
{
    if (path.empty() || (path == "..")) {
        return Item::resolve_expression_object(path);
    }
    Content_library* const library = get_library();
    erhe::Item_host* const owner   = (library != nullptr) ? library->get_owner() : nullptr;
    return (owner != nullptr) ? owner->find_hosted_item(path) : nullptr;
}

auto Content_library_node::get_secondary_property_owner_type() const -> std::optional<erhe::property::Owner_type>
{
    return item ? std::nullopt : category_owner_type;
}

void Content_library_node::handle_remove_child(erhe::Hierarchy* child_node)
{
    Hierarchy::handle_remove_child(child_node);

    Content_library* const      library = get_library();
    Content_library_node* const child   = dynamic_cast<Content_library_node*>(child_node);
    if ((library != nullptr) && (child != nullptr)) {
        child->for_each<Content_library_node>(
            [library](Content_library_node& node) -> bool {
                if (node.item) {
                    library->index_erase(node);
                }
                return true;
            }
        );
    }
    erhe::Item_host* const owner = (library != nullptr) ? library->get_owner() : nullptr;
    if ((owner != nullptr) && (child != nullptr)) {
        release_host_for_subtree(*child, owner, library->get_asset_manager());
    }
}

auto Content_library_node::find_listed_entry(const erhe::Item_base& queried_item) const -> std::shared_ptr<Content_library_node>
{
    Content_library* const library = get_library();
    return (library != nullptr) ? library->find_entry(queried_item) : std::shared_ptr<Content_library_node>{};
}

auto Content_library_node::make_folder(const std::string_view folder_name) -> std::shared_ptr<Content_library_node>
{
    auto new_folder_node = std::make_shared<Content_library_node>(folder_name, type_code, type_name, category_owner_type);
    new_folder_node->set_parent(this);
    return new_folder_node;
}

Content_library::Content_library()
{
    root       = std::make_shared<Content_library_node>("Content Library", erhe::Item_type::content_library_node, "Content_library");
    root->m_library = this;
    // The root opens by default so the type folders are visible; the folders
    // themselves default closed (see the folder constructor).
    root->enable_flag_bits(erhe::Item_flags::expand);

    // Each category folder names its item class as the category owner
    // type, so folders below it hold that class's properties for their
    // entries to inherit (doc/content-library-folders.md D8).
    brushes           = std::make_shared<Content_library_node>("Brushes",           erhe::Item_type::brush,                  "Brush",                  Brush::property_owner_type());
    animations        = std::make_shared<Content_library_node>("Animations",        erhe::Item_type::animation,              "Animation",              erhe::scene::Animation::property_owner_type());
    skins             = std::make_shared<Content_library_node>("Skins",             erhe::Item_type::skin,                   "Skin",                   erhe::scene::Skin::property_owner_type());
    materials         = std::make_shared<Content_library_node>("Materials",         erhe::Item_type::material,               "Material",               erhe::primitive::Material::property_owner_type());
    textures          = std::make_shared<Content_library_node>("Textures",          erhe::Item_type::texture,                "Texture",                erhe::graphics::Texture::property_owner_type());
    graph_textures    = std::make_shared<Content_library_node>("Graph Textures",    erhe::Item_type::graph_texture,          "Graph_texture",          Graph_texture::property_owner_type());
    graph_meshes      = std::make_shared<Content_library_node>("Graph Meshes",      erhe::Item_type::graph_mesh,             "Graph_mesh",             Graph_mesh::property_owner_type());
    physics_materials = std::make_shared<Content_library_node>("Physics Materials", erhe::Item_type::physics_material,       "Physics_material",       erhe::physics::Physics_material::property_owner_type());
    collision_filters = std::make_shared<Content_library_node>("Collision Filters", erhe::Item_type::collision_filter,       "Collision_filter",       erhe::physics::Collision_filter::property_owner_type());
    physics_joints    = std::make_shared<Content_library_node>("Physics Joints",    erhe::Item_type::physics_joint_settings, "Physics_joint_settings", erhe::physics::Physics_joint_settings::property_owner_type());
    styles            = std::make_shared<Content_library_node>("Styles",            erhe::Item_type::style,                  "Style",                  Style::property_owner_type());

    brushes          ->set_parent(root.get());
    animations       ->set_parent(root.get());
    skins            ->set_parent(root.get());
    materials        ->set_parent(root.get());
    textures         ->set_parent(root.get());
    graph_textures   ->set_parent(root.get());
    graph_meshes     ->set_parent(root.get());
    physics_materials->set_parent(root.get());
    collision_filters->set_parent(root.get());
    physics_joints   ->set_parent(root.get());
    styles           ->set_parent(root.get());
}

Content_library::~Content_library() noexcept
{
    // Library items can outlive the library (selection, clipboard, meshes
    // still holding a material); clear their host so no dangling Item_host
    // pointer survives the owning scene's destruction. The asset manager
    // hook is normally already disarmed here (scene unregistration precedes
    // library destruction in ~Scene_root).
    if ((m_owner != nullptr) && root) {
        release_host_for_subtree(*root.get(), m_owner, m_asset_manager);
    }
}

void Content_library::set_owner(erhe::Item_host* const owner)
{
    // Ownership is set once and never transferred; only clearing (to detach
    // items from a dying host) or re-setting the same owner is allowed.
    ERHE_VERIFY((m_owner == nullptr) || (owner == nullptr) || (m_owner == owner));
    erhe::Item_host* const previous_owner = m_owner;
    m_owner = owner;
    if (root) {
        if (owner != nullptr) {
            claim_host_for_subtree(*root.get(), owner, m_asset_manager);
        } else if (previous_owner != nullptr) {
            release_host_for_subtree(*root.get(), previous_owner, m_asset_manager);
        }
    }
}

auto Content_library::get_owner() const -> erhe::Item_host*
{
    return m_owner;
}

auto Content_library::get_category_root(const uint64_t kind_type_bit) const -> std::shared_ptr<Content_library_node>
{
    switch (kind_type_bit) {
        case erhe::Item_type::material:               return materials;
        case erhe::Item_type::texture:                return textures;
        case erhe::Item_type::brush:                  return brushes;
        case erhe::Item_type::style:                  return styles;
        case erhe::Item_type::physics_material:       return physics_materials;
        case erhe::Item_type::collision_filter:       return collision_filters;
        case erhe::Item_type::physics_joint_settings: return physics_joints;
        case erhe::Item_type::animation:              return animations;
        case erhe::Item_type::skin:                   return skins;
        case erhe::Item_type::graph_mesh:             return graph_meshes;
        case erhe::Item_type::graph_texture:          return graph_textures;
        default:                                      return {};
    }
}

void Content_library::index_insert(const std::shared_ptr<Content_library_node>& entry)
{
    if (!entry || !entry->item) {
        return;
    }
    const auto [it, inserted] = m_entry_by_item.emplace(entry->item.get(), entry);
    if (!inserted) {
        // Already listed: the attach walk re-visited it, or the same item is
        // wrapped by an entry that is still in the tree.
        it->second = entry;
        return;
    }
    const uint64_t kind_type_bit = get_kind_type_bit(*entry->item);
    if (kind_type_bit == 0) {
        return; // listed for find_entry(), but of no indexed kind
    }
    Kind_index& kind = m_by_kind[kind_type_bit];
    kind.items.push_back(entry->item);
    ++kind.serial;
}

void Content_library::index_erase(const Content_library_node& entry)
{
    if (!entry.item) {
        return;
    }
    const auto i = m_entry_by_item.find(entry.item.get());
    if (i == m_entry_by_item.end()) {
        return;
    }
    const std::shared_ptr<Content_library_node> listed = i->second.lock();
    if (listed && (listed.get() != &entry)) {
        return; // another entry wraps the item now; that listing stands
    }
    m_entry_by_item.erase(i);
    const uint64_t kind_type_bit = get_kind_type_bit(*entry.item);
    if (kind_type_bit == 0) {
        return;
    }
    Kind_index& kind = m_by_kind[kind_type_bit];
    const auto j = std::find(kind.items.begin(), kind.items.end(), entry.item);
    if (j != kind.items.end()) {
        kind.items.erase(j);
        ++kind.serial;
    }
}

auto Content_library::get_all_of_kind(const uint64_t kind_type_bit) const -> const std::vector<std::shared_ptr<erhe::Item_base>>&
{
    return m_by_kind[kind_type_bit].items;
}

auto Content_library::has_item(const erhe::Item_base& item) const -> bool
{
    const auto i = m_entry_by_item.find(&item);
    return (i != m_entry_by_item.end()) && !i->second.expired();
}

auto Content_library::find_entry(const erhe::Item_base& item) const -> std::shared_ptr<Content_library_node>
{
    const auto i = m_entry_by_item.find(&item);
    return (i != m_entry_by_item.end()) ? i->second.lock() : std::shared_ptr<Content_library_node>{};
}

void Content_library::set_asset_manager(Asset_manager* const asset_manager)
{
    m_asset_manager = asset_manager;
}

auto Content_library::get_asset_manager() const -> Asset_manager*
{
    return m_asset_manager;
}

void copy_content_library_folder(const Content_library_node& src_folder, Content_library_node& dst_folder)
{
    for (const std::shared_ptr<erhe::Hierarchy>& child_hierarchy : src_folder.get_children()) {
        const std::shared_ptr<Content_library_node> src_child = std::dynamic_pointer_cast<Content_library_node>(child_hierarchy);
        if (!src_child) {
            continue;
        }
        if (src_child->item) {
            std::shared_ptr<erhe::Item_base> item_copy{};
            const std::shared_ptr<Brush> brush = std::dynamic_pointer_cast<Brush>(src_child->item);
            if (brush) {
                item_copy = brush->make_shared_payload_copy();
            } else {
                item_copy = src_child->item->clone();
            }
            if (!item_copy) {
                log_scene->warn(
                    "copy_content_library_folder: skipping non-copyable {} '{}'",
                    src_child->item->get_type_name(),
                    src_child->item->get_name()
                );
                continue;
            }
            std::shared_ptr<Content_library_node> dst_child = std::make_shared<Content_library_node>(item_copy);
            dst_child->gltf_source = src_child->gltf_source;
            dst_child->set_parent(&dst_folder);
            // Leaves have no children in practice, but recurse for generality.
            copy_content_library_folder(*src_child, *dst_child);
        } else {
            std::shared_ptr<Content_library_node> dst_child = dst_folder.make_folder(src_child->get_name());
            if (!src_child->is_shown_in_ui()) {
                dst_child->disable_flag_bits(erhe::Item_flags::show_in_ui);
            }
            if ((src_child->get_flag_bits() & erhe::Item_flags::expand) != 0) {
                dst_child->enable_flag_bits(erhe::Item_flags::expand);
            } else {
                dst_child->disable_flag_bits(erhe::Item_flags::expand);
            }
            copy_content_library_folder(*src_child, *dst_child);
        }
    }
}

auto copy_library_item_to_library(const std::shared_ptr<erhe::Item_base>& item, Content_library& target_library) -> std::shared_ptr<erhe::Item_base>
{
    if (!item) {
        return {};
    }

    std::shared_ptr<Content_library_node> folder{};
    const uint64_t type = item->get_type();
    if      ((type & erhe::Item_type::brush)                  != 0) { folder = target_library.brushes;           }
    else if ((type & erhe::Item_type::material)               != 0) { folder = target_library.materials;         }
    else if ((type & erhe::Item_type::physics_material)       != 0) { folder = target_library.physics_materials; }
    else if ((type & erhe::Item_type::collision_filter)       != 0) { folder = target_library.collision_filters; }
    else if ((type & erhe::Item_type::physics_joint_settings) != 0) { folder = target_library.physics_joints;    }
    else if ((type & erhe::Item_type::style)                  != 0) { folder = target_library.styles;            }
    if (!folder) {
        return {};
    }

    std::shared_ptr<erhe::Item_base> copy{};
    const std::shared_ptr<Brush> brush = std::dynamic_pointer_cast<Brush>(item);
    if (brush) {
        copy = brush->make_shared_payload_copy();
    } else {
        copy = item->clone();
    }
    if (!copy) {
        return {};
    }

    // The copy keeps the source name; attaching the entry node below gives it
    // the numeric suffix when the target folder already lists that name
    // (doc/usd-compatibility-plan.md M2), renaming the copy with it.
    std::shared_ptr<Content_library_node> node = std::make_shared<Content_library_node>(copy);
    node->set_parent(folder.get());

    // The copy's style must not point into the source scene: a style of
    // the same name in the target library, else a copy of the style
    // (doc/style-library.md D2).
    if (copy->get_style() && target_library.styles) {
        const std::shared_ptr<const erhe::Item_base> source_style = std::dynamic_pointer_cast<const erhe::Item_base>(copy->get_style());
        std::shared_ptr<erhe::Item_base> target_style{};
        if (source_style) {
            for (const std::shared_ptr<Style>& candidate : target_library.get_all<Style>()) {
                if (candidate && (candidate->get_name() == source_style->get_name())) {
                    target_style = candidate;
                    break;
                }
            }
            if (!target_style) {
                target_style = copy_library_item_to_library(std::const_pointer_cast<erhe::Item_base>(source_style), target_library);
            }
        }
        copy->set_style(target_style);
    }
    return copy;
}

}
