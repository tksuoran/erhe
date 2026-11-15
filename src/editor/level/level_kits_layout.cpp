// Kit layout nodes of Level_kits (D3.9, doc/editor/level_tools.md "Kit layout nodes").

#include "level/level_kits.hpp"

#include "level/kit_layout.hpp"
#include "level/kit_piece.hpp"

#include "app_context.hpp"
#include "brushes/brush.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "operations/compound_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/property_set_operation.hpp"
#include "prefabs/instance_structure.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_item/hierarchy.hpp"
#include "erhe_math/curve_path.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_property/property_value.hpp"
#include "erhe_scene/curve.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_scene/xform.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <set>

namespace editor {

namespace {

constexpr float c_pose_tolerance     = 1.0e-4f;
constexpr float c_rotation_tolerance = 1.0e-5f;

[[nodiscard]] auto find_library_material(const std::shared_ptr<Content_library>& library, const std::string_view name) -> std::shared_ptr<erhe::primitive::Material>
{
    if (!library) {
        return {};
    }
    for (const std::shared_ptr<erhe::primitive::Material>& material : library->get_all<erhe::primitive::Material>()) {
        if (material->get_name() == name) {
            return material;
        }
    }
    return {};
}

template <typename T>
void add_value_operation(
    const std::shared_ptr<erhe::scene::Node>& node,
    const erhe::property::Property<T>&        property,
    const T&                                  value,
    std::vector<std::shared_ptr<Operation>>&  out_operations
)
{
    if (node->get_value(property) == value) {
        return;
    }
    const std::shared_ptr<Property_set_operation> operation = std::make_shared<Property_set_operation>(
        node,
        property.get(),
        node->read_local_state(property.get()),
        std::optional<erhe::property::Local_state>{erhe::property::Local_state{erhe::property::Property_value{value}}}
    );
    // The entry point adds the generation itself.
    operation->set_follow_ups(Property_set_follow_ups::none);
    out_operations.push_back(operation);
}

// The values other than the lock, written as one operation each where they change.
void add_value_operations(const std::shared_ptr<erhe::scene::Node>& node, const Kit_layout_data& data, std::vector<std::shared_ptr<Operation>>& out_operations)
{
    add_value_operation(node, Kit_layout::pattern_property,        to_canonical_text(data.pattern), out_operations);
    add_value_operation(node, Kit_layout::items_property,          to_canonical_text(std::span<const Kit_layout_item>{data.items}), out_operations);
    add_value_operation(node, Kit_layout::module_snap_property,    data.snap == Kit_snap::module, out_operations);
    add_value_operation(node, Kit_layout::surface_snap_property,   data.surface_snap == Surface_snap::on, out_operations);
    add_value_operation(node, Kit_layout::surface_orient_property, data.surface_orient == Surface_orient::on, out_operations);
    add_value_operation(node, Kit_layout::random_seeds_property,   data.seeds == Kit_layout_seeds::per_element, out_operations);
    add_value_operation(node, Kit_layout::seed_property,           data.seed, out_operations);
}

// The values add_value_operations() writes, in its order.
[[nodiscard]] auto get_layout_value_properties() -> std::span<const erhe::property::Dependency_property* const>
{
    static const std::array<const erhe::property::Dependency_property*, 7> properties{
        &Kit_layout::pattern_property.get(), &Kit_layout::items_property.get(), &Kit_layout::module_snap_property.get(),
        &Kit_layout::surface_snap_property.get(), &Kit_layout::surface_orient_property.get(), &Kit_layout::random_seeds_property.get(),
        &Kit_layout::seed_property.get()
    };
    return properties;
}

// The values other than the lock, written without an operation (a preview step).
void write_layout_values(erhe::scene::Node& node, const Kit_layout_data& data)
{
    node.set_value(Kit_layout::pattern_property,        to_canonical_text(data.pattern));
    node.set_value(Kit_layout::items_property,          to_canonical_text(std::span<const Kit_layout_item>{data.items}));
    node.set_value(Kit_layout::module_snap_property,    data.snap == Kit_snap::module);
    node.set_value(Kit_layout::surface_snap_property,   data.surface_snap == Surface_snap::on);
    node.set_value(Kit_layout::surface_orient_property, data.surface_orient == Surface_orient::on);
    node.set_value(Kit_layout::random_seeds_property,   data.seeds == Kit_layout_seeds::per_element);
    node.set_value(Kit_layout::seed_property,           data.seed);
}

// The length a new curve for the pattern gets: the line its count and step make, else its gap-mode length.
[[nodiscard]] auto get_new_curve_length(const Kit_layout_pattern& pattern) -> float
{
    const float length = (pattern.count_mode == Kit_layout_count_mode::gap)
        ? pattern.length
        : (static_cast<float>(std::max(pattern.count, 2) - 1) * pattern.step);
    return (std::isfinite(length) && (length > 0.0f)) ? length : 1.0f;
}

// D4.9: the straight open path a new path layout gets, two corner points along its x axis.
[[nodiscard]] auto make_straight_path(const float length) -> erhe::math::Curve_path
{
    erhe::math::Curve_path path;
    path.points.push_back(erhe::math::Curve_point{.position = glm::vec3{0.0f}});
    path.points.push_back(erhe::math::Curve_point{.position = glm::vec3{length, 0.0f, 0.0f}});
    return path;
}

// A node of the scene that is a kit layout node and may be edited; returns the refusal.
[[nodiscard]] auto check_layout_node(const Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& node, Kit_layout_data& out_data) -> std::string
{
    if (!node) {
        return "no node";
    }
    if (node->get_item_host() != &scene_root) {
        return fmt::format("node '{}' is not in scene '{}'", node->get_name(), scene_root.get_name());
    }
    const std::string error = read_kit_layout(*node, out_data);
    if (!error.empty()) {
        return error;
    }
    const std::optional<std::string> refusal = instance_structure_refusal(*node);
    return refusal.has_value() ? refusal.value() : std::string{};
}

// The hierarchy's subtree, children before their parents.
void collect_subtree_deepest_first(const std::shared_ptr<erhe::Hierarchy>& hierarchy, std::vector<std::shared_ptr<erhe::Hierarchy>>& out_items)
{
    for (const std::shared_ptr<erhe::Hierarchy>& child : hierarchy->get_children()) {
        collect_subtree_deepest_first(child, out_items);
    }
    out_items.push_back(hierarchy);
}

void collect_meshes(const erhe::Hierarchy& hierarchy, std::vector<const erhe::scene::Mesh*>& out_meshes)
{
    for (const std::shared_ptr<erhe::Hierarchy>& child : hierarchy.get_children()) {
        const erhe::scene::Mesh* const mesh = dynamic_cast<const erhe::scene::Mesh*>(child.get());
        if (mesh != nullptr) {
            out_meshes.push_back(mesh);
        }
        collect_meshes(*child, out_meshes);
    }
}

// The stored transform properties a Property_set_operation of a node move writes.
auto is_node_transform_property(const erhe::property::Dependency_property& property) -> bool
{
    return
        (&property == &erhe::scene::Xformable::translation_property.get()) ||
        (&property == &erhe::scene::Xformable::rotation_property.get()) ||
        (&property == &erhe::scene::Xformable::scale_property.get());
}

} // anonymous namespace

auto Level_kits::read_layout_item(Scene_root& scene_root, const Kit_layout_item& item, Kit_placement& out_placement) const -> std::string
{
    const Kit_shape_definition* const shape = m_library.find(item.shape);
    if (shape == nullptr) {
        return fmt::format("unknown shape '{}' (level_get_kits lists the shapes)", item.shape);
    }
    Kit_parameter_values values{*shape};
    const std::string parse_error = parse_canonical_text(*shape, item.parameters, values);
    if (!parse_error.empty()) {
        return fmt::format("the parameters do not read: {}", parse_error);
    }
    const Kit_lengths lengths = get_lengths(scene_root);
    values.resolve_derived(lengths.module, lengths.wall_height);
    const std::string range_error = values.validate();
    if (!range_error.empty()) {
        return range_error;
    }
    if (item.materials.size() > shape->material_slots.size()) {
        return fmt::format("{} materials for the {} material slots of {}", item.materials.size(), shape->material_slots.size(), shape->id);
    }
    out_placement = Kit_placement{};
    out_placement.shape  = shape;
    out_placement.values = values;
    const std::shared_ptr<Content_library> library = scene_root.get_content_library();
    for (std::size_t slot = 0, end = item.materials.size(); slot < end; ++slot) {
        Kit_slot_material material{};
        const std::string& name = item.materials[slot];
        if (!name.empty()) {
            material.material = find_library_material(library, name);
            if (!material.material) {
                if (m_library.find_palette_material(name) == nullptr) {
                    return fmt::format("slot '{}': no material '{}' in the scene or the kit palette", shape->material_slots[slot], name);
                }
                material.palette_name = name;
            }
        }
        out_placement.materials.push_back(std::move(material));
    }
    return {};
}

auto Level_kits::get_layout_item_bounds(Scene_root& scene_root, const Kit_layout_item& item) -> erhe::math::Aabb
{
    Kit_placement placement;
    if (!read_layout_item(scene_root, item, placement).empty()) {
        return erhe::math::Aabb{};
    }
    const std::shared_ptr<Brush> brush = get_brush(placement.values.value());
    return brush ? brush->get_bounding_box() : erhe::math::Aabb{};
}

namespace {

// Called where a generation is recorded; preview steps do not log.
void log_unsettled_rests(const erhe::scene::Node& layout, const Kit_layout_result& result)
{
    if (result.unsettled_count > 0) {
        log_scene->warn("Kit layout '{}': {} pieces still rose at their last surface cast (unsettled rest recorded)", layout.get_name(), result.unsettled_count);
    }
}

} // anonymous namespace

auto Level_kits::build_layout_generation(
    Scene_root&                                     scene_root,
    const std::shared_ptr<erhe::scene::Node>&       node,
    const Kit_layout_data&                          data,
    const glm::mat4&                                world_from_layout,
    const Kit_layout_reuse                          reuse,
    const std::span<const erhe::scene::Mesh* const> excluded_meshes,
    std::vector<std::shared_ptr<Operation>>&        out_operations,
    Kit_layout_result&                              result
) -> std::string
{
    out_operations.clear();
    result.unsettled_count = 0;
    if (data.items.empty()) {
        return "a kit layout needs at least one item";
    }
    // The items, their brushes (and the per-element seeded ones) held until the pieces hold them, and the largest extent along x (D3.9).
    std::vector<Kit_placement>          items(data.items.size());
    std::vector<std::shared_ptr<Brush>> item_brushes;
    std::vector<erhe::math::Aabb>       item_bounds;
    float                               extent = 0.0f;
    for (std::size_t i = 0, end = data.items.size(); i < end; ++i) {
        const std::string error = read_layout_item(scene_root, data.items[i], items[i]);
        if (!error.empty()) {
            return fmt::format("item {}: {}", i, error);
        }
        std::shared_ptr<Brush> brush = get_brush(items[i].values.value());
        if (!brush) {
            return fmt::format("item {}: {} made no geometry", i, data.items[i].shape);
        }
        const erhe::math::Aabb bounds = brush->get_bounding_box();
        extent = std::max(extent, bounds.max.x - bounds.min.x);
        item_bounds.push_back(bounds);
        item_brushes.push_back(std::move(brush));
    }
    result.item_extent = extent;
    // Every kind runs in the node's own frame; a path along the node's own curve (R5.12.8).
    std::string error = check_layout_kind(*node, data.pattern.kind);
    if (!error.empty()) {
        return error;
    }
    const erhe::scene::Curve* const curve = dynamic_cast<const erhe::scene::Curve*>(node.get());
    std::vector<Repeat_pose> poses;
    error = resolve_layout_pattern(data.pattern, data.seed, extent, (curve != nullptr) ? &curve->get_table() : nullptr, result.run, &poses);
    // The run outlives no curve edit.
    result.run.pattern.path = nullptr;
    if (!error.empty()) {
        return error;
    }

    const erhe::scene::Trs_transform layout_world{world_from_layout};
    const glm::vec3 scale = layout_world.get_scale();
    if (glm::any(glm::greaterThan(glm::abs(scale - glm::vec3{1.0f}), glm::vec3{c_pose_tolerance}))) {
        return fmt::format("'{}' is scaled ({}, {}, {}): a kit layout node keeps scale 1", node->get_name(), scale.x, scale.y, scale.z);
    }
    const glm::vec3   layout_position = layout_world.get_translation();
    const glm::quat   layout_rotation = layout_world.get_rotation();
    const glm::mat4   layout_from_world = glm::inverse(world_from_layout);
    const Kit_lengths lengths = get_lengths(scene_root);
    if ((data.snap == Kit_snap::module) && !(lengths.module > 0.0f)) {
        return fmt::format("snap to module needs a positive module, the scene's is {} m", lengths.module);
    }
    const Surface_options surface{.snap = data.surface_snap, .orient = data.surface_orient};
    // The node's current children are no surface for the new ones.
    std::vector<const erhe::scene::Mesh*> excluded{excluded_meshes.begin(), excluded_meshes.end()};
    collect_meshes(*node, excluded);

    std::vector<Kit_placement> elements;
    elements.reserve(poses.size());
    if (data.seeds == Kit_layout_seeds::per_element) {
        item_brushes.reserve(item_brushes.size() + poses.size());
    }
    for (std::size_t i = 0, end = poses.size(); i < end; ++i) {
        const std::size_t item_index = get_layout_item_index(i, items.size());
        Kit_placement placement = items[item_index];
        if (data.seeds == Kit_layout_seeds::per_element) {
            Kit_parameter_values values = placement.values.value();
            static_cast<void>(values.set_int("seed", derive_layout_element_seed(data.seed, i)));
            values.resolve_derived(lengths.module, lengths.wall_height);
            const std::string range_error = values.validate();
            if (!range_error.empty()) {
                return fmt::format("element {}: {}", i, range_error);
            }
            placement.values = values;
        }
        // A piece rests on its own bounds; a per-element seed makes other geometry than the item's.
        erhe::math::Aabb bounds = item_bounds[item_index];
        if (data.seeds == Kit_layout_seeds::per_element) {
            std::shared_ptr<Brush> brush = get_brush(placement.values.value());
            if (!brush) {
                return fmt::format("element {}: {} made no geometry", i, placement.shape->id);
            }
            bounds = brush->get_bounding_box();
            item_brushes.push_back(std::move(brush));
        }
        glm::vec3 position = layout_position + (layout_rotation * poses[i].position);
        glm::quat rotation = glm::normalize(layout_rotation * poses[i].rotation);
        if (data.snap == Kit_snap::module) {
            position = snap_to_module(position, lengths.module);
        }
        if (any_surface_option(surface)) {
            const Surface_placement rest = find_surface_placement(
                scene_root, bounds, Surface_pose{.position = position, .rotation = rotation}, surface, excluded
            );
            position = rest.pose.position;
            rotation = rest.pose.rotation;
            if (!rest.settled) {
                ++result.unsettled_count;
            }
        }
        placement.position        = position;
        placement.rotation        = rotation;
        placement.parent          = node;
        placement.index_in_parent = i;
        placement.name            = fmt::format("{} {}", placement.shape->display_name, i + 1);
        elements.push_back(std::move(placement));
    }

    // A child equals its element: the same record, materials by name and local pose, and no children of its own.
    const auto matches = [this, &layout_from_world](const erhe::Hierarchy& child, const Kit_placement& element) -> bool {
        const erhe::scene::Mesh* const mesh = dynamic_cast<const erhe::scene::Mesh*>(&child);
        if ((mesh == nullptr) || (mesh->get_child_count() > 0)) {
            return false;
        }
        const std::optional<Kit_piece_data> record = read_kit_piece(*mesh);
        if (!record.has_value() || (record.value().shape != element.shape->id) || (record.value().parameters != element.values.value().to_canonical_text())) {
            return false;
        }
        const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
        const std::vector<std::string>&                 slots      = element.shape->material_slots;
        if (primitives.size() != slots.size()) {
            return false;
        }
        for (std::size_t slot = 0, end = slots.size(); slot < end; ++slot) {
            const Kit_slot_material* const given = (slot < element.materials.size()) ? &element.materials[slot] : nullptr;
            std::string expected;
            if ((given != nullptr) && given->material) {
                expected = given->material->get_name();
            } else if ((given != nullptr) && !given->palette_name.empty()) {
                expected = given->palette_name;
            } else {
                const Kit_palette_material* const palette = m_library.find_slot_default(slots[slot]);
                expected = (palette != nullptr) ? palette->name : std::string{};
            }
            if (!primitives[slot].material || (primitives[slot].material->get_name() != expected)) {
                return false;
            }
        }
        const erhe::scene::Trs_transform desired{
            layout_from_world * erhe::math::create_translation<float>(element.position) * glm::mat4_cast(element.rotation)
        };
        const erhe::scene::Trs_transform& actual = mesh->parent_from_node_transform();
        return
            (glm::length(actual.get_translation() - desired.get_translation()) < c_pose_tolerance) &&
            (std::abs(glm::dot(actual.get_rotation(), desired.get_rotation())) > (1.0f - c_rotation_tolerance)) &&
            !glm::any(glm::greaterThan(glm::abs(actual.get_scale() - glm::vec3{1.0f}), glm::vec3{c_pose_tolerance}));
    };

    const std::vector<std::shared_ptr<erhe::Hierarchy>> children = node->get_children();
    std::vector<bool> kept(children.size(), false);
    if (reuse == Kit_layout_reuse::keep_matching) {
        for (std::size_t i = 0, end = std::min(children.size(), elements.size()); i < end; ++i) {
            kept[i] = matches(*children[i], elements[i]);
        }
    }

    // Removed from the last child back, each subtree deepest first, so an undo puts every node back at its index.
    std::vector<std::shared_ptr<Operation>> operations;
    std::vector<std::shared_ptr<erhe::Hierarchy>> subtree;
    for (std::size_t j = children.size(); j > 0; --j) {
        if (kept[j - 1]) {
            ++result.kept_count;
            continue;
        }
        ++result.removed_count;
        subtree.clear();
        collect_subtree_deepest_first(children[j - 1], subtree);
        for (const std::shared_ptr<erhe::Hierarchy>& item : subtree) {
            operations.push_back(
                std::make_shared<Item_insert_remove_operation>(
                    Item_insert_remove_operation::Parameters{
                        .context = m_context,
                        .item    = item,
                        .parent  = item->get_parent().lock(),
                        .mode    = Item_insert_remove_operation::Mode::remove
                    }
                )
            );
        }
    }
    // Inserted in element order: every earlier element stands at its index by then.
    std::vector<Kit_placement> added;
    for (std::size_t i = 0, end = elements.size(); i < end; ++i) {
        if ((i < kept.size()) && kept[i]) {
            continue;
        }
        added.push_back(elements[i]);
    }
    if (!added.empty()) {
        Kit_place_result                        placed;
        std::vector<std::shared_ptr<Operation>> insert_operations;
        if (!prepare_placements(scene_root, added, Kit_snap::none, placed, insert_operations)) {
            return fmt::format("element {}: {}", added[placed.error_index].index_in_parent.value_or(0), placed.error);
        }
        for (std::shared_ptr<Operation>& operation : insert_operations) {
            operations.push_back(std::move(operation));
        }
    }
    result.added_count = added.size();
    out_operations = std::move(operations);
    return {};
}

auto Level_kits::create_layout(Scene_root& scene_root, const Kit_layout_create& create) -> Kit_layout_result
{
    std::vector<std::shared_ptr<Operation>> no_leading;
    return create_layout_after(scene_root, create, std::nullopt, {}, no_leading);
}

auto Level_kits::create_layout_after(
    Scene_root&                                     scene_root,
    const Kit_layout_create&                        create,
    const std::optional<std::size_t>                index_in_parent,
    const std::span<const erhe::scene::Mesh* const> excluded,
    std::vector<std::shared_ptr<Operation>>&        leading_operations
) -> Kit_layout_result
{
    Kit_layout_result result;
    std::shared_ptr<erhe::Hierarchy> parent = create.parent ? create.parent : scene_root.get_scene().get_root_node();
    if (parent->get_item_host() != &scene_root) {
        result.error = fmt::format("parent '{}' is not in scene '{}'", parent->get_name(), scene_root.get_name());
        return result;
    }
    const std::optional<std::string> refusal = instance_child_refusal(*parent);
    if (refusal.has_value()) {
        result.error = refusal.value();
        return result;
    }
    // R5.12.8: a path layout is a curve node carrying the values, the other kinds an Xform.
    const std::string_view name = create.name.empty() ? std::string_view{"Kit Layout"} : std::string_view{create.name};
    std::shared_ptr<erhe::scene::Node> node;
    if (create.data.pattern.kind == Repeat_pattern_kind::path) {
        std::shared_ptr<erhe::scene::Curve> curve = std::make_shared<erhe::scene::Curve>(name);
        curve->set_path(create.path.has_value() ? create.path.value() : make_straight_path(get_new_curve_length(create.data.pattern)));
        node = curve;
    } else if (create.path.has_value()) {
        result.error = fmt::format("a curve's points are given for a {} layout: only a path layout is a curve node", c_str(create.data.pattern.kind));
        return result;
    } else {
        node = std::make_shared<erhe::scene::Xform>(name);
    }
    node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    const glm::mat4 world_from_node = erhe::math::create_translation<float>(create.position) * glm::mat4_cast(glm::normalize(create.rotation));
    // Detached, the world transform is the local one; the insert keeps it.
    node->set_world_from_node(world_from_node);
    write_kit_layout(*node, create.data);

    std::vector<std::shared_ptr<Operation>> generation;
    result.error = build_layout_generation(scene_root, node, create.data, world_from_node, Kit_layout_reuse::replace_all, excluded, generation, result);
    if (!result.error.empty()) {
        return result;
    }
    log_unsettled_rests(*node, result);
    Compound_operation::Parameters parameters;
    for (std::shared_ptr<Operation>& operation : leading_operations) {
        parameters.operations.push_back(std::move(operation));
    }
    leading_operations.clear();
    parameters.operations.push_back(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context         = m_context,
                .item            = node,
                .parent          = parent,
                .mode            = Item_insert_remove_operation::Mode::insert,
                .index_in_parent = index_in_parent.value_or(parent->get_child_count())
            }
        )
    );
    for (std::shared_ptr<Operation>& operation : generation) {
        parameters.operations.push_back(std::move(operation));
    }
    m_context.operation_stack->execute_now(std::make_shared<Compound_operation>(std::move(parameters)));
    result.node    = node;
    result.changed = true;
    return result;
}

auto Level_kits::read_piece_layout_item(const erhe::scene::Mesh& piece, Kit_layout_item& out_item) const -> std::string
{
    const std::optional<Kit_piece_data> record = read_kit_piece(piece);
    if (!record.has_value()) {
        return fmt::format("'{}' is not a kit piece", piece.get_name());
    }
    Kit_parameter_values values;
    const std::string problem = read_piece_values(record.value(), values);
    if (!problem.empty()) {
        return fmt::format("'{}': {}", piece.get_name(), problem);
    }
    const std::vector<erhe::scene::Mesh_primitive>& primitives = piece.get_primitives();
    if (primitives.size() != values.get_shape()->material_slots.size()) {
        return fmt::format("'{}' has {} mesh primitives for the {} material slots of {}", piece.get_name(), primitives.size(), values.get_shape()->material_slots.size(), record.value().shape);
    }
    out_item = Kit_layout_item{};
    out_item.shape      = record.value().shape;
    out_item.parameters = values.to_canonical_text();
    for (const erhe::scene::Mesh_primitive& primitive : primitives) {
        out_item.materials.push_back(primitive.material ? primitive.material->get_name() : std::string{});
    }
    return {};
}

auto Level_kits::make_layout_from_piece(Scene_root& scene_root, const Kit_layout_from_piece& from_piece) -> Kit_layout_result
{
    Kit_layout_result result;
    const std::shared_ptr<erhe::scene::Mesh>& piece = from_piece.piece;
    if (!piece) {
        result.error = "no piece";
        return result;
    }
    if (piece->get_item_host() != &scene_root) {
        result.error = fmt::format("piece '{}' is not in scene '{}'", piece->get_name(), scene_root.get_name());
        return result;
    }
    const std::shared_ptr<erhe::Hierarchy> parent = piece->get_parent().lock();
    if (!parent) {
        result.error = fmt::format("'{}' has no parent", piece->get_name());
        return result;
    }
    const std::optional<std::string> refusal = instance_structure_refusal(*piece);
    if (refusal.has_value()) {
        result.error = refusal.value();
        return result;
    }
    if (piece->get_child_count() > 0) {
        result.error = fmt::format("'{}' has children; a kit layout node makes its own children", piece->get_name());
        return result;
    }
    const erhe::scene::Trs_transform world = piece->world_from_node_transform();
    if (glm::any(glm::greaterThan(glm::abs(world.get_scale() - glm::vec3{1.0f}), glm::vec3{c_pose_tolerance}))) {
        const glm::vec3 scale = world.get_scale();
        result.error = fmt::format("'{}' is scaled ({}, {}, {}): a kit layout node keeps scale 1", piece->get_name(), scale.x, scale.y, scale.z);
        return result;
    }
    Kit_layout_create create;
    Kit_layout_item   item;
    result.error = read_piece_layout_item(*piece, item);
    if (!result.error.empty()) {
        return result;
    }
    create.data.items.push_back(std::move(item));
    create.data.pattern        = from_piece.pattern;
    create.data.snap           = from_piece.snap;
    create.data.surface_snap   = from_piece.surface_snap;
    create.data.surface_orient = from_piece.surface_orient;
    create.data.seeds          = from_piece.seeds;
    create.data.seed           = from_piece.seed;
    create.data.lock           = from_piece.lock;
    create.path                = from_piece.path;
    create.parent              = parent;
    create.position            = world.get_translation();
    create.rotation            = world.get_rotation();
    create.name                = from_piece.name.empty() ? fmt::format("{} Layout", piece->get_name()) : from_piece.name;

    // The node takes the piece's index; the piece is no surface for the element that replaces it.
    const std::size_t index = piece->get_index_in_parent();
    const std::array<const erhe::scene::Mesh*, 1> excluded{piece.get()};
    std::vector<std::shared_ptr<Operation>> leading;
    leading.push_back(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = piece,
                .parent  = parent,
                .mode    = Item_insert_remove_operation::Mode::remove
            }
        )
    );
    return create_layout_after(scene_root, create, index, excluded, leading);
}

auto Level_kits::make_layout_from_piece_initial(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Mesh>& piece) -> Kit_layout_result
{
    const Kit_layout_initial_values initial = get_layout_initial_values();
    Kit_layout_from_piece from_piece{
        .piece          = piece,
        .pattern        = initial.data.pattern,
        .snap           = initial.data.snap,
        .surface_snap   = initial.data.surface_snap,
        .surface_orient = initial.data.surface_orient,
        .seeds          = initial.data.seeds,
        .seed           = initial.data.seed,
        .lock           = initial.data.lock,
        .name           = {},
        .path           = {}
    };
    if (piece && (initial.spacing == Kit_layout_spacing::from_item)) {
        Kit_layout_item item;
        if (read_piece_layout_item(*piece, item).empty()) {
            const erhe::math::Aabb bounds = get_layout_item_bounds(scene_root, item);
            set_layout_spacing_from_extent(from_piece.pattern, bounds.is_valid_3d() ? (bounds.max - bounds.min) : glm::vec3{0.0f});
        }
    }
    return make_layout_from_piece(scene_root, from_piece);
}

auto Level_kits::add_layout_initial(Scene_root& scene_root, const Kit_layout_item& item, const glm::vec3& position, const glm::quat& rotation) -> Kit_layout_result
{
    const Kit_layout_initial_values initial = get_layout_initial_values();
    Kit_layout_create create;
    create.data = initial.data;
    create.data.items.push_back(item);
    if (initial.spacing == Kit_layout_spacing::from_item) {
        const erhe::math::Aabb bounds = get_layout_item_bounds(scene_root, item);
        set_layout_spacing_from_extent(create.data.pattern, bounds.is_valid_3d() ? (bounds.max - bounds.min) : glm::vec3{0.0f});
    }
    const Kit_shape_definition* const shape = m_library.find(item.shape);
    create.name     = fmt::format("{} Layout", (shape != nullptr) ? shape->display_name : item.shape);
    create.position = position;
    create.rotation = rotation;
    return create_layout(scene_root, create);
}

auto Level_kits::set_layout(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& node, const Kit_layout_data& data) -> Kit_layout_result
{
    Kit_layout_result result;
    // A live preview of the node gives its state from before the drag back first, so the entry starts there.
    if (node) {
        end_layout_preview_of(*node);
    }
    Kit_layout_data   current;
    result.error = check_layout_node(scene_root, node, current);
    if (!result.error.empty()) {
        return result;
    }
    result.node = node;
    if (data.lock != current.lock) {
        result.error = "the lock is set by its own operation (level_set_layout_lock)";
        return result;
    }
    std::vector<std::shared_ptr<Operation>> operations;
    add_value_operations(node, data, operations);
    std::vector<std::shared_ptr<Operation>> generation;
    if (current.lock == Kit_layout_lock::locked) {
        result.error = build_layout_generation(scene_root, node, data, node->world_from_node(), Kit_layout_reuse::keep_matching, {}, generation, result);
    } else {
        // Unlocked, the values change alone; they are checked as a generation would read them.
        Kit_layout_result check;
        result.error = build_layout_generation(scene_root, node, data, node->world_from_node(), Kit_layout_reuse::replace_all, {}, generation, check);
        result.run         = check.run;
        result.item_extent = check.item_extent;
        generation.clear();
    }
    if (!result.error.empty()) {
        return result;
    }
    log_unsettled_rests(*node, result);
    for (std::shared_ptr<Operation>& operation : generation) {
        operations.push_back(std::move(operation));
    }
    if (operations.empty()) {
        return result;
    }
    m_context.operation_stack->execute_now(std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)}));
    result.changed = true;
    return result;
}

auto Level_kits::set_layout_lock(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& node, const Kit_layout_lock lock) -> Kit_layout_result
{
    Kit_layout_result result;
    // A live preview of the node gives its state from before the drag back first, so the entry starts there.
    if (node) {
        end_layout_preview_of(*node);
    }
    Kit_layout_data   current;
    result.error = check_layout_node(scene_root, node, current);
    if (!result.error.empty()) {
        return result;
    }
    result.node = node;
    if (lock == current.lock) {
        return result;
    }
    std::vector<std::shared_ptr<Operation>> operations;
    add_value_operation(node, Kit_layout::locked_property, lock == Kit_layout_lock::locked, operations);
    if (lock == Kit_layout_lock::locked) {
        // Locking discards the edits made while unlocked: every child is replaced (R5.12.5).
        Kit_layout_data locked = current;
        locked.lock = Kit_layout_lock::locked;
        std::vector<std::shared_ptr<Operation>> generation;
        result.error = build_layout_generation(scene_root, node, locked, node->world_from_node(), Kit_layout_reuse::replace_all, {}, generation, result);
        if (!result.error.empty()) {
            return result;
        }
        log_unsettled_rests(*node, result);
        for (std::shared_ptr<Operation>& operation : generation) {
            operations.push_back(std::move(operation));
        }
    }
    m_context.operation_stack->execute_now(
        (operations.size() == 1)
            ? operations.front()
            : std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)})
    );
    result.changed = true;
    return result;
}

auto Level_kits::refresh_layout(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& node) -> Kit_layout_result
{
    Kit_layout_result result;
    // A live preview of the node gives its state from before the drag back first, so the entry starts there.
    if (node) {
        end_layout_preview_of(*node);
    }
    Kit_layout_data   current;
    result.error = check_layout_node(scene_root, node, current);
    if (!result.error.empty()) {
        return result;
    }
    result.node = node;
    if (current.lock != Kit_layout_lock::locked) {
        result.error = fmt::format("'{}' is unlocked: it leaves its children alone; lock it to make them from its values", node->get_name());
        return result;
    }
    std::vector<std::shared_ptr<Operation>> operations;
    result.error = build_layout_generation(scene_root, node, current, node->world_from_node(), Kit_layout_reuse::keep_matching, {}, operations, result);
    if (!result.error.empty() || operations.empty()) {
        return result;
    }
    log_unsettled_rests(*node, result);
    m_context.operation_stack->execute_now(std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)}));
    result.changed = true;
    return result;
}

auto Level_kits::append_layout_value_follow_ups(
    erhe::Item_base&                           item,
    const erhe::property::Dependency_property& property,
    std::vector<std::shared_ptr<Operation>>&   out_operations
) -> std::string
{
    if (!is_kit_layout_property(property)) {
        return {};
    }
    erhe::scene::Node* const node = dynamic_cast<erhe::scene::Node*>(&item);
    Scene_root* const scene_root = (node != nullptr) ? dynamic_cast<Scene_root*>(node->get_item_host()) : nullptr;
    if ((scene_root == nullptr) || !carries_kit_layout(*node)) {
        return {};
    }
    Kit_layout_data data;
    const std::string read_error = read_kit_layout(*node, data);
    if (!read_error.empty()) {
        return read_error;
    }
    if (data.lock != Kit_layout_lock::locked) {
        return {};
    }
    // Writing the lock locks as set_layout_lock() does; any other value regenerates as set_layout() does.
    const Kit_layout_reuse reuse = (&property == &Kit_layout::locked_property.get()) ? Kit_layout_reuse::replace_all : Kit_layout_reuse::keep_matching;
    const std::shared_ptr<erhe::scene::Node> shared_node = std::dynamic_pointer_cast<erhe::scene::Node>(node->shared_from_this());
    Kit_layout_result                       result;
    std::vector<std::shared_ptr<Operation>> generation;
    const std::string error = build_layout_generation(*scene_root, shared_node, data, node->world_from_node(), reuse, {}, generation, result);
    if (!error.empty()) {
        return error;
    }
    log_unsettled_rests(*node, result);
    for (std::shared_ptr<Operation>& operation : generation) {
        out_operations.push_back(std::move(operation));
    }
    return {};
}

auto Level_kits::check_layout_value_write(
    erhe::Item_base&                           item,
    const erhe::property::Dependency_property& property,
    const erhe::property::Property_value&      value
) -> std::string
{
    erhe::scene::Node* const node = dynamic_cast<erhe::scene::Node*>(&item);
    Scene_root* const scene_root = (node != nullptr) ? dynamic_cast<Scene_root*>(node->get_item_host()) : nullptr;
    if (!is_kit_layout_property(property) || (scene_root == nullptr) || !carries_kit_layout(*node)) {
        return {};
    }
    Kit_layout_data data;
    std::string error = read_kit_layout(*node, data);
    if (!error.empty()) {
        return error;
    }
    const std::string* const text    = std::get_if<std::string>(&value);
    const bool*        const flag    = std::get_if<bool>(&value);
    const int*         const integer = std::get_if<int>(&value);
    if      ((&property == &Kit_layout::pattern_property.get())        && (text != nullptr))    { error = parse_layout_pattern(*text, data.pattern); }
    else if ((&property == &Kit_layout::items_property.get())          && (text != nullptr))    { error = parse_layout_items(*text, data.items); }
    else if ((&property == &Kit_layout::module_snap_property.get())    && (flag != nullptr))    { data.snap = *flag ? Kit_snap::module : Kit_snap::none; }
    else if ((&property == &Kit_layout::surface_snap_property.get())   && (flag != nullptr))    { data.surface_snap = *flag ? Surface_snap::on : Surface_snap::off; }
    else if ((&property == &Kit_layout::surface_orient_property.get()) && (flag != nullptr))    { data.surface_orient = *flag ? Surface_orient::on : Surface_orient::off; }
    else if ((&property == &Kit_layout::random_seeds_property.get())   && (flag != nullptr))    { data.seeds = *flag ? Kit_layout_seeds::per_element : Kit_layout_seeds::item; }
    else if ((&property == &Kit_layout::seed_property.get())           && (integer != nullptr)) { data.seed = *integer; }
    else if ((&property == &Kit_layout::locked_property.get())         && (flag != nullptr))    { data.lock = *flag ? Kit_layout_lock::locked : Kit_layout_lock::unlocked; }
    else { error = "the value is not of the property's type"; }
    if (error.empty()) {
        error = check_layout_kind(*node, data.pattern.kind);
    }
    if (!error.empty()) {
        return error;
    }
    if (data.lock != Kit_layout_lock::locked) {
        return {};
    }
    const std::shared_ptr<erhe::scene::Node> shared_node = std::dynamic_pointer_cast<erhe::scene::Node>(node->shared_from_this());
    Kit_layout_result                       result;
    std::vector<std::shared_ptr<Operation>> unused;
    return build_layout_generation(*scene_root, shared_node, data, node->world_from_node(), Kit_layout_reuse::replace_all, {}, unused, result);
}

void Level_kits::append_layout_regenerations(
    Scene_root&                                               scene_root,
    const std::span<const std::shared_ptr<erhe::scene::Node>> layouts,
    const char* const                                         reason,
    std::vector<std::shared_ptr<Operation>>&                  out_operations
)
{
    if (!layouts.empty()) {
        // Moved this frame: the layouts' descendants take the move before their rays are cast, as in a preview step.
        scene_root.get_scene().update_node_transforms();
    }
    for (const std::shared_ptr<erhe::scene::Node>& layout : layouts) {
        Kit_layout_data data;
        if (!read_kit_layout(*layout, data).empty() || (data.lock != Kit_layout_lock::locked)) {
            continue;
        }
        Kit_layout_result                       result;
        std::vector<std::shared_ptr<Operation>> generation;
        const std::string error = build_layout_generation(
            scene_root, layout, data, layout->world_from_node(), Kit_layout_reuse::keep_matching, {}, generation, result
        );
        if (!error.empty()) {
            log_scene->warn("Kit layout '{}' keeps its children {}: {}", layout->get_name(), reason, error);
            continue;
        }
        log_unsettled_rests(*layout, result);
        for (std::shared_ptr<Operation>& operation : generation) {
            out_operations.push_back(std::move(operation));
        }
    }
}

void Level_kits::append_layout_move_operations(
    Scene_root&                                               scene_root,
    const std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes,
    std::vector<std::shared_ptr<Operation>>&                  out_operations
)
{
    std::vector<std::shared_ptr<erhe::scene::Node>> layouts;
    collect_moved_layouts(scene_root, moved_nodes, layouts);
    append_layout_regenerations(scene_root, layouts, "after the move", out_operations);
}

void Level_kits::append_layout_transform_follow_ups(
    const std::span<const std::shared_ptr<erhe::Item_base>> items,
    const erhe::property::Dependency_property&              property,
    std::vector<std::shared_ptr<Operation>>&                out_operations
)
{
    if (!is_node_transform_property(property)) {
        return;
    }
    // One hook per scene, so every generation is built before any runs and a layout below two of the nodes is built once.
    std::vector<Scene_root*>                        scene_roots;
    std::vector<std::shared_ptr<erhe::scene::Node>> nodes;
    for (const std::shared_ptr<erhe::Item_base>& item : items) {
        const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        Scene_root* const scene_root = (node != nullptr) ? dynamic_cast<Scene_root*>(node->get_item_host()) : nullptr;
        if ((scene_root != nullptr) && (std::find(scene_roots.begin(), scene_roots.end(), scene_root) == scene_roots.end())) {
            scene_roots.push_back(scene_root);
        }
    }
    for (Scene_root* const scene_root : scene_roots) {
        nodes.clear();
        for (const std::shared_ptr<erhe::Item_base>& item : items) {
            std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
            if ((node != nullptr) && (node->get_item_host() == scene_root)) {
                nodes.push_back(std::move(node));
            }
        }
        append_layout_move_operations(*scene_root, nodes, out_operations);
    }
}

void Level_kits::append_curve_follow_ups(const erhe::scene::Curve& curve, std::vector<std::shared_ptr<Operation>>& out_operations)
{
    Scene_root* const scene_root = dynamic_cast<Scene_root*>(curve.get_item_host());
    if ((scene_root == nullptr) || !is_locked_kit_layout(curve)) {
        return;
    }
    const std::array<std::shared_ptr<erhe::scene::Node>, 1> layouts{
        std::dynamic_pointer_cast<erhe::scene::Node>(std::const_pointer_cast<erhe::Item_base>(curve.shared_from_this()))
    };
    append_layout_regenerations(*scene_root, layouts, "after the curve edit", out_operations);
}

void Level_kits::begin_layout_preview(const std::shared_ptr<erhe::scene::Node>& layout)
{
    begin_layout_preview(std::span<const std::shared_ptr<erhe::scene::Node>>{&layout, 1});
}

void Level_kits::begin_layout_preview(const std::span<const std::shared_ptr<erhe::scene::Node>> layouts)
{
    end_layout_preview();
    for (const std::shared_ptr<erhe::scene::Node>& layout : layouts) {
        if (!layout) {
            continue;
        }
        Layout_preview_entry& entry = m_layout_preview.emplace_back();
        entry.node              = layout;
        entry.selected_at_begin = layout->is_selected();
    }
}

void Level_kits::collect_moved_layouts(
    Scene_root&                                               scene_root,
    const std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes,
    std::vector<std::shared_ptr<erhe::scene::Node>>&          out_layouts
) const
{
    out_layouts.clear();
    std::set<const erhe::scene::Node*> visited;
    const std::function<void(const std::shared_ptr<erhe::scene::Node>&)> visit =
        [&visit, &visited, &out_layouts](const std::shared_ptr<erhe::scene::Node>& node) {
            if (!visited.insert(node.get()).second) {
                return;
            }
            if (is_locked_kit_layout(*node)) {
                Kit_layout_data data;
                // The children ride with the node; a surface setting rests them again.
                if (read_kit_layout(*node, data).empty() && any_surface_option(Surface_options{.snap = data.surface_snap, .orient = data.surface_orient})) {
                    out_layouts.push_back(node);
                }
                return; // its children are its pieces
            }
            for (const std::shared_ptr<erhe::Hierarchy>& child : node->get_children()) {
                const std::shared_ptr<erhe::scene::Node> child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
                if (child_node) {
                    visit(child_node);
                }
            }
        };
    for (const std::shared_ptr<erhe::scene::Node>& node : moved_nodes) {
        if (node && (node->get_item_host() == &scene_root)) {
            visit(node);
        }
    }
}

void Level_kits::begin_layout_move_preview(Scene_root& scene_root, const std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes)
{
    collect_moved_layouts(scene_root, moved_nodes, m_layout_preview_layouts);
    begin_layout_preview(m_layout_preview_layouts);
    m_layout_preview_layouts.clear();
}

void Level_kits::begin_layout_transform_preview(
    const std::span<const std::shared_ptr<erhe::Item_base>> items,
    const erhe::property::Dependency_property&              property
)
{
    if (!is_node_transform_property(property)) {
        return;
    }
    // One scene: the preview's entries go with their scene when it closes (on_close_scene()).
    Scene_root*                                     scene_root = nullptr;
    std::vector<std::shared_ptr<erhe::scene::Node>>& nodes     = m_layout_preview_collected;
    nodes.clear();
    for (const std::shared_ptr<erhe::Item_base>& item : items) {
        std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        Scene_root* const node_scene_root = (node != nullptr) ? dynamic_cast<Scene_root*>(node->get_item_host()) : nullptr;
        if (node_scene_root == nullptr) {
            continue;
        }
        if ((scene_root != nullptr) && (node_scene_root != scene_root)) {
            nodes.clear();
            return;
        }
        scene_root = node_scene_root;
        nodes.push_back(std::move(node));
    }
    if (scene_root == nullptr) {
        return;
    }
    begin_layout_move_preview(*scene_root, nodes);
    nodes.clear();
    m_layout_transform_preview = !m_layout_preview.empty();
}

void Level_kits::step_layout_transform_preview()
{
    if (m_layout_transform_preview) {
        static_cast<void>(step_layout_preview());
    }
}

void Level_kits::end_layout_transform_preview()
{
    if (m_layout_transform_preview) {
        end_layout_preview();
    }
}

auto Level_kits::is_layout_previewing(const erhe::scene::Node& node) const -> bool
{
    for (const Layout_preview_entry& entry : m_layout_preview) {
        if (entry.node.get() == &node) {
            return true;
        }
    }
    return false;
}

auto Level_kits::step_layout_preview() -> std::string
{
    return step_layout_preview_entries(nullptr, nullptr);
}

auto Level_kits::preview_layout_values(const std::shared_ptr<erhe::scene::Node>& layout, const Kit_layout_data& data, Kit_layout_result& out_result) -> std::string
{
    out_result = Kit_layout_result{};
    if (!layout) {
        return "no node";
    }
    Scene_root* const scene_root = dynamic_cast<Scene_root*>(layout->get_item_host());
    if (scene_root == nullptr) {
        return fmt::format("'{}' is not in a scene", layout->get_name());
    }
    Kit_layout_data current;
    std::string error = check_layout_node(*scene_root, layout, current);
    if (!error.empty()) {
        return error;
    }
    if (data.lock != current.lock) {
        return "the lock is set by its own operation (level_set_layout_lock)";
    }
    if ((m_layout_preview.size() != 1) || (m_layout_preview.front().node != layout)) {
        begin_layout_preview(layout);
    }
    error = step_layout_preview_entries(layout.get(), &data);
    out_result = m_layout_preview.front().result;
    return error;
}

auto Level_kits::step_layout_preview_entries(const erhe::scene::Node* const values_node, const Kit_layout_data* const values) -> std::string
{
    if (m_layout_preview.empty()) {
        return {};
    }
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    hold_selection();
    // The previous step's pieces are no surface for this step's, as the layout's children are none for the release's.
    m_layout_preview_excluded.clear();
    for (const Layout_preview_entry& entry : m_layout_preview) {
        collect_meshes(*entry.node, m_layout_preview_excluded);
    }
    // Each step lays out again from the children before the drag; every generation is built before any runs, as the
    // move hook builds them at the release.
    for (auto i = m_layout_preview.rbegin(), end = m_layout_preview.rend(); i != end; ++i) {
        for (auto j = i->operations.rbegin(), operations_end = i->operations.rend(); j != operations_end; ++j) {
            (*j)->undo(m_context);
        }
    }
    // The node's move and the children given back reach their descendants' world transforms and rays in this pass.
    for (const Layout_preview_entry& entry : m_layout_preview) {
        Scene_root* const scene_root = dynamic_cast<Scene_root*>(entry.node->get_item_host());
        if (scene_root != nullptr) {
            scene_root->get_scene().update_node_transforms();
        }
    }
    std::string first_refusal;
    for (Layout_preview_entry& entry : m_layout_preview) {
        entry.generation.clear();
        entry.refusal.clear();
        entry.built  = false;
        entry.result = Kit_layout_result{};
        Scene_root* const scene_root = dynamic_cast<Scene_root*>(entry.node->get_item_host());
        Kit_layout_data   data;
        const bool        given = (values != nullptr) && (entry.node.get() == values_node);
        if (scene_root == nullptr) {
            entry.refusal = fmt::format("'{}' is not in a scene", entry.node->get_name());
        } else if (given) {
            data = *values;
        } else {
            entry.refusal = read_kit_layout(*entry.node, data);
        }
        if (!entry.refusal.empty() || (data.lock != Kit_layout_lock::locked)) {
            continue; // an unlocked layout keeps its children
        }
        entry.refusal = build_layout_generation(
            *scene_root, entry.node, data, entry.node->world_from_node(), Kit_layout_reuse::keep_matching, m_layout_preview_excluded, entry.generation, entry.result
        );
        entry.built = entry.refusal.empty();
        if (entry.built && given) {
            if (entry.values_before.empty()) {
                for (const erhe::property::Dependency_property* property : get_layout_value_properties()) {
                    entry.values_before.push_back(entry.node->read_local_state(*property));
                }
            }
            write_layout_values(*entry.node, data);
        }
    }
    std::size_t piece_count = 0;
    for (Layout_preview_entry& entry : m_layout_preview) {
        if (entry.built) {
            for (const std::shared_ptr<Operation>& operation : entry.generation) {
                operation->execute(m_context);
            }
            // The undone operations held the previous pieces alive through the generation.
            entry.operations.swap(entry.generation);
            entry.generation.clear();
        } else {
            // A refused step keeps the last valid one.
            for (const std::shared_ptr<Operation>& operation : entry.operations) {
                operation->execute(m_context);
            }
            if (!entry.refusal.empty()) {
                log_scene->trace("Kit layout '{}' keeps its last previewed children: {}", entry.node->get_name(), entry.refusal);
                if (first_refusal.empty()) {
                    first_refusal = entry.refusal;
                }
            }
        }
        piece_count += entry.node->get_child_count();
    }
    restore_held_selection();
    const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
    log_scene->trace("layout preview step: {} layouts, {} pieces: {:.3f} ms", m_layout_preview.size(), piece_count, elapsed.count());
    return first_refusal;
}

void Level_kits::end_layout_preview()
{
    m_layout_transform_preview = false;
    if (m_layout_preview.empty()) {
        return;
    }
    hold_selection();
    for (auto i = m_layout_preview.rbegin(), end = m_layout_preview.rend(); i != end; ++i) {
        for (auto j = i->operations.rbegin(), operations_end = i->operations.rend(); j != operations_end; ++j) {
            (*j)->undo(m_context);
        }
        if (!i->values_before.empty()) {
            const std::span<const erhe::property::Dependency_property* const> properties = get_layout_value_properties();
            for (std::size_t k = 0, count = properties.size(); k < count; ++k) {
                static_cast<void>(i->node->apply_local_state(*properties[k], i->values_before[k]));
            }
        }
    }
    m_layout_preview.clear();
    restore_held_selection();
}

void Level_kits::hold_selection()
{
    m_held_selection   = m_context.selection->get_selected_items();
    m_held_active_item = m_context.selection->get_active_item();
}

void Level_kits::restore_held_selection()
{
    if ((m_context.selection->get_selected_items() != m_held_selection) || (m_context.selection->get_active_item() != m_held_active_item)) {
        m_context.selection->set_selection(m_held_selection, m_held_active_item);
    }
    m_held_selection.clear();
    m_held_active_item.reset();
}

void Level_kits::end_layout_preview_of(const erhe::scene::Node& node)
{
    if (is_layout_previewing(node)) {
        end_layout_preview();
    }
}

auto Level_kits::resolve_layout_run(
    const erhe::scene::Node& layout,
    const Kit_layout_data&   data,
    const float              item_extent,
    Kit_layout_run&          out_run
) const -> std::string
{
    out_run = Kit_layout_run{};
    const std::string error = check_layout_kind(layout, data.pattern.kind);
    if (!error.empty()) {
        return error;
    }
    const erhe::scene::Curve* const curve = dynamic_cast<const erhe::scene::Curve*>(&layout);
    const std::string run_error = resolve_layout_pattern(data.pattern, data.seed, item_extent, (curve != nullptr) ? &curve->get_table() : nullptr, out_run, nullptr);
    out_run.pattern.path = nullptr;
    return run_error;
}

auto Level_kits::make_layout_on_curve(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Curve>& curve, const Kit_layout_data& data) -> Kit_layout_result
{
    Kit_layout_result result;
    if (!curve) {
        result.error = "no curve";
        return result;
    }
    if (curve->get_item_host() != &scene_root) {
        result.error = fmt::format("curve '{}' is not in scene '{}'", curve->get_name(), scene_root.get_name());
        return result;
    }
    if (carries_kit_layout(*curve)) {
        result.error = fmt::format("'{}' is a kit layout node already (level_set_layout edits its values)", curve->get_name());
        return result;
    }
    const std::optional<std::string> refusal = instance_structure_refusal(*curve);
    if (refusal.has_value()) {
        result.error = refusal.value();
        return result;
    }
    if (curve->get_child_count() > 0) {
        result.error = fmt::format("'{}' has children; a kit layout node makes its own children", curve->get_name());
        return result;
    }
    if (data.pattern.kind != Repeat_pattern_kind::path) {
        result.error = fmt::format("a curve node carries a path layout only, not a {} layout", c_str(data.pattern.kind));
        return result;
    }
    result.node = curve;
    // The values as one operation each, the key (the pattern) first, then the children a locked layout makes.
    std::vector<std::shared_ptr<Operation>> operations;
    add_value_operations(curve, data, operations);
    add_value_operation(curve, Kit_layout::locked_property, data.lock == Kit_layout_lock::locked, operations);
    // Unlocked, the values are checked as a generation would read them and make no child.
    std::vector<std::shared_ptr<Operation>> generation;
    result.error = build_layout_generation(scene_root, curve, data, curve->world_from_node(), Kit_layout_reuse::replace_all, {}, generation, result);
    if (!result.error.empty()) {
        return result;
    }
    if (data.lock == Kit_layout_lock::locked) {
        log_unsettled_rests(*curve, result);
        for (std::shared_ptr<Operation>& operation : generation) {
            operations.push_back(std::move(operation));
        }
    } else {
        result.added_count = 0;
    }
    m_context.operation_stack->execute_now(std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)}));
    result.changed = true;
    return result;
}

} // namespace editor
