#include "content_library/content_library.hpp"

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

void Content_library_node::handle_add_child(const std::shared_ptr<erhe::Hierarchy>& child_node, std::size_t position)
{
    Hierarchy::handle_add_child(child_node, position);
    m_cache.clear();
}

void Content_library_node::handle_remove_child(erhe::Hierarchy* child_node)
{
    Hierarchy::handle_remove_child(child_node);
    m_cache.clear();
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

}
