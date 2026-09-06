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

// The resource kinds the library indexes, in the order a scene creates their
// scopes. Each row is one `erhe::Item_type` bit and the name of the `Scope`
// the resources of that kind are gathered under.
class Kind_row
{
public:
    uint64_t         type_bit;
    std::string_view scope_name;
};

constexpr std::array<Kind_row, 11> c_kinds{
    Kind_row{erhe::Item_type::brush,                  "Brushes"          },
    Kind_row{erhe::Item_type::animation,              "Animations"       },
    Kind_row{erhe::Item_type::skin,                   "Skins"            },
    Kind_row{erhe::Item_type::material,               "Materials"        },
    Kind_row{erhe::Item_type::texture,                "Textures"         },
    Kind_row{erhe::Item_type::graph_texture,          "Graph Textures"   },
    Kind_row{erhe::Item_type::graph_mesh,             "Graph Meshes"     },
    Kind_row{erhe::Item_type::physics_material,       "Physics Materials"},
    Kind_row{erhe::Item_type::collision_filter,       "Collision Filters"},
    Kind_row{erhe::Item_type::physics_joint_settings, "Physics Joints"   },
    Kind_row{erhe::Item_type::style,                  "Styles"           }
};

constexpr auto make_all_kind_bits() -> uint64_t
{
    uint64_t bits = 0;
    for (const Kind_row& row : c_kinds) {
        bits |= row.type_bit;
    }
    return bits;
}

constexpr uint64_t c_all_kind_bits = make_all_kind_bits();

auto make_kind_type_bits() -> std::array<uint64_t, c_kinds.size()>
{
    std::array<uint64_t, c_kinds.size()> bits{};
    for (std::size_t i = 0; i < c_kinds.size(); ++i) {
        bits[i] = c_kinds[i].type_bit;
    }
    return bits;
}

} // anonymous namespace

auto Content_library::get_kind_type_bits() -> std::span<const uint64_t>
{
    static const std::array<uint64_t, c_kinds.size()> bits = make_kind_type_bits();
    return std::span<const uint64_t>{bits};
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

auto Content_library::get_kind_scope_name(const uint64_t kind_type_bit) -> std::string_view
{
    for (const Kind_row& row : c_kinds) {
        if (row.type_bit == kind_type_bit) {
            return row.scope_name;
        }
    }
    return {};
}

Content_library::Content_library()
{
    m_own_root = std::make_shared<erhe::Scope>("Content Library");
    // A library with no scene hosts its own prims, so the index is maintained
    // through the same hook in both cases.
    m_own_root->set_item_host(this);
}

Content_library::~Content_library() noexcept
{
    // Resources can outlive the library (selection, clipboard, meshes still
    // holding a material): take the kind scopes out of the tree, so no
    // resource keeps a dangling Item_host pointer to a dying host.
    std::vector<std::shared_ptr<erhe::Scope>> scopes;
    scopes.reserve(m_scopes.size());
    for (const auto& [type_bit, scope] : m_scopes) {
        scopes.push_back(scope);
    }
    m_scopes.clear();
    for (const std::shared_ptr<erhe::Scope>& scope : scopes) {
        scope->set_parent(std::shared_ptr<erhe::Hierarchy>{});
    }
    m_own_root.reset();
}

auto Content_library::get_host_name() const -> const char*
{
    return "Content_library";
}

void Content_library::register_prim(const std::shared_ptr<erhe::Typed>& prim)
{
    if (!prim || (get_kind_type_bit(*prim) == 0)) {
        return; // a Scope, or a prim of a class no library indexes
    }
    index_insert(prim);
    announce_attached(prim);
}

void Content_library::unregister_prim(const std::shared_ptr<erhe::Typed>& prim)
{
    if (!prim || (get_kind_type_bit(*prim) == 0)) {
        return;
    }
    index_erase(prim);
    announce_detached(prim);
}

void Content_library::announce_attached(const std::shared_ptr<erhe::Item_base>& item)
{
    if (m_asset_manager == nullptr) {
        return;
    }
    erhe::Item_host* const owner    = (m_owner != nullptr) ? m_owner : static_cast<erhe::Item_host*>(this);
    Resource_metadata&     metadata = metadata_entry(item);
    m_asset_manager->on_library_item_attached(
        owner, item,
        metadata.is_reference ? Library_listing::referenced : Library_listing::owned,
        metadata.asset_usership
    );
    // Cancels a pending removal note: a move between scopes is a detach
    // immediately followed by this attach, and must not be announced as a
    // removal (doc/import-undo-reference-clearing.md).
    m_asset_manager->note_item_attached(item.get());
}

void Content_library::announce_detached(const std::shared_ptr<erhe::Item_base>& item)
{
    if (m_asset_manager == nullptr) {
        return;
    }
    erhe::Item_host* const owner    = (m_owner != nullptr) ? m_owner : static_cast<erhe::Item_host*>(this);
    Resource_metadata&     metadata = metadata_entry(item);
    m_asset_manager->on_library_item_detached(
        owner, item,
        metadata.is_reference ? Library_listing::referenced : Library_listing::owned,
        metadata.asset_usership
    );
    // EVERY kind, not just the manager-owned ones: the graph editor windows
    // hold Graph_mesh / Graph_texture resources, which the same undo removes
    // (doc/import-undo-reference-clearing.md).
    m_asset_manager->note_item_detached(item);
}

void Content_library::index_insert(const std::shared_ptr<erhe::Item_base>& item)
{
    if (!m_listed.insert(item.get()).second) {
        return;
    }
    Kind_index& kind = m_by_kind[get_kind_type_bit(*item)];
    kind.items.push_back(item);
    ++kind.serial;
}

void Content_library::index_erase(const std::shared_ptr<erhe::Item_base>& item)
{
    if (m_listed.erase(item.get()) == 0) {
        return;
    }
    Kind_index& kind = m_by_kind[get_kind_type_bit(*item)];
    const auto i = std::find(kind.items.begin(), kind.items.end(), item);
    if (i != kind.items.end()) {
        kind.items.erase(i);
        ++kind.serial;
    }
}

void Content_library::set_owner(erhe::Item_host* const owner, const std::shared_ptr<erhe::Hierarchy>& prim_root)
{
    // Ownership is set once and never transferred; only clearing (to take the
    // scopes out of a dying host's tree) or re-setting the same owner is
    // allowed.
    ERHE_VERIFY((m_owner == nullptr) || (owner == nullptr) || (m_owner == owner));
    m_owner     = owner;
    m_prim_root = prim_root;
    const std::shared_ptr<erhe::Hierarchy> new_parent = get_prim_root();
    for (const auto& [type_bit, scope] : m_scopes) {
        scope->set_parent(new_parent);
    }
}

auto Content_library::get_owner() const -> erhe::Item_host*
{
    return m_owner;
}

auto Content_library::get_prim_root() const -> std::shared_ptr<erhe::Hierarchy>
{
    return m_prim_root ? m_prim_root : std::static_pointer_cast<erhe::Hierarchy>(m_own_root);
}

void Content_library::set_asset_manager(Asset_manager* const asset_manager)
{
    m_asset_manager = asset_manager;
}

auto Content_library::get_asset_manager() const -> Asset_manager*
{
    return m_asset_manager;
}

void Content_library::announce_all_listed()
{
    for (const uint64_t kind_type_bit : get_kind_type_bits()) {
        // A copy: announce_attached() can touch the metadata table, and the
        // manager may not reorder the index, but the loop must survive
        // either way.
        const std::vector<std::shared_ptr<erhe::Item_base>> items = m_by_kind[kind_type_bit].items;
        for (const std::shared_ptr<erhe::Item_base>& item : items) {
            announce_attached(item);
        }
    }
}

auto Content_library::find_scope(const uint64_t kind_type_bit) const -> std::shared_ptr<erhe::Scope>
{
    const auto i = m_scopes.find(kind_type_bit);
    return (i != m_scopes.end()) ? i->second : std::shared_ptr<erhe::Scope>{};
}

auto Content_library::get_scope(const uint64_t kind_type_bit) -> std::shared_ptr<erhe::Scope>
{
    const std::shared_ptr<erhe::Scope> existing = find_scope(kind_type_bit);
    if (existing) {
        return existing;
    }
    const std::string_view scope_name = get_kind_scope_name(kind_type_bit);
    if (scope_name.empty()) {
        log_scene->warn("content library: item type bit {:#x} names no resource kind", kind_type_bit);
        return {};
    }
    std::shared_ptr<erhe::Scope> scope = std::make_shared<erhe::Scope>(scope_name);
    scope->enable_flag_bits(erhe::Item_flags::show_in_ui);
    m_scopes.emplace(kind_type_bit, scope);
    scope->set_parent(get_prim_root());
    return scope;
}

auto Content_library::find_scope_kind(const erhe::Hierarchy& prim) const -> uint64_t
{
    for (const auto& [type_bit, scope] : m_scopes) {
        if ((scope.get() == &prim) || prim.is_ancestor(scope.get())) {
            return type_bit;
        }
    }
    return 0;
}

void Content_library::add_referenced(const std::shared_ptr<erhe::Item_base>& item, const std::optional<Asset_key>& asset_key)
{
    ERHE_VERIFY(item);
    if (get_kind_type_bit(*item) == 0) {
        return;
    }
    Resource_metadata& metadata = metadata_entry(item);
    if (asset_key.has_value() && !metadata.asset_key.has_value()) {
        metadata.asset_key = asset_key;
    }
    if (has_item(*item)) {
        return;
    }
    metadata.is_reference = true;
    m_referenced.emplace(item.get(), item);
    index_insert(item);
    announce_attached(item);
}

void Content_library::remove_referenced(const std::shared_ptr<erhe::Item_base>& item)
{
    ERHE_VERIFY(item);
    if (m_referenced.erase(item.get()) == 0) {
        return;
    }
    index_erase(item);
    announce_detached(item);
    m_metadata.erase(item.get());
}

auto Content_library::is_referenced(const erhe::Item_base& item) const -> bool
{
    const Resource_metadata* const metadata = find_metadata(item);
    return (metadata != nullptr) && metadata->is_reference;
}

void Content_library::set_referenced(const erhe::Item_base& item)
{
    const auto i = m_metadata.find(&item);
    if (i != m_metadata.end()) {
        i->second.is_reference = true;
    }
}

auto Content_library::has_item(const erhe::Item_base& item) const -> bool
{
    return m_listed.contains(&item);
}

auto Content_library::get_all_of_kind(const uint64_t kind_type_bit) const -> const std::vector<std::shared_ptr<erhe::Item_base>>&
{
    return m_by_kind[kind_type_bit].items;
}

auto Content_library::find_metadata(const erhe::Item_base& item) const -> const Resource_metadata*
{
    const auto i = m_metadata.find(&item);
    if (i == m_metadata.end()) {
        return nullptr;
    }
    // A pointer key can be reused by a later allocation once the resource is
    // gone; the weak guard says whether this entry is still that resource's.
    return i->second.resource.expired() ? nullptr : &i->second;
}

auto Content_library::metadata_entry(const std::shared_ptr<erhe::Item_base>& item) -> Resource_metadata&
{
    Resource_metadata& metadata = m_metadata[item.get()];
    if (metadata.resource.expired() || (metadata.resource.lock() != item)) {
        metadata = Resource_metadata{};
        metadata.resource = item;
    }
    return metadata;
}

void Content_library::set_gltf_source(const std::shared_ptr<erhe::Item_base>& item, const Gltf_source_reference& gltf_source)
{
    metadata_entry(item).gltf_source = gltf_source;
}

void Content_library::set_image_source(const std::shared_ptr<erhe::Item_base>& item, const std::shared_ptr<erhe::gltf::Gltf_image_source>& image_source)
{
    metadata_entry(item).image_source = image_source;
}

void Content_library::set_asset_key(const std::shared_ptr<erhe::Item_base>& item, const Asset_key& asset_key)
{
    metadata_entry(item).asset_key = asset_key;
}

namespace {

// Copies one subtree of a source library's scope structure into a target
// subtree, recreating the scopes and copying the resources.
void copy_subtree(
    const Content_library&  source,
    const erhe::Hierarchy&  src_parent,
    erhe::Hierarchy&        dst_parent,
    Content_library&        target
)
{
    for (const std::shared_ptr<erhe::Hierarchy>& src_child : src_parent.get_children()) {
        const std::shared_ptr<erhe::Scope> src_scope = std::dynamic_pointer_cast<erhe::Scope>(src_child);
        if (src_scope) {
            std::shared_ptr<erhe::Scope> dst_scope = std::make_shared<erhe::Scope>(src_scope->get_name());
            if (src_scope->is_shown_in_ui()) {
                dst_scope->enable_flag_bits(erhe::Item_flags::show_in_ui);
            } else {
                dst_scope->disable_flag_bits(erhe::Item_flags::show_in_ui);
            }
            if ((src_scope->get_flag_bits() & erhe::Item_flags::expand) != 0) {
                dst_scope->enable_flag_bits(erhe::Item_flags::expand);
            } else {
                dst_scope->disable_flag_bits(erhe::Item_flags::expand);
            }
            dst_scope->set_parent(&dst_parent);
            copy_subtree(source, *src_scope, *dst_scope, target);
            continue;
        }
        if (Content_library::get_kind_type_bit(*src_child) == 0) {
            continue;
        }
        std::shared_ptr<erhe::Item_base> item_copy{};
        const std::shared_ptr<Brush> brush = std::dynamic_pointer_cast<Brush>(src_child);
        if (brush) {
            item_copy = brush->make_shared_payload_copy();
        } else {
            item_copy = src_child->clone();
        }
        const std::shared_ptr<erhe::Hierarchy> copy_prim = std::dynamic_pointer_cast<erhe::Hierarchy>(item_copy);
        if (!copy_prim) {
            log_scene->warn(
                "copy_content_library: skipping non-copyable {} '{}'",
                src_child->get_type_name(), src_child->get_name()
            );
            continue;
        }
        const Resource_metadata* const metadata = source.find_metadata(*src_child);
        copy_prim->set_parent(&dst_parent);
        if ((metadata != nullptr) && metadata->gltf_source.has_value()) {
            target.set_gltf_source(item_copy, metadata->gltf_source.value());
        }
        copy_subtree(source, *src_child, *copy_prim, target);
    }
}

} // anonymous namespace

void copy_content_library(const Content_library& source, Content_library& target)
{
    for (const uint64_t kind_type_bit : Content_library::get_kind_type_bits()) {
        const std::shared_ptr<erhe::Scope> src_scope = source.find_scope(kind_type_bit);
        if (!src_scope) {
            continue;
        }
        const std::shared_ptr<erhe::Scope> dst_scope = target.get_scope(kind_type_bit);
        if (!dst_scope) {
            continue;
        }
        copy_subtree(source, *src_scope, *dst_scope, target);
    }
}

auto copy_library_item_to_library(const std::shared_ptr<erhe::Item_base>& item, Content_library& target_library) -> std::shared_ptr<erhe::Item_base>
{
    if (!item) {
        return {};
    }
    const uint64_t kind_type_bit = Content_library::get_kind_type_bit(*item);
    // Textures and graph assets are shared GPU / graph resources - a copy
    // would alias the device object, so they are not copied across libraries.
    const bool copyable =
        (kind_type_bit == erhe::Item_type::brush)                  ||
        (kind_type_bit == erhe::Item_type::material)               ||
        (kind_type_bit == erhe::Item_type::physics_material)       ||
        (kind_type_bit == erhe::Item_type::collision_filter)       ||
        (kind_type_bit == erhe::Item_type::physics_joint_settings) ||
        (kind_type_bit == erhe::Item_type::style);
    if (!copyable) {
        return {};
    }

    std::shared_ptr<erhe::Item_base> copy{};
    const std::shared_ptr<Brush> brush = std::dynamic_pointer_cast<Brush>(item);
    if (brush) {
        copy = brush->make_shared_payload_copy();
    } else {
        copy = item->clone();
    }
    const std::shared_ptr<erhe::Hierarchy> copy_prim = std::dynamic_pointer_cast<erhe::Hierarchy>(copy);
    if (!copy_prim) {
        return {};
    }
    const std::shared_ptr<erhe::Scope> scope = target_library.get_scope(kind_type_bit);
    if (!scope) {
        return {};
    }
    // The copy keeps the source name; attaching it gives it the numeric
    // suffix when the target scope already holds that name
    // (doc/usd-compatibility-plan.md M2).
    copy_prim->set_parent(scope);

    // The copy's style must not point into the source scene: a style of the
    // same name in the target library, else a copy of the style
    // (doc/style-library.md D2).
    if (copy->get_style()) {
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
