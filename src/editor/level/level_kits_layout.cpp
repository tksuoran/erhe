// Kit layout nodes of Level_kits (D3.9, doc/editor/level_tools.md "Kit layout nodes").

#include "level/level_kits.hpp"

#include "level/kit_layout.hpp"
#include "level/kit_piece.hpp"
#include "operations/curve_set_operation.hpp"
#include "level/kit_piece_light.hpp"

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

#include "erhe_geometry/geometry.hpp"
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
#include <unordered_map>
#include <utility>

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

// A raw write of the support rule: one of the enumerators (U52), else the refusal.
[[nodiscard]] auto read_surface_support(const erhe::property::Enum_value enumerator, Surface_support& out_support) -> std::string
{
    switch (static_cast<Surface_support>(enumerator.value)) {
        case Surface_support::five_lines:
        case Surface_support::center_blocks:
        case Surface_support::center_fallback: {
            out_support = static_cast<Surface_support>(enumerator.value);
            return {};
        }
        default: {
            return fmt::format("surface support {} is none of five_lines, center_blocks, center_fallback", enumerator.value);
        }
    }
}

// Whether a value equal to the node's is written anyway: a node saved before U51 reads a distance it does not hold from
// its legacy value, so writing that distance takes it off the fallback.
enum class Kit_value_write : unsigned int {
    when_changed = 0,
    when_not_local
};

template <typename T>
void add_value_operation(
    const std::shared_ptr<erhe::scene::Node>& node,
    const erhe::property::Property<T>&        property,
    const T&                                  value,
    std::vector<std::shared_ptr<Operation>>&  out_operations,
    const Kit_value_write                     write = Kit_value_write::when_changed
)
{
    const bool local = node->read_local_value(property).has_value();
    if ((node->get_value(property) == value) && ((write == Kit_value_write::when_changed) || local)) {
        return;
    }
    const std::shared_ptr<Property_set_operation> operation = std::make_shared<Property_set_operation>(
        node,
        property.get(),
        node->read_local_state(property.get()),
        std::optional<erhe::property::Local_state>{erhe::property::Local_state{erhe::property::make_value(value)}}
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
    add_value_operation(node, Kit_layout::surface_direction_property,         data.surface_direction, out_operations);
    const Kit_value_write distance_write = node->read_local_value(Kit_layout::legacy_surface_max_distance_property).has_value()
        ? Kit_value_write::when_not_local
        : Kit_value_write::when_changed;
    add_value_operation(node, Kit_layout::surface_forward_distance_property,  data.surface_forward_distance,  out_operations, distance_write);
    add_value_operation(node, Kit_layout::surface_backward_distance_property, data.surface_backward_distance, out_operations, distance_write);
    add_value_operation(node, Kit_layout::surface_support_property,           data.surface_support,           out_operations);
    add_value_operation(node, Kit_layout::per_piece_seed_property,   data.seeds == Kit_layout_seeds::per_element, out_operations);
    add_value_operation(node, Kit_layout::seed_property,           data.seed, out_operations);
    add_value_operation(node, Kit_layout::hide_all_connectors_property, data.connector_hiding.all == Kit_layout_connector_visibility::hidden, out_operations);
    for (std::size_t i = 0; i < c_layout_direction_count; ++i) {
        const Kit_layout_direction direction = static_cast<Kit_layout_direction>(i);
        add_value_operation(node, Kit_layout::get_hide_direction_property(direction), data.connector_hiding.is_hidden(direction), out_operations);
    }
}

// The values add_value_operations() writes, in its order.
[[nodiscard]] auto get_layout_value_properties() -> std::span<const erhe::property::Dependency_property* const>
{
    static const std::array<const erhe::property::Dependency_property*, 18> properties{
        &Kit_layout::pattern_property.get(), &Kit_layout::items_property.get(), &Kit_layout::module_snap_property.get(),
        &Kit_layout::surface_snap_property.get(), &Kit_layout::surface_orient_property.get(),
        &Kit_layout::surface_direction_property.get(), &Kit_layout::surface_forward_distance_property.get(), &Kit_layout::surface_backward_distance_property.get(),
        &Kit_layout::surface_support_property.get(),
        &Kit_layout::per_piece_seed_property.get(), &Kit_layout::seed_property.get(),
        &Kit_layout::hide_all_connectors_property.get(),
        &Kit_layout::hide_plus_tangent_connectors_property.get(), &Kit_layout::hide_minus_tangent_connectors_property.get(),
        &Kit_layout::hide_plus_normal_connectors_property.get(), &Kit_layout::hide_minus_normal_connectors_property.get(),
        &Kit_layout::hide_plus_binormal_connectors_property.get(), &Kit_layout::hide_minus_binormal_connectors_property.get()
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
    node.set_value(Kit_layout::surface_direction_property,         data.surface_direction);
    node.set_value(Kit_layout::surface_forward_distance_property,  data.surface_forward_distance);
    node.set_value(Kit_layout::surface_backward_distance_property, data.surface_backward_distance);
    node.set_value(Kit_layout::surface_support_property,           data.surface_support);
    node.set_value(Kit_layout::per_piece_seed_property,   data.seeds == Kit_layout_seeds::per_element);
    node.set_value(Kit_layout::seed_property,           data.seed);
    write_layout_connector_hiding(node, data.connector_hiding);
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

// Two corner points on the x axis.
[[nodiscard]] auto make_x_path(const float x0, const float x1) -> erhe::math::Curve_path
{
    erhe::math::Curve_path path;
    path.points.push_back(erhe::math::Curve_point{.position = glm::vec3{x0, 0.0f, 0.0f}});
    path.points.push_back(erhe::math::Curve_point{.position = glm::vec3{x1, 0.0f, 0.0f}});
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
    return get_kit_geometry_bounds(placement.values.value());
}

void Level_kits::get_layout_item_sizes(Scene_root& scene_root, const std::span<const Kit_layout_item> items, Kit_layout_item_sizes& out_sizes)
{
    out_sizes.clear();
    for (const Kit_layout_item& item : items) {
        out_sizes.add(item, get_layout_item_bounds(scene_root, item));
    }
}

void Kit_layout_preparation::clear()
{
    items.clear();
    bounds.clear();
    sizes.clear();
    run = Kit_layout_run{};
    poses.clear();
    element_values.clear();
}

auto is_layout_scale_kept(const glm::mat4& world_from_layout) -> bool
{
    const glm::vec3 scale = erhe::scene::Trs_transform{world_from_layout}.get_scale();
    return !glm::any(glm::greaterThan(glm::abs(scale - glm::vec3{1.0f}), glm::vec3{c_pose_tolerance}));
}

auto Level_kits::prepare_layout_generation(
    Scene_root&              scene_root,
    const erhe::scene::Node& node,
    const Kit_layout_data&   data,
    const glm::mat4&         world_from_layout,
    Kit_layout_preparation&  out_preparation
) -> std::string
{
    out_preparation.clear();
    // A stairs layout places no item: its child is generated from the curve (R5.12.13).
    if ((data.pattern.kind == Repeat_pattern_kind::stairs) || (data.pattern.kind == Repeat_pattern_kind::floor)) {
        std::string stairs_error = check_layout_kind(node, data.pattern.kind);
        if (stairs_error.empty()) {
            const erhe::scene::Curve* const stairs_curve = dynamic_cast<const erhe::scene::Curve*>(&node);
            stairs_error = resolve_layout_pattern(data.pattern, data.seed, out_preparation.sizes, &stairs_curve->get_table(), out_preparation.run, &out_preparation.poses);
        }
        if (stairs_error.empty() && !is_layout_scale_kept(world_from_layout)) {
            const glm::vec3 scale = erhe::scene::Trs_transform{world_from_layout}.get_scale();
            stairs_error = fmt::format("'{}' is scaled ({}, {}, {}): a kit layout node keeps scale 1", node.get_name(), scale.x, scale.y, scale.z);
        }
        return stairs_error;
    }
    if (data.items.empty()) {
        return "a kit layout needs at least one item";
    }
    for (const std::string& surface_error : {
        check_surface_direction(data.surface_direction),
        check_surface_max_distance(data.surface_forward_distance, "surface forward distance"),
        check_surface_max_distance(data.surface_backward_distance, "surface backward distance")
    }) {
        if (!surface_error.empty()) {
            return surface_error;
        }
    }
    // The items, their bounds from the CPU geometry and the largest extent along x (D3.9).
    out_preparation.items.resize(data.items.size());
    for (std::size_t i = 0, end = data.items.size(); i < end; ++i) {
        const std::string error = read_layout_item(scene_root, data.items[i], out_preparation.items[i]);
        if (!error.empty()) {
            return fmt::format("item {}: {}", i, error);
        }
        const erhe::math::Aabb bounds = get_kit_geometry_bounds(out_preparation.items[i].values.value());
        if (!bounds.is_valid_3d()) {
            return fmt::format("item {}: {} made no geometry", i, data.items[i].shape);
        }
        out_preparation.sizes.add(data.items[i], bounds);
        out_preparation.bounds.push_back(bounds);
    }
    // Every kind runs in the node's own frame; a path along the node's own curve (R5.12.8).
    std::string error = check_layout_kind(node, data.pattern.kind);
    if (!error.empty()) {
        return error;
    }
    const erhe::scene::Curve* const curve = dynamic_cast<const erhe::scene::Curve*>(&node);
    error = resolve_layout_pattern(
        data.pattern, data.seed, out_preparation.sizes, (curve != nullptr) ? &curve->get_table() : nullptr, out_preparation.run, &out_preparation.poses
    );
    // The run outlives no curve edit and no item edit.
    out_preparation.run.pattern.path          = nullptr;
    out_preparation.run.pattern.scatter_items = {};
    out_preparation.run.pattern.segment_items = {};
    if (!error.empty()) {
        return error;
    }
    if (!is_layout_scale_kept(world_from_layout)) {
        const glm::vec3 scale = erhe::scene::Trs_transform{world_from_layout}.get_scale();
        return fmt::format("'{}' is scaled ({}, {}, {}): a kit layout node keeps scale 1", node.get_name(), scale.x, scale.y, scale.z);
    }
    const Kit_lengths lengths = get_lengths(scene_root);
    if ((data.snap == Kit_snap::module) && !(lengths.module > 0.0f)) {
        return fmt::format("snap to module needs a positive module, the scene's is {} m", lengths.module);
    }
    const bool segments = (data.pattern.kind == Repeat_pattern_kind::segments);
    if ((data.seeds != Kit_layout_seeds::per_element) && !segments) {
        return {};
    }
    for (std::size_t i = 0, end = out_preparation.poses.size(); i < end; ++i) {
        Kit_parameter_values values = out_preparation.items[out_preparation.poses[i].item].values.value();
        if (data.seeds == Kit_layout_seeds::per_element) {
            static_cast<void>(values.set_int("seed", derive_layout_element_seed(data.seed, i)));
            values.resolve_derived(lengths.module, lengths.wall_height);
        }
        // L24: a segments child takes its segment's length through child.length, when its shape maps it.
        if (segments) {
            const erhe::math::Curve_span& span = curve->get_table().get_span(i);
            const Kit_shape_definition* const shape = values.get_shape();
            for (const Kit_property_mapping& mapping : shape->property_mappings) {
                const std::optional<std::size_t> index = shape->find_parameter(mapping.target);
                if ((mapping.property == Kit_standard_property::child_length) && index.has_value() &&
                    values.set_float(mapping.target, get_segment_span_length(span.control[0], span.control[3], data.pattern.orientation))) {
                    values.set_value_source(index.value(), Kit_value_source::explicit_value);
                }
            }
        }
        std::string range_error = values.validate();
        if (range_error.empty() && values.get_shape()->check_combination) {
            range_error = values.get_shape()->check_combination(values); // a segment too short for a door wall's opening
        }
        if (!range_error.empty()) {
            return fmt::format("element {}: {}", i, range_error);
        }
        // A segments piece is centred on its chord by the bounds of its own geometry, which its values change.
        if (segments) {
            const erhe::math::Aabb element_bounds = get_kit_geometry_bounds(values);
            const erhe::math::Curve_span& span = curve->get_table().get_span(i);
            const Repeat_segment_item item = element_bounds.is_valid_3d()
                ? Repeat_segment_item{.min_x = element_bounds.min.x, .max_x = element_bounds.max.x}
                : Repeat_segment_item{};
            Repeat_pose& pose = out_preparation.poses[i];
            const std::size_t item_index = pose.item;
            const std::string segment_error = make_segment_pose(i, span.control[0], span.control[3], item, data.pattern.orientation, pose);
            if (!segment_error.empty()) {
                return segment_error;
            }
            pose.item = item_index;
        }
        out_preparation.element_values.push_back(std::move(values));
    }
    return {};
}

namespace {

// Releases a persistent preparation's items (materials) and values at the end of the scope; the capacity stays.
class Preparation_release
{
public:
    explicit Preparation_release(Kit_layout_preparation& preparation) : m_preparation{preparation} {}
    ~Preparation_release() noexcept { m_preparation.clear(); }
    Preparation_release(const Preparation_release&) = delete;
    auto operator=(const Preparation_release&) -> Preparation_release& = delete;

private:
    Kit_layout_preparation& m_preparation;
};

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
    if (data.pattern.kind == Repeat_pattern_kind::stairs) {
        return build_stairs_generation(scene_root, node, data, world_from_layout, reuse, out_operations, result);
    }
    if (data.pattern.kind == Repeat_pattern_kind::floor) {
        return build_floor_generation(scene_root, node, data, world_from_layout, reuse, out_operations, result);
    }
    Kit_layout_preparation&   prepared = m_generation_preparation;
    const Preparation_release release{prepared};
    const std::string error = prepare_layout_generation(scene_root, *node, data, world_from_layout, prepared);
    result.item_extent = prepared.sizes.extent;
    result.run         = prepared.run;
    if (!error.empty()) {
        return error;
    }
    // The items' brushes, held until the pieces hold them.
    std::vector<std::shared_ptr<Brush>> item_brushes;
    item_brushes.reserve(prepared.items.size() + (((data.seeds == Kit_layout_seeds::per_element) || (data.pattern.kind == Repeat_pattern_kind::segments)) ? prepared.poses.size() : 0));
    for (std::size_t i = 0, end = prepared.items.size(); i < end; ++i) {
        std::shared_ptr<Brush> brush = get_brush(prepared.items[i].values.value());
        if (!brush) {
            return fmt::format("item {}: {} made no geometry", i, data.items[i].shape);
        }
        item_brushes.push_back(std::move(brush));
    }
    const erhe::scene::Trs_transform layout_world{world_from_layout};
    const glm::vec3   layout_position   = layout_world.get_translation();
    const glm::quat   layout_rotation   = layout_world.get_rotation();
    const glm::mat4   layout_from_world = glm::inverse(world_from_layout);
    const Kit_lengths lengths = get_lengths(scene_root);
    const bool            segments = (data.pattern.kind == Repeat_pattern_kind::segments);
    Surface_options element_surface = get_layout_surface_options(data);
    if (segments) {
        element_surface.orient = Surface_orient::off;
    }
    // The node's current children are no surface for the new ones.
    std::vector<const erhe::scene::Mesh*> excluded{excluded_meshes.begin(), excluded_meshes.end()};
    collect_meshes(*node, excluded);

    const std::vector<Repeat_pose>& poses = prepared.poses;
    std::vector<Kit_placement> elements;
    elements.reserve(poses.size());
    for (std::size_t i = 0, end = poses.size(); i < end; ++i) {
        const std::size_t item_index = poses[i].item;
        Kit_placement placement = prepared.items[item_index];
        // A piece snaps by its own bounds; a per-element seed makes other geometry than the item's.
        erhe::math::Aabb bounds = prepared.bounds[item_index];
        if ((data.seeds == Kit_layout_seeds::per_element) || segments) {
            placement.values = prepared.element_values[i];
            std::shared_ptr<Brush> brush = get_brush(placement.values.value());
            if (!brush) {
                return fmt::format("element {}: {} made no geometry", i, placement.shape->id);
            }
            bounds = brush->get_bounding_box();
            item_brushes.push_back(std::move(brush));
        }
        glm::vec3 position = layout_position + (layout_rotation * poses[i].position);
        glm::quat rotation = glm::normalize(layout_rotation * poses[i].rotation);
        // A segments piece stands on its control points: no module snap, and no orient, which would tilt it off them.
        if ((data.snap == Kit_snap::module) && !segments) {
            position = snap_to_module(position, lengths.module);
        }
        if (any_surface_option(element_surface)) {
            const Surface_placement snapped = find_surface_placement(
                scene_root, bounds, Surface_pose{.position = position, .rotation = rotation, .scale = poses[i].scale}, element_surface, excluded
            );
            position = snapped.pose.position;
            rotation = snapped.pose.rotation;
        }
        placement.position        = position;
        placement.rotation        = rotation;
        placement.scale           = poses[i].scale;
        placement.parent          = node;
        placement.index_in_parent = i;
        placement.name            = fmt::format("{} {}", placement.shape->display_name, i + 1);
        elements.push_back(std::move(placement));
    }

    // A child equals its element: the same record, materials by name and local pose, and no children of its own but its
    // kit lights (D3.15), which a kept child has put on its frames below.
    const auto matches = [this, &layout_from_world](const erhe::Hierarchy& child, const Kit_placement& element) -> bool {
        const erhe::scene::Mesh* const mesh = dynamic_cast<const erhe::scene::Mesh*>(&child);
        if ((mesh == nullptr) || !has_only_kit_light_children(*mesh)) {
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
        // The layout keeps scale 1, so the element's scale is its local scale.
        const erhe::scene::Trs_transform desired{
            layout_from_world * erhe::math::create_translation<float>(element.position) * glm::mat4_cast(element.rotation)
        };
        const erhe::scene::Trs_transform& actual = mesh->parent_from_node_transform();
        return
            (glm::length(actual.get_translation() - desired.get_translation()) < c_pose_tolerance) &&
            (std::abs(glm::dot(actual.get_rotation(), desired.get_rotation())) > (1.0f - c_rotation_tolerance)) &&
            !glm::any(glm::greaterThan(glm::abs(actual.get_scale() - element.scale), glm::vec3{c_pose_tolerance}));
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
    // A kept child's lights go to its frames, as every regeneration puts them (D3.15).
    for (std::size_t i = 0, end = std::min(children.size(), elements.size()); i < end; ++i) {
        if (kept[i]) {
            m_light_sync.append(m_context, scene_root, std::dynamic_pointer_cast<erhe::scene::Node>(children[i]), elements[i].values.value(), operations);
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

auto Level_kits::build_stairs_generation(
    Scene_root&                               scene_root,
    const std::shared_ptr<erhe::scene::Node>& node,
    const Kit_layout_data&                    data,
    const glm::mat4&                          world_from_layout,
    const Kit_layout_reuse                    reuse,
    std::vector<std::shared_ptr<Operation>>&  out_operations,
    Kit_layout_result&                        result
) -> std::string
{
    std::string error = check_layout_kind(*node, data.pattern.kind);
    if (!error.empty()) {
        return error;
    }
    if (!is_layout_scale_kept(world_from_layout)) {
        const glm::vec3 scale = erhe::scene::Trs_transform{world_from_layout}.get_scale();
        return fmt::format("'{}' is scaled ({}, {}, {}): a kit layout node keeps scale 1", node->get_name(), scale.x, scale.y, scale.z);
    }
    error = check_stairs_material(scene_root, data.pattern.stairs_material);
    if (!error.empty()) {
        return error;
    }
    // Treads are level in the node's frame, so the node stays upright; a turn about the up axis is free.
    const glm::vec3 layout_up = erhe::scene::Trs_transform{world_from_layout}.get_rotation() * glm::vec3{0.0f, 1.0f, 0.0f};
    if (layout_up.y < (1.0f - c_rotation_tolerance)) {
        return fmt::format(
            "'{}' is tilted (its up axis is ({:.3f}, {:.3f}, {:.3f})): a stairs layout node stays upright, so its treads are level",
            node->get_name(), layout_up.x, layout_up.y, layout_up.z
        );
    }
    const erhe::scene::Curve& curve = dynamic_cast<const erhe::scene::Curve&>(*node);
    result.run           = Kit_layout_run{};
    result.run.count     = 1;
    result.run.requested = 1;

    // The one child is kept when it was generated from this curve and these values with this material, at the node's origin.
    const std::string source = make_stairs_source_text(curve.get_path(), data.pattern.stairs);
    const std::vector<std::shared_ptr<erhe::Hierarchy>> children = node->get_children();
    bool kept = false;
    if ((reuse == Kit_layout_reuse::keep_matching) && (children.size() == 1)) {
        const erhe::scene::Mesh* const mesh = dynamic_cast<const erhe::scene::Mesh*>(children.front().get());
        const Kit_palette_material* const palette = m_library.find_slot_default(c_stairs_material_slot);
        const std::string_view expected_material = !data.pattern.stairs_material.empty()
            ? std::string_view{data.pattern.stairs_material}
            : ((palette != nullptr) ? std::string_view{palette->name} : std::string_view{});
        if (
            (mesh != nullptr) && (mesh->get_child_count() == 0) && (read_layout_generated_source(*mesh) == source) &&
            (mesh->get_primitives().size() == 1) && mesh->get_primitives().front().material &&
            (mesh->get_primitives().front().material->get_name() == expected_material)
        ) {
            const erhe::scene::Trs_transform& local = mesh->parent_from_node_transform();
            kept =
                (glm::length(local.get_translation()) < c_pose_tolerance) &&
                (std::abs(local.get_rotation().w) > (1.0f - c_rotation_tolerance)) &&
                !glm::any(glm::greaterThan(glm::abs(local.get_scale() - glm::vec3{1.0f}), glm::vec3{c_pose_tolerance}));
        }
    }
    // A kept child needs the fit alone (the run and the readback); a new one its mesh.
    const std::shared_ptr<erhe::geometry::Geometry> geometry = kept ? std::shared_ptr<erhe::geometry::Geometry>{} : std::make_shared<erhe::geometry::Geometry>("Stairs");
    error = make_stairs(curve.get_table(), data.pattern.stairs, result.stairs, geometry ? &geometry->get_mesh() : nullptr);
    if (!error.empty()) {
        return error;
    }
    result.run.space = result.stairs.fit.length;
    if (kept) {
        result.kept_count = 1;
        return {};
    }
    // Removed from the last child back, each subtree deepest first, so an undo puts every node back at its index.
    std::vector<std::shared_ptr<Operation>>       operations;
    std::vector<std::shared_ptr<erhe::Hierarchy>> subtree;
    for (std::size_t j = children.size(); j > 0; --j) {
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
    error = make_stairs_insert(scene_root, node, world_from_layout, geometry, data.pattern.stairs_material, source, operations);
    if (!error.empty()) {
        return error;
    }
    result.added_count = 1;
    out_operations = std::move(operations);
    return {};
}

auto Level_kits::build_floor_generation(
    Scene_root&                               scene_root,
    const std::shared_ptr<erhe::scene::Node>& node,
    const Kit_layout_data&                    data,
    const glm::mat4&                          world_from_layout,
    const Kit_layout_reuse                    reuse,
    std::vector<std::shared_ptr<Operation>>&  out_operations,
    Kit_layout_result&                        result
) -> std::string
{
    std::string error = check_layout_kind(*node, data.pattern.kind);
    if (!error.empty()) {
        return error;
    }
    if (!is_layout_scale_kept(world_from_layout)) {
        const glm::vec3 scale = erhe::scene::Trs_transform{world_from_layout}.get_scale();
        return fmt::format("'{}' is scaled ({}, {}, {}): a kit layout node keeps scale 1", node->get_name(), scale.x, scale.y, scale.z);
    }
    const glm::vec3 layout_up = erhe::scene::Trs_transform{world_from_layout}.get_rotation() * glm::vec3{0.0f, 1.0f, 0.0f};
    if (layout_up.y < (1.0f - c_rotation_tolerance)) {
        return fmt::format("'{}' is tilted: a floor layout node stays upright, so its walls stand upright", node->get_name());
    }
    error = check_slot_material_name(scene_root, c_floor_material_slot, data.pattern.floor_material, "floor material");
    if (!error.empty()) {
        return error;
    }
    const erhe::scene::Curve& curve = dynamic_cast<const erhe::scene::Curve&>(*node);
    std::vector<glm::vec2> outline;
    error = read_floor_outline(curve.get_table(), outline);
    if (!error.empty()) {
        return fmt::format("'{}': {}", node->get_name(), error);
    }
    result.run           = Kit_layout_run{};
    result.run.count     = 1;
    result.run.requested = 1;

    const std::string source = make_floor_source_text(outline, data.pattern.floor);
    const std::vector<std::shared_ptr<erhe::Hierarchy>> children = node->get_children();
    if ((reuse == Kit_layout_reuse::keep_matching) && (children.size() == 1)) {
        const erhe::scene::Mesh* const mesh = dynamic_cast<const erhe::scene::Mesh*>(children.front().get());
        const Kit_palette_material* const palette = m_library.find_slot_default(c_floor_material_slot);
        const std::string_view expected_material = !data.pattern.floor_material.empty()
            ? std::string_view{data.pattern.floor_material}
            : ((palette != nullptr) ? std::string_view{palette->name} : std::string_view{});
        if (
            (mesh != nullptr) && (mesh->get_child_count() == 0) && (read_layout_generated_source(*mesh) == source) &&
            (mesh->get_primitives().size() == 1) && mesh->get_primitives().front().material &&
            (mesh->get_primitives().front().material->get_name() == expected_material)
        ) {
            result.kept_count = 1;
            return check_floor_outline(outline);
        }
    }
    const std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("Floor");
    error = make_floor_mesh(outline, data.pattern.floor, geometry->get_mesh());
    if (!error.empty()) {
        return error;
    }
    std::vector<std::shared_ptr<Operation>>       operations;
    std::vector<std::shared_ptr<erhe::Hierarchy>> subtree;
    for (std::size_t j = children.size(); j > 0; --j) {
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
    error = make_generated_child_insert(
        Generated_child{.name = "Floor", .slot = c_floor_material_slot, .what = "floor material", .crease_degrees = 30.0f},
        scene_root, node, world_from_layout, geometry, data.pattern.floor_material, source, operations
    );
    if (!error.empty()) {
        return error;
    }
    result.added_count = 1;
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
    if (is_curve_layout_kind(create.data.pattern.kind)) {
        std::shared_ptr<erhe::scene::Curve> curve = std::make_shared<erhe::scene::Curve>(name);
        if (create.path.has_value()) {
            curve->set_path(create.path.value());
        } else if (create.data.pattern.kind == Repeat_pattern_kind::segments) {
            // One segment spanning the first item at scale 1, so element 0 stands at the node's origin.
            const erhe::math::Aabb bounds = create.data.items.empty() ? erhe::math::Aabb{} : get_layout_item_bounds(scene_root, create.data.items.front());
            curve->set_path(
                bounds.is_valid_3d()
                    ? make_x_path(bounds.min.x, bounds.max.x)
                    : make_straight_path(1.0f)
            );
        } else if (create.data.pattern.kind == Repeat_pattern_kind::stairs) {
            // A flight of ten steps at the values' slope along the x axis.
            erhe::math::Curve_path flight;
            flight.points.push_back(erhe::math::Curve_point{.position = glm::vec3{0.0f}});
            flight.points.push_back(
                erhe::math::Curve_point{.position = glm::vec3{10.0f * create.data.pattern.stairs.min_tread, 10.0f * create.data.pattern.stairs.max_rise, 0.0f}}
            );
            curve->set_path(flight);
        } else if (create.data.pattern.kind == Repeat_pattern_kind::floor) {
            // C8.2: a rectangle of four modules square.
            const float side = 4.0f * get_lengths(scene_root).module;
            std::vector<glm::vec2> rectangle;
            make_floor_rectangle(side, side, rectangle);
            curve->set_path(make_floor_path(rectangle));
        } else {
            curve->set_path(make_straight_path(get_new_curve_length(create.data.pattern)));
        }
        node = curve;
    } else if (create.path.has_value()) {
        result.error = fmt::format("a curve's points are given for a {} layout: only a path, segments or stairs layout is a curve node", c_str(create.data.pattern.kind));
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
    out_item.shape      = values.get_shape()->id;
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
    if (from_piece.pattern.kind == Repeat_pattern_kind::stairs) {
        result.error = "a stairs layout places no piece: it is made on a curve (level_create_layout with points or curve_id)";
        return result;
    }
    if (from_piece.pattern.kind == Repeat_pattern_kind::floor) {
        result.error = "a floor layout places no piece: it is made on a closed curve (level_create_layout with points and closed, or curve_id)";
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
    if (!has_only_kit_light_children(*piece)) {
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
    Kit_layout_item   layout_item;
    result.error = read_piece_layout_item(*piece, layout_item);
    if (!result.error.empty()) {
        return result;
    }
    create.data.items.push_back(std::move(layout_item));
    create.data.pattern        = from_piece.pattern;
    create.data.snap           = from_piece.snap;
    create.data.surface_snap   = from_piece.surface_snap;
    create.data.surface_orient = from_piece.surface_orient;
    create.data.surface_direction         = from_piece.surface_direction;
    create.data.surface_forward_distance  = from_piece.surface_forward_distance;
    create.data.surface_backward_distance = from_piece.surface_backward_distance;
    create.data.surface_support           = from_piece.surface_support;
    create.data.seeds          = from_piece.seeds;
    create.data.seed           = from_piece.seed;
    create.data.lock           = from_piece.lock;
    create.data.connector_hiding = from_piece.connector_hiding;
    create.path                = from_piece.path;
    create.parent              = parent;
    create.position            = world.get_translation();
    create.rotation            = world.get_rotation();
    create.name                = from_piece.name.empty() ? fmt::format("{} Layout", piece->get_name()) : from_piece.name;

    // The node takes the piece's index; the piece is no surface for the element that replaces it.
    const std::size_t index = piece->get_index_in_parent();
    const std::array<const erhe::scene::Mesh*, 1> excluded{piece.get()};
    // The piece goes with its kit lights (a remove alone would move them to its parent); the elements make their own.
    std::vector<std::shared_ptr<Operation>>       leading;
    std::vector<std::shared_ptr<erhe::Hierarchy>> subtree;
    collect_subtree_deepest_first(piece, subtree);
    for (const std::shared_ptr<erhe::Hierarchy>& item : subtree) {
        leading.push_back(
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
        .surface_direction         = initial.data.surface_direction,
        .surface_forward_distance  = initial.data.surface_forward_distance,
        .surface_backward_distance = initial.data.surface_backward_distance,
        .surface_support           = initial.data.surface_support,
        .seeds          = initial.data.seeds,
        .seed           = initial.data.seed,
        .lock           = initial.data.lock,
        .connector_hiding = initial.data.connector_hiding,
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

namespace {

// The child indices from the scene's root node down to the item; pre-order is their lexicographic order.
void get_hierarchy_path(const erhe::Hierarchy& item, std::vector<std::size_t>& out_path)
{
    out_path.clear();
    const erhe::Hierarchy* current = &item;
    for (;;) {
        const std::shared_ptr<erhe::Hierarchy> parent = current->get_parent().lock();
        if (!parent) {
            break;
        }
        out_path.push_back(current->get_index_in_parent());
        current = parent.get();
    }
    std::reverse(out_path.begin(), out_path.end());
}

} // anonymous namespace

auto Level_kits::prepare_wrap_with_path(Scene_root& scene_root, const std::span<const std::shared_ptr<erhe::scene::Node>> nodes, Kit_wrap_plan& out_plan) -> std::string
{
    out_plan.pieces.clear();
    out_plan.items.clear();
    out_plan.extent = 0.0f;
    if (nodes.empty()) {
        return "nothing to wrap: select kit pieces";
    }
    for (const std::shared_ptr<erhe::scene::Node>& node : nodes) {
        if (!node) {
            return "no node";
        }
        if (node->get_item_host() != &scene_root) {
            return fmt::format("'{}' is not in scene '{}'", node->get_name(), scene_root.get_name());
        }
        std::shared_ptr<erhe::scene::Mesh> mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(node);
        if (!mesh || !carries_kit_piece(*node)) {
            return fmt::format("'{}' is not a kit piece: Wrap with Path wraps kit pieces", node->get_name());
        }
        const std::optional<std::string> refusal = instance_structure_refusal(*node);
        if (refusal.has_value()) {
            return refusal.value();
        }
        if (!has_only_kit_light_children(*node)) {
            return fmt::format("'{}' has children other than its kit lights; move them off the piece first", node->get_name());
        }
        const erhe::scene::Trs_transform world = node->world_from_node_transform();
        if (glm::any(glm::greaterThan(glm::abs(world.get_scale() - glm::vec3{1.0f}), glm::vec3{c_pose_tolerance}))) {
            const glm::vec3 scale = world.get_scale();
            return fmt::format("'{}' is scaled ({}, {}, {}): a kit layout node keeps scale 1", node->get_name(), scale.x, scale.y, scale.z);
        }
        // A layout item carries no connector switches (its pieces have all of them on), so switched-off ones would be lost.
        const Kit_connector_switches switches = read_piece_connector_switches(*node);
        if (!switches.connectors_enabled || !switches.disabled_connectors.empty()) {
            return fmt::format("'{}' has connectors switched off, which a path layout's items do not carry; switch them on first", node->get_name());
        }
        if (std::find(out_plan.pieces.begin(), out_plan.pieces.end(), mesh) == out_plan.pieces.end()) {
            out_plan.pieces.push_back(std::move(mesh));
        }
    }
    // Hierarchy order: the child indices from the scene's root node down, compared lexicographically (pre-order).
    std::vector<std::pair<std::vector<std::size_t>, std::shared_ptr<erhe::scene::Mesh>>> ordered;
    ordered.reserve(out_plan.pieces.size());
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : out_plan.pieces) {
        std::vector<std::size_t> path;
        get_hierarchy_path(*mesh, path);
        ordered.emplace_back(std::move(path), mesh);
    }
    std::sort(
        ordered.begin(), ordered.end(),
        [](const std::pair<std::vector<std::size_t>, std::shared_ptr<erhe::scene::Mesh>>& lhs, const std::pair<std::vector<std::size_t>, std::shared_ptr<erhe::scene::Mesh>>& rhs) {
            return lhs.first < rhs.first;
        }
    );
    for (std::size_t i = 0, end = ordered.size(); i < end; ++i) {
        out_plan.pieces[i] = ordered[i].second;
    }
    const std::shared_ptr<erhe::scene::Mesh>& first = out_plan.pieces.front();
    const std::shared_ptr<erhe::Hierarchy> parent = first->get_parent().lock();
    if (!parent) {
        return fmt::format("'{}' has no parent", first->get_name());
    }
    // An unlocked layout's children are its pieces; a lock would replace a path layout nested there.
    const erhe::scene::Node* const parent_node = dynamic_cast<const erhe::scene::Node*>(parent.get());
    if ((parent_node != nullptr) && carries_kit_layout(*parent_node)) {
        return fmt::format(
            "'{}' is a child of the unlocked kit layout node '{}': a path layout put there would be replaced when the layout is locked; "
            "move the piece out of the layout first", first->get_name(), parent_node->get_name()
        );
    }
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : out_plan.pieces) {
        Kit_layout_item item;
        const std::string problem = read_piece_layout_item(*mesh, item);
        if (!problem.empty()) {
            return problem;
        }
        if (!append_distinct_layout_item(out_plan.items, item)) {
            continue;
        }
        const erhe::math::Aabb bounds = get_layout_item_bounds(scene_root, item);
        if (!bounds.is_valid_3d() || !(bounds.max.x > bounds.min.x)) {
            return fmt::format("'{}' has no extent along its x axis, the axis a path layout steps along", mesh->get_name());
        }
        out_plan.extent = std::max(out_plan.extent, bounds.max.x - bounds.min.x);
    }
    if (out_plan.items.size() > c_max_repeat_count) {
        return fmt::format("{} distinct pieces: a path layout holds at most {} pieces", out_plan.items.size(), c_max_repeat_count);
    }
    return {};
}

auto Level_kits::find_wrap_with_path_refusal(Scene_root& scene_root, const std::span<const std::shared_ptr<erhe::scene::Node>> nodes) -> std::optional<std::string>
{
    Kit_wrap_plan plan;
    std::string error = prepare_wrap_with_path(scene_root, nodes, plan);
    if (error.empty()) {
        return std::nullopt;
    }
    return error;
}

auto Level_kits::wrap_with_path(Scene_root& scene_root, const std::span<const std::shared_ptr<erhe::scene::Node>> nodes) -> Kit_layout_result
{
    Kit_layout_result result;
    Kit_wrap_plan     plan;
    result.error = prepare_wrap_with_path(scene_root, nodes, plan);
    if (!result.error.empty()) {
        return result;
    }
    const std::shared_ptr<erhe::scene::Mesh>& first = plan.pieces.front();
    const erhe::scene::Trs_transform world = first->world_from_node_transform();
    Kit_layout_create create;
    create.data.pattern = make_wrap_path_pattern();
    create.data.items   = std::move(plan.items);
    create.path         = make_straight_path(get_wrap_path_length(plan.extent, create.data.items.size()));
    create.parent       = first->get_parent().lock();
    create.position     = world.get_translation();
    create.rotation     = world.get_rotation();
    create.name         = fmt::format("{} Path", first->get_name());

    // Removed last first, each with its kit lights (the elements make their own); the first piece's index stays valid.
    const std::size_t index = first->get_index_in_parent();
    std::vector<const erhe::scene::Mesh*>         excluded;
    std::vector<std::shared_ptr<Operation>>       leading;
    std::vector<std::shared_ptr<erhe::Hierarchy>> subtree;
    for (std::size_t i = plan.pieces.size(); i > 0; --i) {
        const std::shared_ptr<erhe::scene::Mesh>& piece = plan.pieces[i - 1];
        excluded.push_back(piece.get());
        subtree.clear();
        collect_subtree_deepest_first(piece, subtree);
        for (const std::shared_ptr<erhe::Hierarchy>& item : subtree) {
            leading.push_back(
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
    result = create_layout_after(scene_root, create, index, excluded, leading);
    if (result.error.empty() && result.node && (m_context.selection != nullptr)) {
        m_context.selection->set_selection({result.node}, result.node);
    }
    return result;
}

auto Level_kits::find_layout_parent_refusal(const erhe::Hierarchy& parent, const Repeat_pattern_kind kind) -> const char*
{
    const erhe::scene::Node* const node = dynamic_cast<const erhe::scene::Node*>(&parent);
    if (node == nullptr) {
        return "a kit layout node goes under a node";
    }
    if (carries_kit_layout(*node)) {
        return "the parent is a kit layout node: it lays out its own children, and a lock would replace a layout nested in it";
    }
    const erhe::scene::Trs_transform world = node->world_from_node_transform();
    if (glm::any(glm::greaterThan(glm::abs(world.get_scale() - glm::vec3{1.0f}), glm::vec3{c_pose_tolerance}))) {
        return "the parent is scaled: a kit layout node keeps world scale 1";
    }
    if ((kind == Repeat_pattern_kind::stairs) || (kind == Repeat_pattern_kind::floor)) {
        const glm::vec3 up = world.get_rotation() * glm::vec3{0.0f, 1.0f, 0.0f};
        if (up.y < (1.0f - c_rotation_tolerance)) {
            return (kind == Repeat_pattern_kind::stairs)
                ? "the parent is tilted: a stairs layout node stays upright, so its treads are level"
                : "the parent is tilted: a floor layout node stays upright, so its walls stand upright";
        }
    }
    return nullptr;
}

auto Level_kits::make_initial_layout_data(Scene_root& scene_root, const Repeat_pattern_kind kind, const std::optional<Kit_layout_item>& item) -> Kit_layout_data
{
    const Kit_layout_initial_values initial = get_layout_initial_values();
    Kit_layout_data data = initial.data;
    data.pattern.kind = kind;
    data.items.clear();
    if ((kind == Repeat_pattern_kind::stairs) || (kind == Repeat_pattern_kind::floor)) {
        return data;
    }
    if (item.has_value()) {
        data.items.push_back(item.value());
    } else {
        const Kit_shape_definition* const block = m_library.find("basic.block");
        if (block != nullptr) {
            Kit_layout_item block_item;
            block_item.shape      = block->id;
            block_item.parameters = make_default_values(scene_root, *block).to_canonical_text();
            block_item.materials.resize(block->material_slots.size());
            data.items.push_back(std::move(block_item));
        }
    }
    if ((initial.spacing == Kit_layout_spacing::from_item) && !data.items.empty()) {
        const erhe::math::Aabb bounds = get_layout_item_bounds(scene_root, data.items.front());
        set_layout_spacing_from_extent(data.pattern, bounds.is_valid_3d() ? (bounds.max - bounds.min) : glm::vec3{0.0f});
    }
    return data;
}

auto Level_kits::add_layout_of_kind(
    Scene_root&                             scene_root,
    const Repeat_pattern_kind               kind,
    const std::shared_ptr<erhe::Hierarchy>& parent,
    const glm::vec3&                        position,
    const glm::quat&                        rotation,
    const std::optional<Kit_layout_item>&   item
) -> Kit_layout_result
{
    Kit_layout_result result;
    const std::shared_ptr<erhe::Hierarchy> target_parent = parent ? parent : scene_root.get_scene().get_root_node();
    const char* const refusal = find_layout_parent_refusal(*target_parent, kind);
    if (refusal != nullptr) {
        result.error = refusal;
        return result;
    }
    Kit_layout_create create;
    create.data     = make_initial_layout_data(scene_root, kind, item);
    create.parent   = target_parent;
    create.position = position;
    create.rotation = rotation;
    create.name     = get_layout_kind_create_label(kind);
    result = create_layout(scene_root, create);
    if (result.error.empty() && result.node && (m_context.selection != nullptr)) {
        m_context.selection->set_selection({result.node}, result.node);
    }
    return result;
}

auto Level_kits::make_initial_layout_on_curve(
    Scene_root&                                scene_root,
    const std::shared_ptr<erhe::scene::Curve>& curve,
    const Repeat_pattern_kind                  kind,
    const std::optional<Kit_layout_item>&      item
) -> Kit_layout_result
{
    Kit_layout_result result = make_layout_on_curve(scene_root, curve, make_initial_layout_data(scene_root, kind, item));
    if (result.error.empty() && result.node && (m_context.selection != nullptr)) {
        m_context.selection->set_selection({result.node}, result.node);
    }
    return result;
}

auto Level_kits::add_layout_initial(
    Scene_root&            scene_root,
    const Kit_layout_item& item,
    const glm::vec3&       position,
    const glm::quat&       rotation,
    const glm::vec3&       surface_direction
) -> Kit_layout_result
{
    const Kit_layout_initial_values initial = get_layout_initial_values();
    Kit_layout_create create;
    create.data = initial.data;
    create.data.surface_direction = surface_direction;
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
        result.stairs      = check.stairs;
        generation.clear();
    }
    if (!result.error.empty()) {
        return result;
    }
    for (std::shared_ptr<Operation>& operation : generation) {
        operations.push_back(std::move(operation));
    }
    if (operations.empty()) {
        return result;
    }
    // A floor's values (its thickness, its wall thickness) move its edge connectors: its walls follow and take values.
    if (is_floor_layout(*node) || (data.pattern.kind == Repeat_pattern_kind::floor)) {
        std::shared_ptr<Operation> follow = make_attachment_follow_operation(scene_root, {}, std::span<const std::shared_ptr<erhe::scene::Node>>{&node, 1});
        if (follow) {
            operations.push_back(std::move(follow));
        }
    }
    result.error   = execute_entry(std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)}));
    result.changed = result.error.empty();
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
    result.error   = execute_entry(std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)}));
    result.changed = result.error.empty();
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
    // The connector switches change no child (U39): the connector readers follow Kit_layout_changed_message.
    if ((data.lock != Kit_layout_lock::locked) || is_layout_connector_switch_property(property)) {
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
    const float*       const number  = std::get_if<float>(&value);
    const glm::vec3*   const vector  = std::get_if<glm::vec3>(&value);
    const erhe::property::Enum_value* const enumerator = std::get_if<erhe::property::Enum_value>(&value);
    if      ((&property == &Kit_layout::pattern_property.get())        && (text != nullptr))    { error = parse_layout_pattern(*text, data.pattern); }
    else if ((&property == &Kit_layout::items_property.get())          && (text != nullptr))    { error = parse_layout_items(*text, data.items); }
    else if ((&property == &Kit_layout::module_snap_property.get())    && (flag != nullptr))    { data.snap = *flag ? Kit_snap::module : Kit_snap::none; }
    else if ((&property == &Kit_layout::surface_snap_property.get())   && (flag != nullptr))    { data.surface_snap = *flag ? Surface_snap::on : Surface_snap::off; }
    else if ((&property == &Kit_layout::surface_orient_property.get()) && (flag != nullptr))    { data.surface_orient = *flag ? Surface_orient::on : Surface_orient::off; }
    else if ((&property == &Kit_layout::surface_direction_property.get()) && (vector != nullptr)) { error = check_surface_direction(*vector); data.surface_direction = *vector; }
    else if ((&property == &Kit_layout::surface_forward_distance_property.get()) && (number != nullptr)) { error = check_surface_max_distance(*number, "surface forward distance"); data.surface_forward_distance = *number; }
    else if ((&property == &Kit_layout::surface_backward_distance_property.get()) && (number != nullptr)) { error = check_surface_max_distance(*number, "surface backward distance"); data.surface_backward_distance = *number; }
    else if ((&property == &Kit_layout::surface_support_property.get()) && (enumerator != nullptr)) { error = read_surface_support(*enumerator, data.surface_support); }
    else if ((&property == &Kit_layout::per_piece_seed_property.get())   && (flag != nullptr))    { data.seeds = *flag ? Kit_layout_seeds::per_element : Kit_layout_seeds::item; }
    else if ((&property == &Kit_layout::seed_property.get())           && (integer != nullptr)) { data.seed = *integer; }
    else if ((&property == &Kit_layout::locked_property.get())         && (flag != nullptr))    { data.lock = *flag ? Kit_layout_lock::locked : Kit_layout_lock::unlocked; }
    else if (is_layout_connector_switch_property(property)             && (flag != nullptr))    { return {}; } // changes no child
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
        for (std::shared_ptr<Operation>& operation : generation) {
            out_operations.push_back(std::move(operation));
        }
    }
}

void Level_kits::append_layout_move_operations(
    Scene_root&                                               scene_root,
    const std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes,
    std::vector<std::shared_ptr<Operation>>&                  out_operations,
    const Kit_snap_pair* const                                snap
)
{
    std::vector<std::shared_ptr<erhe::scene::Node>> layouts;
    collect_moved_layouts(scene_root, moved_nodes, layouts);
    append_layout_regenerations(scene_root, layouts, "after the move", out_operations);
    // Step 41: the pieces attached to the moved nodes follow, last in the entry.
    std::shared_ptr<Operation> follow = make_attachment_follow_operation(scene_root, moved_nodes, {}, snap);
    if (follow) {
        out_operations.push_back(std::move(follow));
    }
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

void Level_kits::append_shared_corner_moves(
    Scene_root&                              scene_root,
    const erhe::scene::Curve&                curve,
    const erhe::math::Curve_path&            before,
    std::vector<std::shared_ptr<Operation>>& out_operations
)
{
    // The user: "moving connected corner control point moves corner in both floor tiles ... the corners which are
    // connected will be moved on all connected paths". Only where the flow mask lets floor joins through (C3.9).
    if (!get_effective_flow_mask().contains(Kit_connector_type::floor)) {
        return;
    }
    const erhe::math::Curve_path& after = curve.get_path();
    if (before.points.size() != after.points.size()) {
        return; // an added or removed corner moves no shared one
    }
    const glm::mat4 world_from_curve = curve.world_from_node();
    std::vector<std::pair<glm::vec3, glm::vec3>> moved; // world: from, to
    for (std::size_t i = 0, end = after.points.size(); i < end; ++i) {
        if (glm::distance(before.points[i].position, after.points[i].position) > 1.0e-6f) {
            moved.emplace_back(
                glm::vec3{world_from_curve * glm::vec4{before.points[i].position, 1.0f}},
                glm::vec3{world_from_curve * glm::vec4{after.points[i].position, 1.0f}}
            );
        }
    }
    if (moved.empty()) {
        return;
    }
    // The floors joined to this one by floor connectors, either end recording.
    std::vector<std::shared_ptr<erhe::scene::Curve>> neighbours;
    const std::string uid = curve.get_gltf_uid();
    const auto add_neighbour = [&](const std::shared_ptr<erhe::scene::Node>& node) {
        std::shared_ptr<erhe::scene::Curve> other = std::dynamic_pointer_cast<erhe::scene::Curve>(node);
        if (other && (other.get() != &curve) && is_floor_layout(*other) && (std::find(neighbours.begin(), neighbours.end(), other) == neighbours.end())) {
            neighbours.push_back(std::move(other));
        }
    };
    const auto is_floor_connector = [](const std::string& name) { return name.rfind("floor_", 0) == 0; };
    std::vector<Kit_attachment> own = read_kit_attachments(curve);
    scene_root.get_scene().for_each_node([&](const std::shared_ptr<erhe::scene::Node>& node) -> bool {
        for (const Kit_attachment& record : own) {
            if ((node->get_gltf_uid() == record.target) && is_floor_connector(record.connector)) {
                add_neighbour(node);
            }
        }
        if (node->has_local_value(Kit_piece::attachment_property.get())) {
            for (const Kit_attachment& record : read_kit_attachments(*node)) {
                if ((record.target == uid) && is_floor_connector(record.target_connector)) {
                    add_neighbour(node);
                }
            }
        }
        return true;
    });
    for (const std::shared_ptr<erhe::scene::Curve>& neighbour : neighbours) {
        const glm::mat4 neighbour_from_world = neighbour->node_from_world();
        erhe::math::Curve_path path = neighbour->get_path();
        bool changed = false;
        for (erhe::math::Curve_point& point : path.points) {
            const glm::vec3 world = glm::vec3{neighbour->world_from_node() * glm::vec4{point.position, 1.0f}};
            for (const std::pair<glm::vec3, glm::vec3>& move : moved) {
                if (glm::distance(world, move.first) <= 1.0e-3f) {
                    const glm::vec3 local = glm::vec3{neighbour_from_world * glm::vec4{move.second, 1.0f}};
                    point.position = glm::vec3{local.x, 0.0f, local.z};
                    changed = true;
                    break;
                }
            }
        }
        if (changed) {
            log_scene->log(flow_log_level(spdlog::level::info), "Floor '{}' moves the corners it shares with '{}'", neighbour->get_name(), curve.get_name());
            out_operations.push_back(std::make_shared<Curve_set_operation>(neighbour, neighbour->get_path(), std::move(path), std::string{"shared corner"}));
        }
    }
}

void Level_kits::append_floor_edge_renames(
    Scene_root&                              scene_root,
    const erhe::scene::Curve&                curve,
    const erhe::math::Curve_path&            before,
    std::vector<std::shared_ptr<Operation>>& out_operations
)
{
    // An edit that keeps the corner count keeps each edge's number: its connections stay on their edges.
    if (before.points.size() == curve.get_path().points.size()) {
        return;
    }
    // C8.4, K3.8: an edit that adds or removes corners renumbers the edges. The other ends have not moved yet, so each
    // connection naming one of the floor's connectors goes to the edge nearest the other end's connector when its own edge
    // is gone or clearly farther. The two families rename apart: edge_<i> (walls) and floor_<i> (floors, L25), each one
    // connection per edge; the floor's own records rename their own connector the same way.
    std::vector<glm::vec2> outline;
    if (!read_floor_outline(curve.get_table(), outline).empty() || (outline.size() < 3)) {
        return;
    }
    const std::size_t n = outline.size();
    const auto distance_to_edge = [&outline, n](const glm::vec2& p, const std::size_t edge) {
        const glm::vec2 a = outline[edge];
        const glm::vec2 b = outline[(edge + 1) % n];
        const glm::vec2 d = b - a;
        const float     t = std::clamp(glm::dot(p - a, d) / std::max(glm::dot(d, d), 1.0e-12f), 0.0f, 1.0f);
        return glm::distance(p, a + (t * d));
    };
    // The family and edge of one of the floor's connector names; nullopt for a name of neither family.
    class Floor_connector
    {
    public:
        bool        floor_family{false}; // floor_<i>, else edge_<i>
        std::size_t edge{0};             // n when the edge is gone
    };
    const auto read_name = [n](const std::string& name) -> std::optional<Floor_connector> {
        const bool floor_family = name.starts_with("floor_");
        if (!floor_family && !name.starts_with("edge_")) {
            return std::nullopt;
        }
        Floor_connector result{.floor_family = floor_family, .edge = n};
        for (std::size_t k = 0; k < n; ++k) {
            if (name == (floor_family ? get_floor_floor_connector_name(k) : get_floor_edge_connector_name(k))) {
                result.edge = k;
            }
        }
        return result;
    };
    const std::shared_ptr<erhe::scene::Node> self = std::dynamic_pointer_cast<erhe::scene::Node>(std::const_pointer_cast<erhe::Item_base>(curve.shared_from_this()));
    const std::string uid = curve.get_gltf_uid();
    const glm::mat4   floor_from_world = curve.node_from_world();
    std::vector<std::size_t> used_edges;  // edge_<i> family
    std::vector<std::size_t> used_floors; // floor_<i> family
    std::vector<Kit_connector> frames;
    // The floor's connector of one record renamed by where the other end's connector is, kept or dropped.
    enum class Rename : unsigned int { kept, renamed, dropped };
    const auto rename = [&](const erhe::scene::Node& holder, std::string& floor_connector, const erhe::scene::Node& other_end, const std::string& other_connector) -> Rename {
        const std::optional<Floor_connector> name = read_name(floor_connector);
        if (!name.has_value() || !get_world_connectors(other_end, frames, Kit_connector_set::all).empty()) {
            return Rename::kept;
        }
        const Kit_connector* const other = find_connector(frames, other_connector);
        if (other == nullptr) {
            return Rename::kept;
        }
        const glm::vec3 p3 = glm::vec3{floor_from_world * glm::vec4{other->position, 1.0f}};
        const glm::vec2 p{p3.x, p3.z};
        std::size_t nearest = 0;
        for (std::size_t k = 1; k < n; ++k) {
            if (distance_to_edge(p, k) < distance_to_edge(p, nearest)) {
                nearest = k;
            }
        }
        const std::size_t recorded = name.value().edge;
        const bool keep = (recorded < n) && (distance_to_edge(p, recorded) <= (distance_to_edge(p, nearest) + 0.01f));
        const std::size_t edge = keep ? recorded : nearest;
        std::vector<std::size_t>& used = name.value().floor_family ? used_floors : used_edges;
        if (std::find(used.begin(), used.end(), edge) != used.end()) {
            log_scene->log(
                flow_log_level(spdlog::level::warn), "Floor '{}': '{}' loses its connection, its edge {} holds another {}",
                curve.get_name(), holder.get_name(), edge, name.value().floor_family ? "floor" : "wall"
            );
            return Rename::dropped;
        }
        used.push_back(edge);
        if (keep) {
            return Rename::kept;
        }
        floor_connector = name.value().floor_family ? get_floor_floor_connector_name(edge) : get_floor_edge_connector_name(edge);
        return Rename::renamed;
    };
    std::unordered_map<std::string_view, std::shared_ptr<erhe::scene::Node>> by_uid;
    scene_root.get_scene().for_each_node([&by_uid](const std::shared_ptr<erhe::scene::Node>& node) -> bool {
        by_uid.emplace(std::string_view{node->get_gltf_uid()}, node);
        return true;
    });
    scene_root.get_scene().for_each_node([&](const std::shared_ptr<erhe::scene::Node>& node) -> bool {
        if (!node->has_local_value(Kit_piece::attachment_property.get())) {
            return true;
        }
        const bool own = (node == self);
        std::vector<Kit_attachment> records = read_kit_attachments(*node);
        bool changed = false;
        std::erase_if(records, [&](Kit_attachment& record) {
            Rename result = Rename::kept;
            if (own) {
                const std::unordered_map<std::string_view, std::shared_ptr<erhe::scene::Node>>::const_iterator target = by_uid.find(std::string_view{record.target});
                if (target != by_uid.end()) {
                    result = rename(*node, record.connector, *target->second, record.target_connector);
                }
            } else if (record.target == uid) {
                result = rename(*node, record.target_connector, *node, record.connector);
            }
            changed = changed || (result != Rename::kept);
            return result == Rename::dropped;
        });
        if (changed) {
            std::shared_ptr<Operation> operation = make_attachments_operation(node, records);
            if (operation) {
                out_operations.push_back(std::move(operation));
            }
        }
        return true;
    });
}

void Level_kits::append_curve_follow_ups(const erhe::scene::Curve& curve, const erhe::math::Curve_path& before, std::vector<std::shared_ptr<Operation>>& out_operations)
{
    Scene_root* const scene_root = dynamic_cast<Scene_root*>(curve.get_item_host());
    if ((scene_root == nullptr) || !is_locked_kit_layout(curve)) {
        return;
    }
    const std::array<std::shared_ptr<erhe::scene::Node>, 1> layouts{
        std::dynamic_pointer_cast<erhe::scene::Node>(std::const_pointer_cast<erhe::Item_base>(curve.shared_from_this()))
    };
    append_layout_regenerations(*scene_root, layouts, "after the curve edit", out_operations);
    // C3.6: a floor's outline edit moves its edge connectors: its walls follow and take their new lengths.
    if (is_floor_layout(curve)) {
        append_floor_edge_renames(*scene_root, curve, before, out_operations);
        append_shared_corner_moves(*scene_root, curve, before, out_operations);
        std::shared_ptr<Operation> follow = make_attachment_follow_operation(*scene_root, {}, layouts);
        if (follow) {
            out_operations.push_back(std::move(follow));
        }
    }
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
        // K9.3: a floor's corners shared with connected floors move from where they stood at the begin.
        const erhe::scene::Curve* const curve = dynamic_cast<const erhe::scene::Curve*>(layout.get());
        if ((curve != nullptr) && is_floor_layout(*curve)) {
            entry.curve_before = curve->get_path();
        }
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
                // The children ride with the node; a surface setting snaps them to the surface again.
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
    // K9.1: a move of kit pieces or floors runs the release's flow at each step; a move of other nodes moves no connector.
    const bool moves_connectors = std::any_of(moved_nodes.begin(), moved_nodes.end(), [](const std::shared_ptr<erhe::scene::Node>& node) {
        bool found = false;
        if (node) {
            static_cast<void>(node->for_each<erhe::scene::Node>([&found](erhe::scene::Node& n) -> bool {
                found = carries_kit_piece(n) || is_floor_layout(n);
                return !found;
            }));
        }
        return found;
    });
    if (!moves_connectors) {
        return;
    }
    m_flow_preview.scene_root = &scene_root;
    m_flow_preview.moved.assign(moved_nodes.begin(), moved_nodes.end());
    m_flow_preview.inputs.reserve(moved_nodes.size());
    m_flow_preview.outputs.reserve(moved_nodes.size());
    m_flow_preview.tool_poses.reserve(moved_nodes.size());
    m_flow_preview_state = Kit_flow_preview_state{.active = true};
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
    m_layout_transform_preview = !m_layout_preview.empty() || (m_flow_preview.scene_root != nullptr);
}

void Level_kits::step_layout_transform_preview()
{
    if (m_layout_transform_preview) {
        static_cast<void>(step_layout_preview());
    }
}

void Level_kits::prepare_layout_transform_write()
{
    if (!m_layout_transform_preview || (m_flow_preview.scene_root == nullptr)) {
        return;
    }
    read_flow_preview_tool_poses();
    undo_flow_preview_run();
    m_flow_preview.ran = false; // the next step runs: its inputs are the write's
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

auto Level_kits::step_layout_preview(const Kit_snap_pair* const snap) -> std::string
{
    const bool flow = (m_flow_preview.scene_root != nullptr);
    if (flow) {
        ++m_flow_preview_state.steps;
        read_flow_preview_tool_poses();
        // K9.4: a step of the poses and pair the shown run started from (a held snap) runs nothing.
        if (is_flow_preview_held(snap)) {
            ++m_flow_preview_state.held_steps;
            set_flow_preview_poses(m_flow_preview.outputs);
            return {};
        }
        // The flow ran after the layouts' generations: it is undone first and runs last, as the release orders them.
        undo_flow_preview_run();
    }
    std::string refusal = step_layout_preview_entries(nullptr, nullptr);
    if (flow) {
        run_flow_preview(snap);
    }
    return refusal;
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
    m_flow_previewing = true; // a floor's follow-ups run its flow
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
        // A floor's walls follow its outline and values live: the step runs the release's follow-ups (append_curve_follow_ups()
        // of a corner drag), undone with it.
        if (entry.built && (data.pattern.kind == Repeat_pattern_kind::floor)) {
            const erhe::scene::Curve* const curve = dynamic_cast<const erhe::scene::Curve*>(entry.node.get());
            if (curve != nullptr) {
                if (entry.curve_before.has_value()) {
                    append_floor_edge_renames(*scene_root, *curve, entry.curve_before.value(), entry.generation);
                    append_shared_corner_moves(*scene_root, *curve, entry.curve_before.value(), entry.generation);
                }
            }
            std::shared_ptr<Operation> follow = make_attachment_follow_operation(
                *scene_root, {}, std::span<const std::shared_ptr<erhe::scene::Node>>{&entry.node, 1}
            );
            if (follow) {
                entry.generation.push_back(std::move(follow));
            }
        }
        if (entry.built && given) {
            if (entry.values_before.empty()) {
                for (const erhe::property::Dependency_property* property : get_layout_value_properties()) {
                    entry.values_before.push_back(entry.node->read_local_state(*property));
                }
            }
            write_layout_values(*entry.node, data);
            m_layout_indicators.forget_layout(*entry.node);
            m_connector_indicators.forget_children_of(static_cast<std::uint64_t>(entry.node->get_id()));
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
    m_flow_previewing = false;
    restore_held_selection();
    const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
    log_scene->trace("layout preview step: {} layouts, {} pieces: {:.3f} ms", m_layout_preview.size(), piece_count, elapsed.count());
    return first_refusal;
}

void Level_kits::end_layout_preview()
{
    m_layout_transform_preview = false;
    end_flow_preview();
    if (m_layout_preview.empty()) {
        return;
    }
    hold_selection();
    m_flow_previewing = true;
    for (auto i = m_layout_preview.rbegin(), end = m_layout_preview.rend(); i != end; ++i) {
        for (auto j = i->operations.rbegin(), operations_end = i->operations.rend(); j != operations_end; ++j) {
            (*j)->undo(m_context);
        }
        if (!i->values_before.empty()) {
            const std::span<const erhe::property::Dependency_property* const> properties = get_layout_value_properties();
            for (std::size_t k = 0, count = properties.size(); k < count; ++k) {
                static_cast<void>(i->node->apply_local_state(*properties[k], i->values_before[k]));
            }
            m_layout_indicators.forget_layout(*i->node);
            m_connector_indicators.forget_children_of(static_cast<std::uint64_t>(i->node->get_id()));
        }
    }
    m_layout_preview.clear();
    m_flow_previewing = false;
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
    const erhe::scene::Node&     layout,
    const Kit_layout_data&       data,
    const Kit_layout_item_sizes& items,
    Kit_layout_run&              out_run
) const -> std::string
{
    out_run = Kit_layout_run{};
    const std::string error = check_layout_kind(layout, data.pattern.kind);
    if (!error.empty()) {
        return error;
    }
    const erhe::scene::Curve* const curve = dynamic_cast<const erhe::scene::Curve*>(&layout);
    const std::string run_error = resolve_layout_pattern(data.pattern, data.seed, items, (curve != nullptr) ? &curve->get_table() : nullptr, out_run, nullptr);
    out_run.pattern.path          = nullptr;
    out_run.pattern.scatter_items = {};
    out_run.pattern.segment_items = {};
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
    if (!is_curve_layout_kind(data.pattern.kind)) {
        result.error = fmt::format("a curve node carries a path, segments or stairs layout only, not a {} layout", c_str(data.pattern.kind));
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
        for (std::shared_ptr<Operation>& operation : generation) {
            operations.push_back(std::move(operation));
        }
    } else {
        result.added_count = 0;
    }
    result.error   = execute_entry(std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)}));
    result.changed = result.error.empty();
    return result;
}

} // namespace editor
