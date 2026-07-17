#include "content_library/content_library.hpp"

#include "brushes/brush.hpp"
#include "texture_graph/graph_texture.hpp"

#include "erhe_graphics/texture.hpp"

namespace editor {

Content_library_node::Content_library_node(const Content_library_node& other)
    : Item     {other}
    , type_code{other.type_code}
    , type_name{other.type_name}
    , item     {other.item ? other.item->clone() : std::shared_ptr<erhe::Item_base>{}}
{
}
Content_library_node& Content_library_node::operator=(const Content_library_node&) = default;
Content_library_node::~Content_library_node() noexcept                             = default;

Content_library_node::Content_library_node(std::string_view folder_name, uint64_t type_code, std::string_view type_name)
    : Item     {folder_name}
    , type_code{type_code}
    , type_name{type_name}
{
    enable_flag_bits(erhe::Item_flags::expand);
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
void claim_host_for_subtree(Content_library_node& subtree_root, erhe::Item_host* const owner)
{
    subtree_root.for_each<Content_library_node>(
        [owner](Content_library_node& node) -> bool {
            if (node.item && !node.is_reference) {
                erhe::Item_host* const current_host = node.item->get_item_host();
                ERHE_VERIFY((current_host == nullptr) || (current_host == owner));
                node.item->set_item_host(owner);
            }
            return true;
        }
    );
}

// Reverse of claim_host_for_subtree: detaches owned items from the given
// owner. Items hosted by someone else (never expected) and reference entries
// are left alone.
void release_host_for_subtree(Content_library_node& subtree_root, erhe::Item_host* const owner)
{
    subtree_root.for_each<Content_library_node>(
        [owner](Content_library_node& node) -> bool {
            if (node.item && !node.is_reference && (node.item->get_item_host() == owner)) {
                node.item->set_item_host(nullptr);
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
    Hierarchy::handle_add_child(child_node, position);
    m_cache.clear();

    Content_library* const library = get_library();
    erhe::Item_host* const owner   = (library != nullptr) ? library->get_owner() : nullptr;
    if (owner != nullptr) {
        const std::shared_ptr<Content_library_node> child = std::dynamic_pointer_cast<Content_library_node>(child_node);
        if (child) {
            claim_host_for_subtree(*child.get(), owner);
        }
    }
}

void Content_library_node::handle_remove_child(erhe::Hierarchy* child_node)
{
    Hierarchy::handle_remove_child(child_node);
    m_cache.clear();

    Content_library* const library = get_library();
    erhe::Item_host* const owner   = (library != nullptr) ? library->get_owner() : nullptr;
    if (owner != nullptr) {
        Content_library_node* const child = dynamic_cast<Content_library_node*>(child_node);
        if (child != nullptr) {
            release_host_for_subtree(*child, owner);
        }
    }
}

auto Content_library_node::has_item(const erhe::Item_base& queried_item) const -> bool
{
    bool found = false;
    for_each_const<Content_library_node>(
        [&found, &queried_item](const Content_library_node& node) -> bool {
            if (node.item.get() == &queried_item) {
                found = true;
                return false; // in for_each() lambda - found, stop
            }
            return true; // in for_each() lambda - continue to children
        }
    );
    return found;
}

auto Content_library_node::make_folder(const std::string_view folder_name) -> std::shared_ptr<Content_library_node>
{
    auto new_folder_node = std::make_shared<Content_library_node>(folder_name, type_code, type_name);
    new_folder_node->set_parent(this);
    return new_folder_node;
}

Content_library::Content_library()
{
    root       = std::make_shared<Content_library_node>("Content Library", erhe::Item_type::content_library_node, "Content_library");
    root->m_library = this;

    brushes           = std::make_shared<Content_library_node>("Brushes",           erhe::Item_type::brush,                  "Brush"                 );
    animations        = std::make_shared<Content_library_node>("Animations",        erhe::Item_type::animation,              "Animation"             );
    skins             = std::make_shared<Content_library_node>("Skins",             erhe::Item_type::skin,                   "Skin"                  );
    materials         = std::make_shared<Content_library_node>("Materials",         erhe::Item_type::material,               "Material"              );
    textures          = std::make_shared<Content_library_node>("Textures",          erhe::Item_type::texture,                "Texture"               );
    graph_textures    = std::make_shared<Content_library_node>("Graph Textures",    erhe::Item_type::graph_texture,          "Graph_texture"         );
    graph_meshes      = std::make_shared<Content_library_node>("Graph Meshes",      erhe::Item_type::graph_mesh,             "Graph_mesh"            );
    physics_materials = std::make_shared<Content_library_node>("Physics Materials", erhe::Item_type::physics_material,       "Physics_material"      );
    collision_filters = std::make_shared<Content_library_node>("Collision Filters", erhe::Item_type::collision_filter,       "Collision_filter"      );
    physics_joints    = std::make_shared<Content_library_node>("Physics Joints",    erhe::Item_type::physics_joint_settings, "Physics_joint_settings");

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
}

Content_library::~Content_library() noexcept
{
    // Library items can outlive the library (selection, clipboard, meshes
    // still holding a material); clear their host so no dangling Item_host
    // pointer survives the owning scene's destruction.
    if ((m_owner != nullptr) && root) {
        release_host_for_subtree(*root.get(), m_owner);
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
            claim_host_for_subtree(*root.get(), owner);
        } else if (previous_owner != nullptr) {
            release_host_for_subtree(*root.get(), previous_owner);
        }
    }
}

auto Content_library::get_owner() const -> erhe::Item_host*
{
    return m_owner;
}

auto Content_library::texture_reference_combo(
    App_context&                                        context,
    const char*                                         label,
    std::shared_ptr<erhe::graphics::Texture_reference>& in_out_reference,
    const bool                                          empty_option
) const -> bool
{
    const bool             empty_entry   = empty_option || (!in_out_reference);
    const erhe::Item_base* selected_item = dynamic_cast<const erhe::Item_base*>(in_out_reference.get());
    const char*            preview_value =
        (selected_item != nullptr) ? selected_item->get_name().c_str()
        : in_out_reference         ? "(unnamed)"
                                   : "(none)";
    bool selection_changed = false;

    // List every shown item in the folder that is a Texture_reference (both
    // plain Texture and Graph_texture are). Returns the number of listed items
    // so a separator is only drawn between non-empty sections.
    const auto add_section = [&context, &selection_changed, &in_out_reference](const Content_library_node* folder) -> int {
        int shown_count = 0;
        if (folder == nullptr) {
            return shown_count;
        }
        folder->for_each_const<Content_library_node>(
            [&context, &selection_changed, &in_out_reference, &shown_count](const Content_library_node& node) -> bool {
                const std::shared_ptr<erhe::graphics::Texture_reference> node_reference =
                    std::dynamic_pointer_cast<erhe::graphics::Texture_reference>(node.item);
                if (!node_reference) {
                    return true; // in for_each() lambda - continue to children
                }
                const bool shown = node.item->is_shown_in_ui() ||
                    (context.developer_mode && ((node.item->get_flag_bits() & erhe::Item_flags::show_in_developer_ui) != 0));
                if (!shown) {
                    return true; // in for_each() lambda - continue to children
                }
                ++shown_count;
                const bool is_selected = (in_out_reference == node_reference);
                context.icon_set->add_icons(node.item->get_type(), 1.0f);
                if (ImGui::Selectable(node.item->get_debug_label().data(), is_selected)) {
                    in_out_reference  = node_reference;
                    selection_changed = true;
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
                return !selection_changed; // in for_each() lambda - continue to children if not selection_changed
            }
        );
        return shown_count;
    };

    const bool begin = ImGui::BeginCombo(label, preview_value, ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_HeightLarge);
    if (begin) {
        if (empty_entry) {
            const bool is_selected = !in_out_reference;
            if (ImGui::Selectable("(none)", is_selected)) {
                in_out_reference.reset();
                selection_changed = true;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }

        const int texture_count = add_section(textures.get());
        if ((texture_count > 0) && !selection_changed) {
            // Peek whether the graph section will show anything before drawing
            // the separator is not worth the extra pass; a separator above an
            // empty section is drawn only when graph textures exist at all.
            if (graph_textures && (graph_textures->get_child_count() > 0)) {
                ImGui::Separator();
            }
        }
        add_section(graph_textures.get());

        ImGui::EndCombo();
    } else if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* drag_node_payload    = ImGui::AcceptDragDropPayload(Content_library_node::static_type_name.data());
        const ImGuiPayload* drag_texture_payload = ImGui::AcceptDragDropPayload(erhe::graphics::Texture::static_type_name.data());
        const ImGuiPayload* drag_graph_payload   = ImGui::AcceptDragDropPayload(Graph_texture::static_type_name.data());
        const erhe::Item_base* drag_item = nullptr;
        if (drag_texture_payload != nullptr) {
            drag_item = *(static_cast<erhe::Item_base**>(drag_texture_payload->Data));
        } else if (drag_graph_payload != nullptr) {
            drag_item = *(static_cast<erhe::Item_base**>(drag_graph_payload->Data));
        } else if (drag_node_payload != nullptr) {
            const erhe::Item_base*      drag_node_ = *(static_cast<erhe::Item_base**>(drag_node_payload->Data));
            const Content_library_node* drag_node  = dynamic_cast<const Content_library_node*>(drag_node_);
            if (drag_node != nullptr) {
                drag_item = drag_node->item.get();
            }
        }
        if (drag_item != nullptr) {
            const auto accept_from = [&selection_changed, &in_out_reference, drag_item](const Content_library_node* folder) {
                if ((folder == nullptr) || selection_changed) {
                    return;
                }
                folder->for_each_const<Content_library_node>(
                    [&selection_changed, &in_out_reference, drag_item](const Content_library_node& node) -> bool {
                        if (node.item.get() == drag_item) {
                            const std::shared_ptr<erhe::graphics::Texture_reference> node_reference =
                                std::dynamic_pointer_cast<erhe::graphics::Texture_reference>(node.item);
                            if (node_reference) {
                                in_out_reference  = node_reference;
                                selection_changed = true;
                            }
                            return false; // in for_each() lambda - stop
                        }
                        return true; // in for_each() lambda - continue to children
                    }
                );
            };
            accept_from(textures.get());
            accept_from(graph_textures.get());
        }
        ImGui::EndDragDropTarget();
        return selection_changed;
    }

    return selection_changed;
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
            if ((src_child->get_flag_bits() & erhe::Item_flags::expand) == 0) {
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

    std::set<std::string> used_names;
    folder->for_each<Content_library_node>(
        [&used_names](Content_library_node& node) -> bool {
            if (node.item) {
                used_names.insert(node.item->get_name());
            }
            return true;
        }
    );
    const std::string base_name  = item->get_name();
    std::string       final_name = base_name;
    for (std::size_t number = 2; used_names.contains(final_name); ++number) {
        final_name = base_name + " (" + std::to_string(number) + ")";
    }
    copy->set_name(final_name);

    std::shared_ptr<Content_library_node> node = std::make_shared<Content_library_node>(copy);
    node->set_parent(folder.get());
    return copy;
}

}
