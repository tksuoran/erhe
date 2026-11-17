#include "level/kit_piece_light.hpp"
#include "level/kit_light.hpp"
#include "level/kit_shape.hpp"

#include "app_context.hpp"
#include "editor_item_bits.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/node_transform_operation.hpp"
#include "operations/property_set_operation.hpp"
#include "scene/scene_root.hpp"

#include "erhe_item/hierarchy.hpp"
#include "erhe_property/attached_group.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/trs_transform.hpp"

#include <limits>

namespace editor {

namespace {

using erhe::property::Dependency_property;
using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;

[[nodiscard]] auto to_scene_light_type(const Kit_light_type type) -> erhe::scene::Light_type
{
    switch (type) {
        case Kit_light_type::spot:  return erhe::scene::Light_type::spot;
        case Kit_light_type::point:
        default:                    return erhe::scene::Light_type::point;
    }
}

[[nodiscard]] auto get_frame_transform(const Kit_light_frame& frame) -> erhe::scene::Trs_transform
{
    return erhe::scene::Trs_transform{frame.position, get_light_frame_rotation(frame.direction), glm::vec3{1.0f}};
}

[[nodiscard]] auto is_light_at_frame(const erhe::scene::Light& light, const Kit_light_frame& frame) -> bool
{
    const erhe::scene::Trs_transform& local = light.parent_from_node_transform();
    return is_at_light_frame(local.get_translation(), local.get_rotation(), local.get_scale(), frame);
}

// The node's subtree, children before their parents.
void collect_subtree_deepest_first(const std::shared_ptr<erhe::Hierarchy>& hierarchy, std::vector<std::shared_ptr<erhe::Hierarchy>>& out_items)
{
    for (const std::shared_ptr<erhe::Hierarchy>& child : hierarchy->get_children()) {
        collect_subtree_deepest_first(child, out_items);
    }
    out_items.push_back(hierarchy);
}

} // anonymous namespace

auto Kit_light::property_owner_type() -> erhe::property::Owner_type
{
    static const erhe::property::Owner_type s_id = erhe::property::allocate_owner_type(
        erhe::property::root_owner_type, "Kit_light"
    );
    return s_id;
}

const Property<std::string> Kit_light::frame_property = Property<std::string>::register_attached(
    "frame", Kit_light::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value = std::string{},
        .inherits      = false,
        .ui            = Property_ui{
            .group          = "Kit Light",
            .tooltip        = "The light frame of its kit piece this light node sits on (doc/editor/level_tools.md \"Lights on pieces\")",
            .developer_only = true,
            .label          = "Frame"
        }
    }
);

auto Kit_light::all_properties() -> const std::vector<const Dependency_property*>&
{
    static const std::vector<const Dependency_property*> s_properties{
        frame_property.get_ptr()
    };
    return s_properties;
}

auto read_kit_light_frame(const erhe::scene::Node& node) -> std::optional<std::string>
{
    if (!erhe::property::carries_attached_group(node, Kit_light::frame_property.get())) {
        return {};
    }
    return node.get_value(Kit_light::frame_property);
}

void collect_piece_lights(const erhe::scene::Node& piece, std::vector<std::shared_ptr<erhe::scene::Light>>& out_lights)
{
    out_lights.clear();
    for (const std::shared_ptr<erhe::Hierarchy>& child : piece.get_children()) {
        std::shared_ptr<erhe::scene::Light> light = std::dynamic_pointer_cast<erhe::scene::Light>(child);
        if (light && read_kit_light_frame(*light).has_value()) {
            out_lights.push_back(std::move(light));
        }
    }
}

auto has_only_kit_light_children(const erhe::scene::Node& node) -> bool
{
    for (const std::shared_ptr<erhe::Hierarchy>& child : node.get_children()) {
        const erhe::scene::Light* const light = dynamic_cast<const erhe::scene::Light*>(child.get());
        if ((light == nullptr) || !read_kit_light_frame(*light).has_value() || (light->get_child_count() > 0)) {
            return false;
        }
    }
    return true;
}

auto make_piece_light(Scene_root& scene_root, const Kit_light_frame& frame) -> std::shared_ptr<erhe::scene::Light>
{
    std::shared_ptr<erhe::scene::Light> light = std::make_shared<erhe::scene::Light>(frame.name);
    light->set_light_type      (to_scene_light_type(frame.start.type));
    light->set_color           (frame.start.color);
    light->set_intensity       (frame.start.intensity);
    light->set_range           (frame.start.range);
    if (frame.start.type == Kit_light_type::spot) {
        light->set_inner_spot_angle(frame.start.inner_spot_angle);
        light->set_outer_spot_angle(frame.start.outer_spot_angle);
    }
    light->set_cast_shadow     (false);
    light->layer_id = scene_root.layers().light()->id;
    light->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui | Editor_item_flags::show_debug_visualizations);
    light->set_value(Kit_light::frame_property, std::string{frame.name});
    light->set_parent_from_node(get_frame_transform(frame));
    return light;
}

void add_piece_lights(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& piece, const Kit_parameter_values& values)
{
    std::vector<Kit_light_frame> frames;
    get_shape_light_frames(values, frames);
    for (const Kit_light_frame& frame : frames) {
        const std::shared_ptr<erhe::scene::Light> light = make_piece_light(scene_root, frame);
        // set_parent() keeps the world transform; the frame is the local one.
        light->set_parent(piece);
        light->set_parent_from_node(get_frame_transform(frame));
    }
}

auto Kit_piece_light_sync::make_plan(const erhe::scene::Node& piece, const Kit_parameter_values& values) -> bool
{
    const Kit_shape_definition* const shape = values.get_shape();
    const bool has_frames = (shape != nullptr) && static_cast<bool>(shape->get_light_frames);
    if (!has_frames && (piece.get_child_count() == 0)) {
        return false;
    }
    get_shape_light_frames(values, m_frames);
    collect_piece_lights(piece, m_lights);
    if (m_frames.empty() && m_lights.empty()) {
        return false;
    }
    // Assigned in place: a frame name fits the strings' capacity, so a steady step allocates nothing.
    m_light_frames.resize(m_lights.size());
    for (std::size_t i = 0, end = m_lights.size(); i < end; ++i) {
        m_light_frames[i] = m_lights[i]->get_value(Kit_light::frame_property);
    }
    match_light_frames(m_frames, std::span<const std::string>{m_light_frames.data(), m_lights.size()}, m_match);
    return true;
}

void Kit_piece_light_sync::release()
{
    m_lights.clear();
    m_subtree.clear();
}

void Kit_piece_light_sync::append(
    App_context&                              context,
    Scene_root&                               scene_root,
    const std::shared_ptr<erhe::scene::Node>& piece,
    const Kit_parameter_values&               values,
    std::vector<std::shared_ptr<Operation>>&  out_operations
)
{
    if (!make_plan(*piece, values)) {
        release();
        return;
    }
    // A user's copy of a kit light keeps living as a plain light: its record goes, its node stays.
    for (const std::size_t node_index : m_match.released_nodes) {
        const std::shared_ptr<erhe::scene::Light>& light = m_lights[node_index];
        out_operations.push_back(
            std::make_shared<Property_set_operation>(
                light, Kit_light::frame_property.get(), light->read_local_state(Kit_light::frame_property.get()), std::optional<erhe::property::Local_state>{}
            )
        );
    }
    // Removed last child first, each subtree deepest first, so an undo puts every node back at its index.
    for (auto i = m_match.removed_nodes.rbegin(), end = m_match.removed_nodes.rend(); i != end; ++i) {
        m_subtree.clear();
        collect_subtree_deepest_first(m_lights[*i], m_subtree);
        for (const std::shared_ptr<erhe::Hierarchy>& item : m_subtree) {
            out_operations.push_back(
                std::make_shared<Item_insert_remove_operation>(
                    Item_insert_remove_operation::Parameters{
                        .context = context,
                        .item    = item,
                        .parent  = item->get_parent().lock(),
                        .mode    = Item_insert_remove_operation::Mode::remove
                    }
                )
            );
        }
    }
    for (std::size_t f = 0, end = m_frames.size(); f < end; ++f) {
        const Kit_light_frame& frame      = m_frames[f];
        const std::size_t      node_index = m_match.frame_nodes[f];
        if (node_index != c_no_light_node) {
            const std::shared_ptr<erhe::scene::Light>& light = m_lights[node_index];
            if (is_light_at_frame(*light, frame)) {
                continue;
            }
            out_operations.push_back(
                std::make_shared<Node_transform_operation>(
                    Node_transform_operation::Parameters{
                        .node                    = light,
                        .parent_from_node_before = light->parent_from_node_transform(),
                        .parent_from_node_after  = get_frame_transform(frame),
                        .xform_op_stack_before   = light->copy_xform_op_stack()
                    }
                )
            );
            continue;
        }
        // The new node keeps the frame as its local transform: under a sheared piece a world matrix would not decompose back to it.
        out_operations.push_back(
            std::make_shared<Item_insert_remove_operation>(
                Item_insert_remove_operation::Parameters{
                    .context         = context,
                    .item            = make_piece_light(scene_root, frame),
                    .parent          = piece,
                    .mode            = Item_insert_remove_operation::Mode::insert,
                    .index_in_parent = std::numeric_limits<std::size_t>::max(), // last child
                    .first_insert    = Item_insert_transform::keep_local
                }
            )
        );
    }
    release();
}

void Kit_piece_light_sync::preview(const std::shared_ptr<erhe::scene::Node>& piece, const Kit_parameter_values& values)
{
    if (!m_previewing) {
        m_previewing = true;
        m_preview.clear();
        collect_piece_lights(*piece, m_lights);
        for (const std::shared_ptr<erhe::scene::Light>& light : m_lights) {
            m_preview.push_back(Remembered{.light = light, .parent_from_node = light->parent_from_node_transform()});
        }
    }
    if (!make_plan(*piece, values)) {
        release();
        return;
    }
    for (std::size_t f = 0, end = m_frames.size(); f < end; ++f) {
        const std::size_t node_index = m_match.frame_nodes[f];
        if ((node_index != c_no_light_node) && !is_light_at_frame(*m_lights[node_index], m_frames[f])) {
            m_lights[node_index]->set_parent_from_node(get_frame_transform(m_frames[f]));
        }
    }
    release();
}

void Kit_piece_light_sync::restore_preview()
{
    for (const Remembered& remembered : m_preview) {
        remembered.light->set_parent_from_node(remembered.parent_from_node);
    }
    m_preview.clear();
    m_previewing = false;
}

auto Kit_piece_light_sync::apply_now(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& piece, const Kit_parameter_values& values) -> std::size_t
{
    if (!make_plan(*piece, values)) {
        release();
        return 0;
    }
    std::size_t changed = 0;
    for (const std::size_t node_index : m_match.released_nodes) {
        static_cast<void>(m_lights[node_index]->apply_local_state(Kit_light::frame_property.get(), std::optional<erhe::property::Local_state>{}));
        ++changed;
    }
    for (const std::size_t node_index : m_match.removed_nodes) {
        m_lights[node_index]->set_parent(std::shared_ptr<erhe::Hierarchy>{});
        ++changed;
    }
    for (std::size_t f = 0, end = m_frames.size(); f < end; ++f) {
        const Kit_light_frame& frame      = m_frames[f];
        const std::size_t      node_index = m_match.frame_nodes[f];
        if (node_index != c_no_light_node) {
            const std::shared_ptr<erhe::scene::Light>& light = m_lights[node_index];
            if (!is_light_at_frame(*light, frame)) {
                light->set_parent_from_node(get_frame_transform(frame));
                ++changed;
            }
            continue;
        }
        const std::shared_ptr<erhe::scene::Light> light = make_piece_light(scene_root, frame);
        light->set_parent(piece);
        light->set_parent_from_node(get_frame_transform(frame));
        ++changed;
    }
    release();
    return changed;
}

} // namespace editor
