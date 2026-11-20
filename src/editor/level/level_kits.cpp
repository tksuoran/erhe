#include "level/level_kits.hpp"
#include "level/kit_thumbnails.hpp"

#include "level/kit_layout.hpp"
#include "level/kit_mesh_builder.hpp"
#include "level/kit_piece.hpp"
#include "level/kit_piece_light.hpp"
#include "level/kit_piece_operation.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_settings.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "brushes/brush.hpp"
#include "brushes/brush_placement.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "operations/compound_operation.hpp"
#include "operations/curve_set_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/item_parent_change_operation.hpp"
#include "operations/library_attach_operation.hpp"
#include "operations/mesh_material_assign_operation.hpp"
#include "level/floor_polygon.hpp"
#include "level/level_repeat.hpp"
#include "operations/mesh_primitive_swap.hpp"
#include "operations/node_transform_operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/property_set_operation.hpp"
#include "prefabs/instance_structure.hpp"
#include "scene/collision_shape_from_mesh.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_physics_system.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_verify/verify.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item_host.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_scene/curve.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include <fmt/format.h>
#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <deque>
#include <limits>
#include <chrono>
#include <cmath>
#include <mutex>

namespace editor {

auto make_joined_local_transform(const glm::mat4& parent_from_node, const glm::vec3& scale) -> erhe::scene::Trs_transform
{
    const glm::vec3 translation{parent_from_node[3]};
    const std::optional<glm::quat> rotation = rotation_for_scale(glm::mat3{parent_from_node}, scale);
    if (rotation.has_value()) {
        return erhe::scene::Trs_transform{translation, rotation.value(), scale};
    }
    const erhe::scene::Trs_transform decomposed{parent_from_node};
    return erhe::scene::Trs_transform{decomposed.get_translation(), decomposed.get_rotation(), decomposed.get_scale()};
}

namespace {

[[nodiscard]] auto sheared_join_message(const std::string_view piece, const std::string_view parent) -> std::string
{
    return fmt::format(
        "'{}' under '{}': the parent chain has non-uniform scale or shear, so the joined pose would need a sheared local transform",
        piece, parent
    );
}


// Smooth shapes keep their base rims hard: an edge sharper than this is a crease.
constexpr float c_smooth_crease_angle_degrees = 80.0f;

// Corner normals averaged over the facets around each vertex within the crease angle of the corner's facet.
void set_crease_corner_normals(erhe::geometry::Geometry& geometry, const float crease_angle_degrees)
{
    const GEO::Mesh&                 mesh       = geometry.get_mesh();
    erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();
    const float cos_limit = std::cos(glm::radians(crease_angle_degrees));

    std::vector<GEO::vec3f>                facet_normals(mesh.facets.nb());
    std::vector<std::vector<GEO::index_t>> vertex_facets(mesh.vertices.nb());
    for (const GEO::index_t facet : mesh.facets) {
        facet_normals[facet] = GEO::normalize(erhe::geometry::mesh_facet_normalf(mesh, facet));
        for (const GEO::index_t corner : mesh.facets.corners(facet)) {
            vertex_facets[mesh.facet_corners.vertex(corner)].push_back(facet);
        }
    }
    for (const GEO::index_t facet : mesh.facets) {
        const GEO::vec3f facet_normal = facet_normals[facet];
        for (const GEO::index_t corner : mesh.facets.corners(facet)) {
            GEO::vec3f sum{0.0f, 0.0f, 0.0f};
            for (const GEO::index_t other : vertex_facets[mesh.facet_corners.vertex(corner)]) {
                if (GEO::dot(facet_normal, facet_normals[other]) >= cos_limit) {
                    sum += facet_normals[other];
                }
            }
            attributes.corner_normal.set(corner, (GEO::length(sum) > 0.0f) ? GEO::normalize(sum) : facet_normal);
        }
    }
}

void process_kit_geometry(const Kit_shape_definition& shape, erhe::geometry::Geometry& geometry)
{
    geometry.process(
        {
            .flags =
                erhe::geometry::Geometry::process_flag_connect |
                erhe::geometry::Geometry::process_flag_build_edges |
                erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates
        }
    );
    if (shape.normal_style == Kit_normal_style::smooth) {
        set_crease_corner_normals(geometry, c_smooth_crease_angle_degrees);
    } else if (shape.normal_style == Kit_normal_style::smooth_with_hard_edges) {
        set_crease_corner_normals(geometry, shape.hard_edge_angle_degrees);
    }
}

} // anonymous namespace

auto get_kit_normal_style(const Kit_shape_definition& shape) -> erhe::primitive::Normal_style
{
    switch (shape.normal_style) {
        case Kit_normal_style::smooth:
        case Kit_normal_style::smooth_with_hard_edges: return erhe::primitive::Normal_style::corner_normals;
        case Kit_normal_style::faceted:
        default:                                       return erhe::primitive::Normal_style::polygon_normals;
    }
}

// The brush's geometry, hull and triangle mesh collision shape; a thumbnail's one part.
auto make_kit_geometry(const Kit_parameter_values& values) -> std::shared_ptr<erhe::geometry::Geometry>
{
    const Kit_shape_definition& shape = *values.get_shape();
    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>(shape.display_name);
    GEO::Mesh& mesh = geometry->get_mesh();
    shape.generator(values, mesh);
    // The parts carry the slots; the whole geometry keeps no kit attribute.
    if (mesh.facets.attributes().is_defined(c_kit_slot_attribute)) {
        mesh.facets.attributes().delete_attribute_store(c_kit_slot_attribute);
    }
    process_kit_geometry(shape, *geometry.get());
    return geometry;
}

auto make_kit_part_geometries(
    const Kit_parameter_values&                             values,
    std::vector<std::shared_ptr<erhe::geometry::Geometry>>& out_parts
) -> std::string
{
    const Kit_shape_definition& shape = *values.get_shape();
    out_parts.clear();
    GEO::Mesh mesh{3, true};
    shape.generator(values, mesh);
    std::vector<uint32_t> facet_slots;
    read_kit_facet_slots(mesh, facet_slots);
    for (const uint32_t slot : facet_slots) {
        if (slot >= shape.material_slots.size()) {
            return fmt::format("a facet names slot {}, the shape has {} slots", slot, shape.material_slots.size());
        }
    }
    for (std::size_t slot = 0, end = shape.material_slots.size(); slot < end; ++slot) {
        std::shared_ptr<erhe::geometry::Geometry> part = std::make_shared<erhe::geometry::Geometry>(
            fmt::format("{} {}", shape.display_name, shape.material_slots[slot])
        );
        extract_kit_slot_mesh(mesh, facet_slots, static_cast<uint32_t>(slot), part->get_mesh());
        if (part->get_mesh().facets.nb() == 0) {
            return fmt::format("slot '{}' has no facets", shape.material_slots[slot]);
        }
        process_kit_geometry(shape, *part.get());
        out_parts.push_back(std::move(part));
    }
    return {};
}

namespace {

[[nodiscard]] auto find_material_by_name(
    const std::shared_ptr<Content_library>& library,
    const std::string_view                  name
) -> std::shared_ptr<erhe::primitive::Material>
{
    for (const std::shared_ptr<erhe::primitive::Material>& material : library->get_all<erhe::primitive::Material>()) {
        if (material->get_name() == name) {
            return material;
        }
    }
    return {};
}

} // anonymous namespace

auto make_palette_material(const Kit_palette_material& palette) -> std::shared_ptr<erhe::primitive::Material>
{
    erhe::primitive::Material_values material_values{};
    material_values.base_color = palette.base_color;
    material_values.roughness  = glm::vec2{palette.roughness, palette.roughness};
    material_values.metallic   = palette.metallic;
    material_values.emissive   = palette.emissive;
    return std::make_shared<erhe::primitive::Material>(
        erhe::primitive::Material_create_info{.name = palette.name, .values = material_values}
    );
}

namespace {

// The given slot materials of a placement; returns the problem, empty when each resolves (Kit_slot_material).
[[nodiscard]] auto check_slot_materials(
    const Kit_shape_library&             library,
    const Kit_shape_definition&          shape,
    const std::vector<Kit_slot_material>& materials
) -> std::string
{
    if (materials.size() > shape.material_slots.size()) {
        return fmt::format("{} materials for the {} material slots of {}", materials.size(), shape.material_slots.size(), shape.id);
    }
    for (std::size_t slot = 0, end = shape.material_slots.size(); slot < end; ++slot) {
        const bool given = (slot < materials.size()) && (materials[slot].material || !materials[slot].palette_name.empty());
        if (given && !materials[slot].material && (library.find_palette_material(materials[slot].palette_name) == nullptr)) {
            return fmt::format("slot '{}': no palette material '{}'", shape.material_slots[slot], materials[slot].palette_name);
        }
        if (!given && (library.find_slot_default(shape.material_slots[slot]) == nullptr)) {
            return fmt::format("no palette material for slot '{}'", shape.material_slots[slot]);
        }
    }
    return {};
}

// Palette materials by name: found in the content library, else made once with its insert operation kept for the caller's entry.
class Palette_material_resolver
{
public:
    Palette_material_resolver(App_context& context, const std::shared_ptr<Content_library>& library)
        : m_context{context}
        , m_library{library}
    {
    }

    // The checks of check_slot_materials() have passed.
    [[nodiscard]] auto resolve(const Kit_shape_library& kits, const std::string_view slot, const Kit_slot_material& given) -> std::shared_ptr<erhe::primitive::Material>
    {
        if (given.material) {
            return given.material;
        }
        const Kit_palette_material* const palette = given.palette_name.empty()
            ? kits.find_slot_default(slot)
            : kits.find_palette_material(given.palette_name);
        const std::map<std::string, std::shared_ptr<erhe::primitive::Material>>::iterator made = m_made.find(palette->name);
        if (made != m_made.end()) {
            return made->second;
        }
        std::shared_ptr<erhe::primitive::Material> material = find_material_by_name(m_library, palette->name);
        if (!material) {
            material = make_palette_material(*palette);
            m_operations.push_back(make_resource_insert_operation(m_context, m_library, material, {}));
        }
        m_made.emplace(palette->name, material);
        return material;
    }

    [[nodiscard]] auto take_operations() -> std::vector<std::shared_ptr<Operation>>
    {
        return std::move(m_operations);
    }

private:
    App_context&                                                      m_context;
    std::shared_ptr<Content_library>                                  m_library;
    std::map<std::string, std::shared_ptr<erhe::primitive::Material>> m_made;
    std::vector<std::shared_ptr<Operation>>                           m_operations;
};

// Primitive i of the piece takes the brush's part i, its material kept; the counts are equal (one per slot).
[[nodiscard]] auto with_brush_primitives(const std::vector<erhe::scene::Mesh_primitive>& primitives, const Brush::Scaled& scaled) -> std::vector<erhe::scene::Mesh_primitive>
{
    std::vector<erhe::scene::Mesh_primitive> result = primitives;
    for (std::size_t i = 0, end = std::min(result.size(), scaled.part_primitives.size()); i < end; ++i) {
        result[i].primitive = scaled.part_primitives[i];
    }
    return result;
}

[[nodiscard]] auto shares_part_primitives(const erhe::scene::Mesh& mesh, const Brush::Scaled& scaled) -> bool
{
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh.get_primitives();
    if (primitives.empty() || (primitives.size() != scaled.part_primitives.size())) {
        return false;
    }
    for (std::size_t i = 0, end = primitives.size(); i < end; ++i) {
        if (primitives[i].primitive != scaled.part_primitives[i]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto part_count_problem(const erhe::scene::Mesh& mesh, const Brush::Scaled& scaled) -> std::string
{
    return fmt::format(
        "the mesh has {} primitives, the shape {} material slots", mesh.get_primitives().size(), scaled.part_primitives.size()
    );
}

// Kit_snap::module (U51): with a surface option on, the coordinate along the direction's dominant axis puts the
// point back on the plane through it across the direction, so the rounding acts within the surface.
[[nodiscard]] auto snap_point_to_module(const glm::vec3& point, const float module, const Surface_options& surface) -> glm::vec3
{
    const glm::vec3 rounded = Level_kits::snap_to_module(point, module);
    if (!any_surface_option(surface)) {
        return rounded;
    }
    const glm::vec3& direction = surface.direction;
    const glm::vec3  size      = glm::abs(direction);
    // Ties prefer Y, then X: a 45 degree slope keeps its X and Z on the lattice.
    const int axis = ((size.y >= size.x) && (size.y >= size.z)) ? 1 : ((size.x >= size.z) ? 0 : 2);
    if (!(size[axis] > 0.0f)) {
        return rounded;
    }
    glm::vec3 across = rounded - point;
    across[axis] = 0.0f;
    glm::vec3 result = rounded;
    result[axis] = point[axis] - (glm::dot(across, direction) / direction[axis]);
    return result;
}

// The module snap first, then the surface options (D3.2, D4.4); excludes nothing from the rays.
[[nodiscard]] auto snapped_pose(
    Scene_root&             scene_root,
    const erhe::math::Aabb& local_bounds,
    const glm::vec3&        position,
    const glm::quat&        rotation,
    const glm::vec3&        scale,
    const Kit_snap          snap,
    const float             module,
    const Surface_options&  surface
) -> Surface_placement
{
    const glm::vec3 snapped = (snap == Kit_snap::module) ? snap_point_to_module(position, module, surface) : position;
    return find_surface_placement(
        scene_root, local_bounds, Surface_pose{.position = snapped, .rotation = rotation, .scale = scale}, surface, {}
    );
}

} // anonymous namespace

Level_kits::Level_kits(App_context& context, App_message_bus& app_message_bus, erhe::graphics::Device& graphics_device, tf::Executor& background_executor)
    : m_context             {context}
    , m_thumbnails          {std::make_unique<Kit_thumbnails>(*this, context, app_message_bus, graphics_device, background_executor)}
    , m_connector_indicators{*this}
    , m_layout_indicators   {*this}
{
    // A live parameter drag ends without an entry when its piece is deselected, removed or its scene closes (D3.6).
    m_selection_subscription = app_message_bus.selection.subscribe(
        [this](Selection_message&) {
            on_selection();
        }
    );
    m_items_removed_subscription = app_message_bus.items_removed.subscribe(
        [this](Items_removed_message& message) {
            on_items_removed(*message.removed.get());
        }
    );
    m_close_scene_subscription = app_message_bus.close_scene.subscribe(
        [this](Close_scene_message& message) {
            on_close_scene(message.scene_root.get());
        }
    );
    m_curve_changed_subscription = app_message_bus.curve_changed.subscribe(
        [this](Curve_changed_message& message) {
            if (message.curve) {
                // A closure or point count change can make other children an open curve's head and tail (U39).
                m_connector_indicators.forget_children_of(static_cast<std::uint64_t>(message.curve->get_id()));
            }
            if (message.shape && message.curve) {
                // A segments layout draws its curve's chords.
                m_layout_indicators.forget_layout(*message.curve);
                if (is_layout_previewing(*message.curve)) {
                    static_cast<void>(step_layout_preview());
                }
            }
        }
    );
    // A parameter change, replace or live parameter drag swaps the piece's primitives with its record.
    m_mesh_geometry_changed_subscription = app_message_bus.mesh_geometry_changed.subscribe(
        [this](Mesh_geometry_changed_message& message) {
            if (message.mesh) {
                m_connector_indicators.forget_piece(*message.mesh);
            }
        }
    );
    // An operation wrote a Kit_layout value of the node.
    m_kit_layout_changed_subscription = app_message_bus.kit_layout_changed.subscribe(
        [this](Kit_layout_changed_message& message) {
            m_layout_indicators.forget_layout(message.node_id);
            // The connector switches, the lock or a regeneration's child count (U39); a floor's own values or kind.
            m_connector_indicators.forget_children_of(message.node_id);
            m_connector_indicators.forget_node(message.node_id);
        }
    );
}

auto Level_kits::get_connector_indicators() -> Kit_connector_indicators&
{
    return m_connector_indicators;
}

auto Level_kits::get_layout_indicators() -> Kit_layout_indicators&
{
    return m_layout_indicators;
}

void Level_kits::on_scene_materials_changed()
{
    // A layout item names its slot materials: every outline is refused or accepted again.
    m_layout_indicators.forget_all();
}

void Level_kits::on_level_lengths_changed()
{
    // Item footprints and extents derive from the lengths: every outline resolves again.
    m_layout_indicators.forget_all();
    // The shape defaults derive from them too.
    m_thumbnails->on_level_lengths_changed();
}

auto Level_kits::get_thumbnails() -> Kit_thumbnails&
{
    return *m_thumbnails;
}

void Level_kits::on_kit_piece_record_changed(const erhe::scene::Node& node)
{
    m_connector_indicators.forget_piece(node);
}

Level_kits::~Level_kits() noexcept = default;

auto Level_kits::get_library() const -> const Kit_shape_library&
{
    return m_library;
}

auto Level_kits::get_lengths(const Scene_root& scene_root) const -> Kit_lengths
{
    const std::optional<Level_settings>& level = scene_root.get_scene_settings().level;
    if (!level.has_value()) {
        return Kit_lengths{};
    }
    return Kit_lengths{.module = level.value().module, .wall_height = level.value().wall_height};
}

auto Level_kits::get_placement_settings() const -> Kit_placement_settings
{
    Kit_placement_settings settings;
    if (m_context.editor_settings == nullptr) {
        return settings;
    }
    const Level_kit_config& config = m_context.editor_settings->level_kit;
    settings.snap                  = config.snap_to_module ? Kit_snap::module : Kit_snap::none;
    settings.surface               = get_surface_settings();
    settings.surface.rest          = get_surface_rest(); // U60: the Brush tool follows the setting, as Transform tool drags do
    settings.rotation_step_degrees = static_cast<float>(config.rotation_step_degrees);
    settings.seed_per_piece        = config.new_seed_per_piece ? Kit_seed_per_piece::next : Kit_seed_per_piece::same;
    return settings;
}

auto Level_kits::add_piece_of_shape(
    Scene_root&                             scene_root,
    const Kit_shape_definition&             shape,
    const std::shared_ptr<erhe::Hierarchy>& parent,
    const glm::vec3&                        position,
    const glm::quat&                        rotation
) -> Kit_place_result
{
    Kit_placement placement;
    placement.shape    = &shape;
    placement.parent   = parent;
    placement.position = position;
    placement.rotation = rotation;
    Kit_place_result result = place(scene_root, std::span<const Kit_placement>{&placement, 1}, Kit_snap::none);
    if (result.error.empty() && !result.pieces.empty() && (m_context.selection != nullptr)) {
        m_context.selection->set_selection({result.pieces.front().node}, result.pieces.front().node);
    }
    return result;
}

auto Level_kits::get_layout_initial_values() const -> Kit_layout_initial_values
{
    Kit_layout_initial_values values;
    if (m_context.editor_settings == nullptr) {
        return values;
    }
    const Level_kit_config& config = m_context.editor_settings->level_kit;
    if (!config.layout_pattern.empty()) {
        Kit_layout_pattern pattern;
        const std::string error = parse_layout_pattern(config.layout_pattern, pattern);
        if (error.empty()) {
            values.data.pattern = pattern;
        } else {
            log_scene->warn("level_kit.layout_pattern does not read, the defaults stand: {}", error);
        }
    }
    values.data.snap           = config.layout_module_snap    ? Kit_snap::module              : Kit_snap::none;
    values.data.surface_snap   = config.layout_surface_snap   ? Surface_snap::on              : Surface_snap::off;
    values.data.surface_orient = config.layout_surface_orient ? Surface_orient::on            : Surface_orient::off;
    values.data.surface_direction         = config.layout_surface_direction;
    values.data.surface_forward_distance  = config.layout_surface_forward_distance;
    values.data.surface_backward_distance = config.layout_surface_backward_distance;
    values.data.surface_support           = config.layout_surface_support;
    values.data.seeds          = config.layout_per_piece_seed   ? Kit_layout_seeds::per_element : Kit_layout_seeds::item;
    values.data.seed           = config.layout_seed;
    values.data.lock           = config.layout_locked         ? Kit_layout_lock::locked       : Kit_layout_lock::unlocked;
    const auto visibility = [](const bool hidden) { return hidden ? Kit_layout_connector_visibility::hidden : Kit_layout_connector_visibility::shown; };
    values.data.connector_hiding.all = visibility(config.layout_hide_all_connectors);
    values.data.connector_hiding.set(Kit_layout_direction::plus_tangent,   visibility(config.layout_hide_plus_tangent_connectors));
    values.data.connector_hiding.set(Kit_layout_direction::minus_tangent,  visibility(config.layout_hide_minus_tangent_connectors));
    values.data.connector_hiding.set(Kit_layout_direction::plus_normal,    visibility(config.layout_hide_plus_normal_connectors));
    values.data.connector_hiding.set(Kit_layout_direction::minus_normal,   visibility(config.layout_hide_minus_normal_connectors));
    values.data.connector_hiding.set(Kit_layout_direction::plus_binormal,  visibility(config.layout_hide_plus_binormal_connectors));
    values.data.connector_hiding.set(Kit_layout_direction::minus_binormal, visibility(config.layout_hide_minus_binormal_connectors));
    values.spacing             = config.layout_spacing_from_item ? Kit_layout_spacing::from_item : Kit_layout_spacing::as_given;
    values.place_as            = config.place_as_layout       ? Kit_place_as::layout          : Kit_place_as::piece;
    return values;
}

void Level_kits::set_layout_initial_values(const Kit_layout_initial_values& values)
{
    if (m_context.editor_settings == nullptr) {
        return;
    }
    Level_kit_config& config = m_context.editor_settings->level_kit;
    config.layout_pattern           = (values.data.pattern == Kit_layout_pattern{}) ? std::string{} : to_canonical_text(values.data.pattern);
    config.layout_module_snap       = (values.data.snap == Kit_snap::module);
    config.layout_surface_snap      = (values.data.surface_snap == Surface_snap::on);
    config.layout_surface_orient    = (values.data.surface_orient == Surface_orient::on);
    config.layout_surface_direction         = values.data.surface_direction;
    config.layout_surface_forward_distance  = values.data.surface_forward_distance;
    config.layout_surface_backward_distance = values.data.surface_backward_distance;
    config.layout_surface_support           = values.data.surface_support;
    config.layout_per_piece_seed      = (values.data.seeds == Kit_layout_seeds::per_element);
    config.layout_seed              = values.data.seed;
    config.layout_locked            = (values.data.lock == Kit_layout_lock::locked);
    const Kit_layout_connector_hiding& hiding = values.data.connector_hiding;
    config.layout_hide_all_connectors            = (hiding.all == Kit_layout_connector_visibility::hidden);
    config.layout_hide_plus_tangent_connectors   = hiding.is_hidden(Kit_layout_direction::plus_tangent);
    config.layout_hide_minus_tangent_connectors  = hiding.is_hidden(Kit_layout_direction::minus_tangent);
    config.layout_hide_plus_normal_connectors    = hiding.is_hidden(Kit_layout_direction::plus_normal);
    config.layout_hide_minus_normal_connectors   = hiding.is_hidden(Kit_layout_direction::minus_normal);
    config.layout_hide_plus_binormal_connectors  = hiding.is_hidden(Kit_layout_direction::plus_binormal);
    config.layout_hide_minus_binormal_connectors = hiding.is_hidden(Kit_layout_direction::minus_binormal);
    config.layout_spacing_from_item = (values.spacing == Kit_layout_spacing::from_item);
    config.place_as_layout          = (values.place_as == Kit_place_as::layout);
    if (m_context.app_settings != nullptr) {
        m_context.app_settings->settings_store().touch();
    }
}

auto Level_kits::get_surface_settings() const -> Surface_options
{
    if (m_context.editor_settings == nullptr) {
        return make_surface_options(Surface_snap::off, Surface_orient::off, c_default_surface_direction, c_default_surface_max_distance);
    }
    const Surface_snap_config& config = m_context.editor_settings->surface_snap;
    return make_surface_options(
        config.snap_to_surface   ? Surface_snap::on   : Surface_snap::off,
        config.orient_to_surface ? Surface_orient::on : Surface_orient::off,
        c_default_surface_direction,
        config.max_distance
    );
}

void Level_kits::validate_surface_settings()
{
    if (m_context.editor_settings == nullptr) {
        return;
    }
    // The settings file is where an out-of-range distance enters; it is reported and replaced there.
    Level_kit_config& level_kit = m_context.editor_settings->level_kit;
    class Setting_distance
    {
    public:
        float*      value;
        float       fallback;
        const char* name;
    };
    for (const Setting_distance& distance : {
        Setting_distance{&m_context.editor_settings->surface_snap.max_distance, c_default_surface_max_distance,      "surface_snap.max_distance"},
        Setting_distance{&level_kit.layout_surface_forward_distance,             c_default_surface_forward_distance,  "level_kit.layout_surface_forward_distance"},
        Setting_distance{&level_kit.layout_surface_backward_distance,            c_default_surface_backward_distance, "level_kit.layout_surface_backward_distance"}
    }) {
        const std::string error = check_surface_max_distance(*distance.value, distance.name);
        if (!error.empty()) {
            log_scene->error("editor_settings.json: {}; the default {} m is used", error, distance.fallback);
            *distance.value = distance.fallback;
        }
    }
    class Setting_direction
    {
    public:
        glm::vec3*  value;
        const char* name;
    };
    for (const Setting_direction& direction : {
        Setting_direction{&level_kit.layout_surface_direction,                   "level_kit.layout_surface_direction"},
        Setting_direction{&m_context.editor_settings->surface_snap.drag_direction, "surface_snap.drag_direction"}
    }) {
        const std::string direction_error = check_surface_direction(*direction.value);
        if (!direction_error.empty()) {
            log_scene->error("editor_settings.json: {}: {}; the default (0, -1, 0) is used", direction.name, direction_error);
            *direction.value = c_default_surface_direction;
        }
    }
}

auto Level_kits::get_surface_rest() const -> Surface_snap_rest
{
    if (m_context.editor_settings == nullptr) {
        return Surface_snap_rest::leading_face;
    }
    return m_context.editor_settings->surface_snap.rest;
}

void Level_kits::set_surface_rest(const Surface_snap_rest rest)
{
    if (m_context.editor_settings == nullptr) {
        return;
    }
    m_context.editor_settings->surface_snap.rest = rest;
    if (m_context.app_settings != nullptr) {
        m_context.app_settings->settings_store().touch();
    }
}

auto Level_kits::get_surface_drag_direction_mode() const -> Surface_drag_direction
{
    return (m_context.editor_settings != nullptr) ? m_context.editor_settings->surface_snap.drag_direction_mode : Surface_drag_direction::explicit_vector;
}

void Level_kits::set_surface_drag_direction_mode(const Surface_drag_direction mode)
{
    if (m_context.editor_settings == nullptr) {
        return;
    }
    m_context.editor_settings->surface_snap.drag_direction_mode = mode;
    if (m_context.app_settings != nullptr) {
        m_context.app_settings->settings_store().touch();
    }
}

auto Level_kits::get_surface_drag_direction() const -> glm::vec3
{
    return (m_context.editor_settings != nullptr) ? m_context.editor_settings->surface_snap.drag_direction : c_default_surface_direction;
}

void Level_kits::set_surface_drag_direction(const glm::vec3& direction)
{
    ERHE_VERIFY(check_surface_direction(direction).empty());
    if (m_context.editor_settings == nullptr) {
        return;
    }
    m_context.editor_settings->surface_snap.drag_direction = direction;
    if (m_context.app_settings != nullptr) {
        m_context.app_settings->settings_store().touch();
    }
}

void Level_kits::set_surface_max_distance(const float distance)
{
    ERHE_VERIFY(check_surface_max_distance(distance).empty());
    if (m_context.editor_settings == nullptr) {
        return;
    }
    m_context.editor_settings->surface_snap.max_distance = distance;
    if (m_context.app_settings != nullptr) {
        m_context.app_settings->settings_store().touch();
    }
}

void Level_kits::set_surface_snap(const Surface_snap snap)
{
    if (m_context.editor_settings == nullptr) {
        return;
    }
    m_context.editor_settings->surface_snap.snap_to_surface = (snap == Surface_snap::on);
    if (m_context.app_settings != nullptr) {
        m_context.app_settings->settings_store().touch();
    }
}

void Level_kits::set_surface_orient(const Surface_orient orient)
{
    if (m_context.editor_settings == nullptr) {
        return;
    }
    m_context.editor_settings->surface_snap.orient_to_surface = (orient == Surface_orient::on);
    if (m_context.app_settings != nullptr) {
        m_context.app_settings->settings_store().touch();
    }
}

auto Level_kits::get_connector_snap_distance() const -> float
{
    return (m_context.editor_settings != nullptr) ? m_context.editor_settings->surface_snap.connector_snap_distance : 0.0f;
}

void Level_kits::set_connector_snap_distance(const float distance)
{
    if (m_context.editor_settings == nullptr) {
        return;
    }
    m_context.editor_settings->surface_snap.connector_snap_distance = (std::isfinite(distance) && (distance > 0.0f)) ? distance : 0.0f;
    if (m_context.app_settings != nullptr) {
        m_context.app_settings->settings_store().touch();
    }
}

auto Level_kits::get_connector_snap_axis_mode() const -> Connector_snap_axis_mode
{
    return (m_context.editor_settings != nullptr) ? m_context.editor_settings->surface_snap.connector_snap_axis_mode : Connector_snap_axis_mode::full_offset;
}

void Level_kits::set_connector_snap_axis_mode(const Connector_snap_axis_mode mode)
{
    if (m_context.editor_settings == nullptr) {
        return;
    }
    m_context.editor_settings->surface_snap.connector_snap_axis_mode = mode;
    if (m_context.app_settings != nullptr) {
        m_context.app_settings->settings_store().touch();
    }
}

auto Level_kits::get_connector_auto_connect() const -> bool
{
    return (m_context.editor_settings == nullptr) || m_context.editor_settings->surface_snap.connector_auto_connect;
}

void Level_kits::set_connector_auto_connect(const Connector_auto_connect auto_connect)
{
    if (m_context.editor_settings == nullptr) {
        return;
    }
    m_context.editor_settings->surface_snap.connector_auto_connect = (auto_connect == Connector_auto_connect::on);
    if (m_context.app_settings != nullptr) {
        m_context.app_settings->settings_store().touch();
    }
}

auto Level_kits::get_connector_flow_mask() const -> Kit_connector_types
{
    const int off = (m_context.editor_settings != nullptr) ? m_context.editor_settings->surface_snap.connector_flow_off : 0;
    return Kit_connector_types{.bits = Kit_connector_types::all().bits & ~static_cast<std::uint32_t>(off)};
}

void Level_kits::set_connector_flow_mask(const Kit_connector_types mask)
{
    if (m_context.editor_settings == nullptr) {
        return;
    }
    m_context.editor_settings->surface_snap.connector_flow_off = static_cast<int>(Kit_connector_types::all().bits & ~mask.bits);
    if (m_context.app_settings != nullptr) {
        m_context.app_settings->settings_store().touch();
    }
}

auto Level_kits::get_connection_step_limit() const -> std::size_t
{
    const int limit = (m_context.editor_settings != nullptr) ? m_context.editor_settings->surface_snap.connection_step_limit : static_cast<int>(c_connection_step_limit);
    return static_cast<std::size_t>(std::max(limit, 1));
}

void Level_kits::set_connection_step_limit(const std::size_t limit)
{
    if (m_context.editor_settings == nullptr) {
        return;
    }
    m_context.editor_settings->surface_snap.connection_step_limit = static_cast<int>(std::clamp<std::size_t>(limit, 1, 100000));
    if (m_context.app_settings != nullptr) {
        m_context.app_settings->settings_store().touch();
    }
}

void Level_kits::set_flow_overrides(const std::optional<Kit_flow_overrides>& overrides)
{
    m_flow_overrides = overrides;
}

auto Level_kits::get_flow_overrides() const -> const std::optional<Kit_flow_overrides>&
{
    return m_flow_overrides;
}

auto Level_kits::get_effective_flow_mask() const -> Kit_connector_types
{
    return (m_flow_overrides.has_value() && m_flow_overrides.value().flow_mask.has_value())
        ? m_flow_overrides.value().flow_mask.value()
        : get_connector_flow_mask();
}

auto Level_kits::get_effective_auto_connect() const -> bool
{
    return (m_flow_overrides.has_value() && m_flow_overrides.value().auto_connect.has_value())
        ? m_flow_overrides.value().auto_connect.value()
        : get_connector_auto_connect();
}

auto Level_kits::execute_entry(const std::shared_ptr<Operation>& operation) -> std::string
{
    m_context.operation_stack->execute_now(operation);
    return operation->is_refused() ? operation->get_error() : std::string{};
}

auto Level_kits::get_flow_report() -> Kit_flow_report*
{
    const Operation_stack* const stack = m_context.operation_stack;
    if ((stack == nullptr) || !stack->is_recording() || m_flow_previewing) {
        return nullptr;
    }
    if (m_flow_report_serial != stack->get_recording_serial()) {
        m_flow_report_serial = stack->get_recording_serial();
        m_flow_report.clear();
    }
    return &m_flow_report;
}

auto Level_kits::flow_log_level(const spdlog::level::level_enum level) const -> spdlog::level::level_enum
{
    return m_flow_previewing ? spdlog::level::trace : level;
}

auto Level_kits::get_last_flow_report() const -> const Kit_flow_report&
{
    return m_flow_report;
}

auto Level_kits::get_last_flow_report_serial() const -> std::size_t
{
    return m_flow_report_serial;
}

void Level_kits::update_flow_summary()
{
    const Kit_flow_report* const report = get_flow_report();
    if (report != nullptr) {
        m_flow_summary = report->make_summary();
    }
}

void Level_kits::clear_flow_summary()
{
    m_flow_summary.clear();
}

auto Level_kits::get_flow_summary() const -> const std::string&
{
    return m_flow_summary;
}

auto Level_kits::make_flow_piece(const erhe::scene::Node& node) -> Kit_flow_piece
{
    return Kit_flow_piece{.id = node.get_id(), .name = node.get_name()};
}

void Level_kits::report_new_connection(const Kit_new_connection& connection)
{
    Kit_flow_report* const report = get_flow_report();
    if ((report != nullptr) && connection.node && connection.target) {
        report->add_connection(
            report->new_connections,
            Kit_flow_connection{
                .piece           = make_flow_piece(*connection.node),
                .connector       = connection.attachment.connector,
                .other           = make_flow_piece(*connection.target),
                .other_connector = connection.attachment.target_connector
            }
        );
    }
}

auto Level_kits::get_connector_snap_orientation() const -> Connector_snap_orientation
{
    return (m_context.editor_settings != nullptr) ? m_context.editor_settings->surface_snap.connector_snap_orientation : Connector_snap_orientation::off;
}

void Level_kits::set_connector_snap_orientation(const Connector_snap_orientation orientation)
{
    if (m_context.editor_settings == nullptr) {
        return;
    }
    m_context.editor_settings->surface_snap.connector_snap_orientation = orientation;
    if (m_context.app_settings != nullptr) {
        m_context.app_settings->settings_store().touch();
    }
}

auto Level_kits::make_placement_pose(
    Scene_root&                     scene_root,
    const erhe::math::Aabb&         local_bounds,
    const glm::vec3&                point,
    const std::optional<glm::vec3>& surface_normal,
    const int                       yaw_steps,
    const Kit_placement_settings&   settings
) const -> Kit_placement_pose
{
    const Kit_lengths lengths  = get_lengths(scene_root);
    const Kit_snap    snap     = (lengths.module > 0.0f) ? settings.snap : Kit_snap::none;
    const float       yaw      = glm::radians(static_cast<float>(yaw_steps) * settings.rotation_step_degrees);
    const glm::quat   rotation = glm::angleAxis(yaw, glm::vec3{0.0f, 1.0f, 0.0f});

    // U51: the search runs into the surface under the pointer; without a usable normal the settings' direction (down).
    Surface_options surface = settings.surface;
    if (surface_normal.has_value()) {
        const float length = glm::length(surface_normal.value());
        if (std::isfinite(length) && (length > 1.0e-6f)) {
            surface.direction = -(surface_normal.value() / length);
        }
    }
    const Surface_placement placement = snapped_pose(scene_root, local_bounds, point, rotation, glm::vec3{1.0f}, snap, lengths.module, surface);
    return Kit_placement_pose{
        .position          = placement.pose.position,
        .rotation          = placement.pose.rotation,
        .surface_direction = surface.direction,
        .surface_found     = placement.surface_found
    };
}

void Level_kits::get_default_slot_materials(const Kit_shape_definition& shape, std::vector<std::string>& out_names) const
{
    out_names.clear();
    for (const std::string& slot : shape.material_slots) {
        const Kit_palette_material* const palette = m_library.find_slot_default(slot);
        out_names.push_back((palette != nullptr) ? palette->name : std::string{});
    }
}

auto Level_kits::get_preview_material(const std::string_view palette_name) -> std::shared_ptr<erhe::primitive::Material>
{
    const Kit_palette_material* const palette = m_library.find_palette_material(palette_name);
    if (palette == nullptr) {
        return {};
    }
    const std::map<std::string, std::shared_ptr<erhe::primitive::Material>>::iterator i = m_preview_materials.find(palette->name);
    if (i != m_preview_materials.end()) {
        return i->second;
    }
    std::shared_ptr<erhe::primitive::Material> material = make_palette_material(*palette);
    m_preview_materials.emplace(palette->name, material);
    return material;
}

auto Level_kits::make_default_values(const Scene_root& scene_root, const Kit_shape_definition& shape) const -> Kit_parameter_values
{
    const Kit_lengths lengths = get_lengths(scene_root);
    return editor::make_default_values(shape, lengths.module, lengths.wall_height);
}

namespace {

// The body and the ground flag a fresh placement of a shape gives its piece; Replace across a bodiless shape gives the same.
auto get_placed_motion_mode(const Brush::Scaled& scaled) -> erhe::physics::Motion_mode
{
    // Brush::make_instance() gives a body exactly when the scale has a collision shape (none without static physics).
    return scaled.collision_shape ? erhe::physics::Motion_mode::e_static : erhe::physics::Motion_mode::e_none;
}

auto get_placed_ground(const Kit_shape_definition& shape) -> Kit_piece_ground
{
    return (shape.collision == Kit_collision::none) ? Kit_piece_ground::pass_through : Kit_piece_ground::ground;
}

} // anonymous namespace

auto Level_kits::get_mesh_key(const Kit_parameter_values& values) -> std::string
{
    const Kit_shape_definition* shape = values.get_shape();
    return fmt::format("{} {}", (shape != nullptr) ? shape->id : std::string{}, values.to_canonical_text());
}

auto Level_kits::snap_to_module(const glm::vec3& position, const float module) -> glm::vec3
{
    return glm::vec3{
        module * std::round(position.x / module),
        module * std::round(position.y / module),
        module * std::round(position.z / module)
    };
}

auto Level_kits::make_brush(const Kit_parameter_values& values) -> std::shared_ptr<Brush>
{
    const Kit_shape_definition& shape = *values.get_shape();
    Brush_data brush_data{
        .context      = m_context,
        .app_settings = *m_context.app_settings,
        .name         = shape.display_name,
        .build_info   = erhe::primitive::Build_info{
            .primitive_types = {
                .fill_triangles          = true,
                .fill_triangles_expanded = true,
                .edge_lines              = true,
                .corner_points           = true,
                .centroid_points         = true
            },
            .buffer_info     = m_context.mesh_memory->make_primitive_buffer_info()
        },
        .normal_style       = get_kit_normal_style(shape),
        .geometry_generator = [values]() -> std::shared_ptr<erhe::geometry::Geometry> {
            return make_kit_geometry(values);
        },
        .density            = 1.0f
    };
    // Several slots make several parts, one mesh primitive each (D2.1); one slot is the whole geometry.
    if (shape.material_slots.size() > 1) {
        const std::string problem = make_kit_part_geometries(values, brush_data.part_geometries);
        if (!problem.empty()) {
            log_scene->warn("Kit shape '{}' made no parts for {}: {}", shape.id, values.to_canonical_text(), problem);
            return {};
        }
    }
    // A convex shape takes the brush's convex hull (Brush::late_initialize); the others their own triangles (D2.3)
    // or their bounds (U54); a shape without a body has none (P5).
    const Kit_collision collision = get_kit_collision(values);
    if (collision == Kit_collision::none) {
        brush_data.body = Brush_body::none;
    }
    if (collision == Kit_collision::triangle_mesh) {
        brush_data.collision_shape_generator = [values](const float scale) -> std::shared_ptr<erhe::physics::ICollision_shape> {
            const std::shared_ptr<erhe::geometry::Geometry> geometry = make_kit_geometry(values);
            return build_triangle_mesh_shape(geometry->get_mesh(), scale);
        };
    }
    if (collision == Kit_collision::box) {
        brush_data.collision_shape_generator = [values](const float scale) -> std::shared_ptr<erhe::physics::ICollision_shape> {
            return build_box_shape(get_kit_geometry_bounds(values), scale);
        };
    }
    std::shared_ptr<Brush> brush = std::make_shared<Brush>(brush_data);
    if (!brush->get_geometry()) {
        log_scene->warn("Kit shape '{}' made no geometry for {}", shape.id, values.to_canonical_text());
        return {};
    }
    return brush;
}

auto Level_kits::check_stairs_material(const Scene_root& scene_root, const std::string& material_name) const -> std::string
{
    return check_slot_material_name(scene_root, c_stairs_material_slot, material_name, "stairs material");
}

auto Level_kits::check_slot_material_name(const Scene_root& scene_root, const char* const slot, const std::string& material_name, const char* const what) const -> std::string
{
    if (material_name.empty()) {
        return (m_library.find_slot_default(slot) != nullptr)
            ? std::string{}
            : fmt::format("no palette material for slot '{}'", slot);
    }
    const std::shared_ptr<Content_library> library = scene_root.get_content_library();
    if ((library && find_material_by_name(library, material_name)) || (m_library.find_palette_material(material_name) != nullptr)) {
        return {};
    }
    return fmt::format("{}: no material '{}' in the scene or the kit palette", what, material_name);
}

auto Level_kits::make_stairs_insert(
    Scene_root&                                      scene_root,
    const std::shared_ptr<erhe::scene::Node>&        layout,
    const glm::mat4&                                 world_from_layout,
    const std::shared_ptr<erhe::geometry::Geometry>& geometry,
    const std::string&                               material_name,
    const std::string&                               source,
    std::vector<std::shared_ptr<Operation>>&         out_operations
) -> std::string
{
    return make_generated_child_insert(
        Generated_child{.name = "Stairs", .slot = c_stairs_material_slot, .what = "stairs material", .crease_degrees = c_stairs_crease_angle_degrees},
        scene_root, layout, world_from_layout, geometry, material_name, source, out_operations
    );
}

auto Level_kits::make_generated_child_insert(
    const Generated_child&                           child,
    Scene_root&                                      scene_root,
    const std::shared_ptr<erhe::scene::Node>&        layout,
    const glm::mat4&                                 world_from_layout,
    const std::shared_ptr<erhe::geometry::Geometry>& geometry,
    const std::string&                               material_name,
    const std::string&                               source,
    std::vector<std::shared_ptr<Operation>>&         out_operations
) -> std::string
{
    const std::shared_ptr<Content_library> library = scene_root.get_content_library();
    if (!library) {
        return "the scene has no content library";
    }
    const std::string material_problem = check_slot_material_name(scene_root, child.slot, material_name, child.what);
    if (!material_problem.empty()) {
        return material_problem;
    }
    Kit_slot_material given;
    if (!material_name.empty()) {
        given.material = find_material_by_name(library, material_name);
        if (!given.material) {
            given.palette_name = material_name;
        }
    }
    geometry->process(
        {
            .flags =
                erhe::geometry::Geometry::process_flag_connect |
                erhe::geometry::Geometry::process_flag_build_edges |
                erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates
        }
    );
    // Faces meeting at more than the crease angle keep a hard edge (the stairs' treads and risers, a floor's sides).
    set_crease_corner_normals(*geometry.get(), child.crease_degrees);
    Brush_data brush_data{
        .context      = m_context,
        .app_settings = *m_context.app_settings,
        .name         = child.name,
        .build_info   = erhe::primitive::Build_info{
            .primitive_types = {
                .fill_triangles          = true,
                .fill_triangles_expanded = true,
                .edge_lines              = true,
                .corner_points           = true,
                .centroid_points         = true
            },
            .buffer_info     = m_context.mesh_memory->make_primitive_buffer_info()
        },
        .normal_style = erhe::primitive::Normal_style::corner_normals,
        .geometry     = geometry,
        .density      = 1.0f
    };
    brush_data.collision_shape_generator = [geometry](const float scale) -> std::shared_ptr<erhe::physics::ICollision_shape> {
        return build_triangle_mesh_shape(geometry->get_mesh(), scale);
    };
    const std::shared_ptr<Brush> brush = std::make_shared<Brush>(brush_data);
    Palette_material_resolver    resolver{m_context, library};
    const std::array<std::shared_ptr<erhe::primitive::Material>, 1> materials{resolver.resolve(m_library, child.slot, given)};
    const Brush_instance_insert insert = make_brush_instance_insert(
        m_context, *brush.get(), scene_root, world_from_layout, materials, 1.0, erhe::physics::Motion_mode::e_static, layout, 0
    );
    if (!insert.node) {
        return fmt::format("the {} made no node", child.name);
    }
    insert.node->set_name(child.name);
    insert.node->set_value(Kit_layout_generated::source_property, source);
    // Materials are inserted first, so an undo takes the staircase out before the material it uses.
    for (std::shared_ptr<Operation>& operation : resolver.take_operations()) {
        out_operations.push_back(std::move(operation));
    }
    out_operations.push_back(insert.operation);
    return {};
}

auto Level_kits::get_brush(const Kit_parameter_values& values) -> std::shared_ptr<Brush>
{
    if (values.get_shape() == nullptr) {
        return {};
    }
    const std::string key = get_mesh_key(values);
    const std::map<std::string, std::weak_ptr<Brush>>::iterator i = m_brushes.find(key);
    if (i != m_brushes.end()) {
        std::shared_ptr<Brush> cached = i->second.lock();
        if (cached) {
            return cached;
        }
    }
    std::shared_ptr<Brush> brush = make_brush(values);
    if (!brush) {
        return {};
    }
    // The entries of released brushes go when a brush is made.
    std::erase_if(
        m_brushes,
        [](const std::pair<const std::string, std::weak_ptr<Brush>>& entry) -> bool { return entry.second.expired(); }
    );
    m_brushes.insert_or_assign(key, brush);
    return brush;
}

auto Level_kits::find_cached_brush(const Kit_parameter_values& values) const -> std::shared_ptr<Brush>
{
    if (values.get_shape() == nullptr) {
        return {};
    }
    const std::map<std::string, std::weak_ptr<Brush>>::const_iterator i = m_brushes.find(get_mesh_key(values));
    return (i != m_brushes.end()) ? i->second.lock() : std::shared_ptr<Brush>{};
}

auto Level_kits::read_piece_values(const Kit_piece_data& record, Kit_parameter_values& out_values) const -> std::string
{
    return read_kit_piece_record(m_library, record.shape, record.parameters, out_values);
}

auto Level_kits::check_piece(const erhe::scene::Node& node) const -> Kit_piece_check
{
    Kit_piece_check result;
    const std::optional<Kit_piece_data> record = read_kit_piece(node);
    if (!record.has_value()) {
        result.problem = "not a kit piece";
        return result;
    }
    Kit_parameter_values values;
    result.problem = read_piece_values(record.value(), values);
    if (!result.problem.empty()) {
        return result;
    }
    const std::shared_ptr<Brush>              brush     = find_cached_brush(values);
    const Brush::Scaled* const                scaled    = brush ? brush->get_scaled(1.0) : nullptr;
    const std::optional<Brush_placement_data> placement = read_brush_placement(node);
    const erhe::scene::Mesh* const            mesh      = dynamic_cast<const erhe::scene::Mesh*>(&node);
    const bool shares_primitive = (mesh != nullptr) && (scaled != nullptr) && shares_part_primitives(*mesh, *scaled);
    result.follows_kit =
        shares_primitive && placement.has_value() && (placement.value().brush == brush);
    if (!result.follows_kit) {
        result.problem = "the mesh is not the kit brush's";
    }
    return result;
}

void Level_kits::refresh_scene(const std::shared_ptr<Scene_root>& scene_root, std::function<void()> on_done)
{
    Scene_refresh refresh{.scene_root = scene_root, .on_done = std::move(on_done)};
    std::vector<std::shared_ptr<erhe::scene::Mesh>> pieces;
    scene_root->get_scene().for_each_node(
        [&pieces](const std::shared_ptr<erhe::scene::Node>& node) -> bool {
            if (carries_kit_piece(*node)) {
                const std::shared_ptr<erhe::scene::Mesh> mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(node);
                if (mesh) {
                    pieces.push_back(mesh);
                }
            }
            return true;
        }
    );
    refresh.report.piece_count = pieces.size();
    // K6.2: a loaded connection record list that does not read (a hand-edited file) is named once, at the load.
    scene_root->get_scene().for_each_node(
        [](const std::shared_ptr<erhe::scene::Node>& node) -> bool {
            const std::optional<std::string> text = node->read_local_value(Kit_piece::attachment_property);
            std::vector<Kit_attachment> attachments;
            const std::string problem = (text.has_value() && !text.value().empty()) ? parse_attachments_text(text.value(), attachments) : std::string{};
            if (!problem.empty()) {
                log_scene->error("Kit piece '{}' (id {}): its connections do not read ({}); none is followed", node->get_name(), node->get_id(), problem);
            }
            return true;
        }
    );
    // U33: switch values a write would refuse (a hand-edited file) are kept as loaded and named in one warning.
    std::string switch_problems;
    std::size_t switch_problem_count = 0;
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : pieces) {
        const Kit_piece_data record = read_kit_piece(*mesh).value();
        for (const erhe::property::Dependency_property* property : {&Kit_piece::connectors_enabled_property.get(), &Kit_piece::disabled_connectors_property.get()}) {
            if (!mesh->read_local_state(*property).has_value()) {
                continue;
            }
            const std::string problem = check_connector_switch_write(record, *property, mesh->get_value(*property));
            if (problem.empty()) {
                continue;
            }
            ++switch_problem_count;
            switch_problems += fmt::format("{}'{}' (id {}) {}: {}", switch_problems.empty() ? "" : "; ", mesh->get_name(), mesh->get_id(), property->get_name(), problem);
        }
        refresh.pieces.push_back(mesh);
    }
    if (switch_problem_count > 0) {
        log_scene->warn(
            "Scene '{}': {} kit piece connector switch value(s) kept as loaded although a write would refuse them (a listed name "
            "that is a connector of the shape is off, any other has no effect): {}",
            scene_root->get_name(), switch_problem_count, switch_problems
        );
    }
    m_scene_refreshes.push_back(std::move(refresh));
}

auto Level_kits::refresh_piece(Scene_refresh& refresh, const std::shared_ptr<erhe::scene::Mesh>& mesh) -> bool
{
    Scene_root& scene_root = *refresh.scene_root;
    const std::optional<Kit_piece_data> record = read_kit_piece(*mesh);
    if (!record.has_value() || (mesh->get_item_host() != &scene_root)) {
        return false; // no longer a piece of the scene
    }
    if (check_piece(*mesh).follows_kit) {
        ++refresh.report.follow_count; // an edit since the load gave it its brush
        return false;
    }
    Kit_parameter_values values;
    std::string problem = read_piece_values(record.value(), values);
    const bool made = problem.empty() && !find_cached_brush(values);
    std::shared_ptr<Brush> brush = problem.empty() ? get_brush(values) : std::shared_ptr<Brush>{};
    const Brush::Scaled* const scaled = brush ? brush->get_scaled(1.0) : nullptr;
    if (problem.empty() && (scaled == nullptr)) {
        problem = "the shape made no geometry";
    }
    if (problem.empty() && (mesh->get_primitives().size() != scaled->part_primitives.size())) {
        problem = part_count_problem(*mesh, *scaled);
    }
    if (!problem.empty()) {
        ++refresh.report.problem_count;
        log_scene->warn("Kit piece '{}' (id {}) keeps its loaded mesh: {}", mesh->get_name(), mesh->get_id(), problem);
        return made;
    }
    // As Kit_piece_parameters_operation applies it, without an undo entry: loading is not an edit.
    const std::vector<erhe::scene::Mesh_primitive> primitives = with_brush_primitives(mesh->get_primitives(), *scaled);
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root.item_host_mutex};
        swap_mesh_primitives(mesh, primitives);
        if (carries_node_physics(*mesh)) {
            set_node_collision_shape(*mesh, scaled->collision_shape);
        }
        set_brush_placement(*mesh, brush, GEO::NO_INDEX, GEO::NO_INDEX);
        // A record naming a former id of the shape takes its id, as erhe::property former names migrate.
        if (record.value().shape != values.get_shape()->id) {
            mesh->set_value(Kit_piece::shape_property, values.get_shape()->id);
        }
    }
    refresh.report.light_change_count += m_light_sync.apply_now(scene_root, mesh, values);
    ++refresh.report.follow_count;
    return made;
}

void Level_kits::finish_scene_refresh(Scene_refresh& refresh)
{
    const Kit_refresh_report& report = refresh.report;
    if (report.piece_count > 0) {
        log_scene->info(
            "Scene '{}': {} kit pieces, {} follow the kit, {} keep their loaded mesh, {} light nodes put on their frames",
            refresh.scene_root->get_name(), report.piece_count, report.follow_count, report.problem_count, report.light_change_count
        );
    }
    if (refresh.on_done) {
        refresh.on_done();
    }
}

void Level_kits::update_scene_refreshes()
{
    // Pieces whose brush is cached cost little; making a brush can take 200 ms in a Debug build, so a frame
    // makes at most one. A piece count, not a time budget, keeps runs on the deterministic clock alike.
    bool made = false;
    while (!m_scene_refreshes.empty()) {
        Scene_refresh& refresh = m_scene_refreshes.front();
        if (refresh.scene_root->is_close_requested()) {
            m_scene_refreshes.erase(m_scene_refreshes.begin()); // its close drops it; nothing more is done to it
            continue;
        }
        while (refresh.next < refresh.pieces.size()) {
            const std::shared_ptr<erhe::scene::Mesh> mesh = refresh.pieces[refresh.next].lock();
            if (mesh) {
                Kit_parameter_values values;
                const std::optional<Kit_piece_data> record = read_kit_piece(*mesh);
                const bool would_make = record.has_value() && read_piece_values(record.value(), values).empty() && !find_cached_brush(values);
                if (made && would_make) {
                    return;
                }
                made = refresh_piece(refresh, mesh) || made;
            }
            ++refresh.next;
        }
        Scene_refresh done = std::move(refresh);
        m_scene_refreshes.erase(m_scene_refreshes.begin());
        finish_scene_refresh(done);
    }
}

auto Level_kits::get_pending_refresh_count() const -> std::size_t
{
    std::size_t count = 0;
    for (const Scene_refresh& refresh : m_scene_refreshes) {
        count += (refresh.pieces.size() - refresh.next) + 1; // the finish (on_done) is pending too
    }
    return count;
}

namespace {

class Surface_snap_request
{
public:
    std::shared_ptr<erhe::scene::Mesh> mesh;
    erhe::math::Aabb                   local_bounds;
};

class Surface_snap_plan
{
public:
    Surface_placement          placement;
    std::shared_ptr<Operation> operation; // null when the node keeps its transform
    bool                       moved{false};
};

// Which found places get a transform operation.
enum class Snap_record : unsigned int {
    when_found = 0,
    when_moved
};

class Planned_world
{
public:
    const erhe::scene::Node* node{nullptr};
    glm::mat4                world_from_node{1.0f};
};

// The node's world matrix once the planned snaps of it and of its ancestors apply; none when no such snap is planned.
[[nodiscard]] auto find_planned_world(const erhe::scene::Node& node, const std::span<const Planned_world> planned) -> std::optional<glm::mat4>
{
    if (planned.empty()) {
        return {};
    }
    glm::mat4 ancestor_from_node{1.0f};
    const erhe::scene::Node*           current = &node;
    std::shared_ptr<erhe::scene::Node> parent;
    while (current != nullptr) {
        for (const Planned_world& entry : planned) {
            if (entry.node == current) {
                return entry.world_from_node * ancestor_from_node;
            }
        }
        ancestor_from_node = current->parent_from_node() * ancestor_from_node;
        parent  = current->get_parent_node();
        current = parent.get();
    }
    return {};
}

// D7.3 for each mesh, parents before children: a child's search starts from its pose after its ancestors' snaps, and
// its local transform is taken against its parent's pose after the parent's snap. out_plans is per request, in order.
void plan_surface_snaps(
    erhe::raytrace::IScene&                         committed_rt_scene,
    const std::span<const Surface_snap_request>     requests,
    const Surface_options&                          options,
    const std::span<const erhe::scene::Mesh* const> excluded,
    const Snap_record                               record,
    std::vector<Surface_snap_plan>&                 out_plans
)
{
    out_plans.clear();
    out_plans.resize(requests.size());
    std::vector<std::size_t> order(requests.size());
    for (std::size_t i = 0, end = requests.size(); i < end; ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&requests](const std::size_t lhs, const std::size_t rhs) -> bool {
        return requests[lhs].mesh->get_depth() < requests[rhs].mesh->get_depth();
    });
    std::vector<Planned_world> planned;
    for (const std::size_t i : order) {
        const std::shared_ptr<erhe::scene::Mesh>& mesh  = requests[i].mesh;
        const std::optional<glm::mat4>            moved = find_planned_world(*mesh, planned);
        const erhe::scene::Trs_transform world = moved.has_value() ? erhe::scene::Trs_transform{moved.value()} : mesh->world_from_node_transform();
        const Surface_pose start{.position = world.get_translation(), .rotation = world.get_rotation(), .scale = world.get_scale()};
        Surface_snap_plan& plan = out_plans[i];
        plan.placement = find_surface_placement(committed_rt_scene, requests[i].local_bounds, start, options, excluded);
        plan.moved = plan.placement.surface_found &&
            ((plan.placement.pose.position != start.position) || (plan.placement.pose.rotation != start.rotation));
        const bool records = (record == Snap_record::when_found) ? plan.placement.surface_found : plan.moved;
        if (!records) {
            continue;
        }
        const glm::mat4 world_from_node =
            erhe::math::create_translation<float>(plan.placement.pose.position) *
            glm::mat4_cast(plan.placement.pose.rotation) *
            erhe::math::create_scale<float>(plan.placement.pose.scale);
        const std::shared_ptr<erhe::scene::Node> parent = mesh->get_parent_node();
        glm::mat4 parent_from_node = world_from_node;
        if (parent) {
            const std::optional<glm::mat4> parent_moved = find_planned_world(*parent, planned);
            parent_from_node = (parent_moved.has_value() ? glm::inverse(parent_moved.value()) : parent->node_from_world()) * world_from_node;
        }
        plan.operation = std::make_shared<Node_transform_operation>(
            Node_transform_operation::Parameters{
                .node                    = mesh,
                .parent_from_node_before = mesh->parent_from_node_transform(),
                .parent_from_node_after  = erhe::scene::Trs_transform{parent_from_node},
                .xform_op_stack_before   = mesh->copy_xform_op_stack()
            }
        );
        planned.push_back(Planned_world{.node = mesh.get(), .world_from_node = world_from_node});
    }
}

} // anonymous namespace

auto Level_kits::snap_to_surface(
    Scene_root&                                               scene_root,
    const std::span<const std::shared_ptr<erhe::scene::Mesh>> meshes,
    const Surface_options&                                    options,
    const Kit_snap_apply                                      apply,
    std::vector<Kit_snap_result>&                             out_results,
    std::string* const                                        out_refusal
) -> std::size_t
{
    out_results.clear();
    std::vector<const erhe::scene::Mesh*> excluded;
    std::vector<Surface_snap_request>     requests;
    excluded.reserve(meshes.size());
    requests.reserve(meshes.size());
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : meshes) {
        excluded.push_back(mesh.get());
        requests.push_back(Surface_snap_request{.mesh = mesh, .local_bounds = mesh_local_bounds(*mesh)});
    }
    std::vector<std::shared_ptr<Operation>>         operations;
    std::vector<std::shared_ptr<erhe::scene::Node>> moved_nodes;
    if (any_surface_option(options)) {
        erhe::raytrace::IScene& rt_scene = scene_root.get_raytrace_scene();
        rt_scene.commit();
        std::vector<Surface_snap_plan> plans;
        plan_surface_snaps(rt_scene, requests, options, excluded, Snap_record::when_found, plans);
        for (std::size_t i = 0, end = meshes.size(); i < end; ++i) {
            out_results.push_back(Kit_snap_result{.mesh = meshes[i], .placement = plans[i].placement});
            if ((apply == Kit_snap_apply::move) && plans[i].operation) {
                operations.push_back(plans[i].operation);
                moved_nodes.push_back(meshes[i]);
            }
        }
    } else {
        for (const std::shared_ptr<erhe::scene::Mesh>& mesh : meshes) {
            const erhe::scene::Trs_transform world = mesh->world_from_node_transform();
            out_results.push_back(Kit_snap_result{
                .mesh      = mesh,
                .placement = Surface_placement{.pose = Surface_pose{.position = world.get_translation(), .rotation = world.get_rotation(), .scale = world.get_scale()}}
            });
        }
    }
    const std::size_t moved_count = operations.size();
    if (!operations.empty()) {
        // Step 41: the pieces attached to the snapped pieces follow (and a snapped attached piece off its join detaches).
        std::shared_ptr<Operation> follow = make_attachment_follow_operation(scene_root, moved_nodes, {});
        if (follow) {
            operations.push_back(std::move(follow));
        }
        const std::string refusal = execute_entry(
            (operations.size() == 1)
                ? operations.front()
                : std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)})
        );
        if (!refusal.empty()) {
            if (out_refusal != nullptr) {
                *out_refusal = refusal;
            }
            return 0;
        }
    }
    return moved_count;
}

auto Level_kits::get_cached_brush_count() const -> std::size_t
{
    return static_cast<std::size_t>(
        std::count_if(
            m_brushes.begin(), m_brushes.end(),
            [](const std::pair<const std::string, std::weak_ptr<Brush>>& entry) -> bool { return !entry.second.expired(); }
        )
    );
}

auto Level_kits::place(Scene_root& scene_root, const std::span<const Kit_placement> placements, const Kit_snap snap) -> Kit_place_result
{
    Kit_place_result result;
    for (std::size_t i = 0, end = placements.size(); i < end; ++i) {
        if (placements[i].parent && (placements[i].parent->get_item_host() != &scene_root)) {
            result.error       = fmt::format("parent '{}' is not in scene '{}'", placements[i].parent->get_name(), scene_root.get_name());
            result.error_index = i;
            return result;
        }
        const std::optional<std::string> refusal = placements[i].parent
            ? locked_layout_child_refusal(*placements[i].parent)
            : std::optional<std::string>{};
        if (refusal.has_value()) {
            result.error       = refusal.value();
            result.error_index = i;
            return result;
        }
    }
    std::vector<std::shared_ptr<Operation>> operations;
    if (!prepare_placements(scene_root, placements, snap, result, operations) || result.pieces.empty()) {
        return result;
    }
    const std::string refusal = execute_entry(std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)}));
    if (!refusal.empty()) {
        result.error = refusal;
        result.pieces.clear();
    }
    return result;
}

auto Level_kits::prepare_placements(
    Scene_root&                              scene_root,
    const std::span<const Kit_placement>     placements,
    const Kit_snap                           snap,
    Kit_place_result&                        result,
    std::vector<std::shared_ptr<Operation>>& out_operations
) -> bool
{
    out_operations.clear();
    result = Kit_place_result{};
    const auto fail = [&result](const std::size_t index, std::string message) -> bool {
        result.pieces.clear();
        result.error       = std::move(message);
        result.error_index = index;
        return false;
    };

    const std::shared_ptr<Content_library> library = scene_root.get_content_library();
    if (!library) {
        return fail(0, "the scene has no content library");
    }
    const Kit_lengths lengths = get_lengths(scene_root);
    if ((snap == Kit_snap::module) && !(lengths.module > 0.0f)) {
        return fail(0, fmt::format("snap to module needs a positive module, the scene's is {} m", lengths.module));
    }

    // Every placement is checked before anything is made, so a failing one leaves the scene unchanged.
    std::vector<Kit_new_connection>           made;                // the joins' connections, on the end that holds each
    std::vector<Kit_new_connection>           recorded_on_targets; // of them, those held by a placement's target (K13)
    std::vector<Kit_parameter_values>         values;
    std::vector<std::optional<Kit_join_pose>> joins;
    values.reserve(placements.size());
    joins.reserve(placements.size());
    for (std::size_t i = 0, end = placements.size(); i < end; ++i) {
        const Kit_placement& placement = placements[i];
        if (placement.shape == nullptr) {
            return fail(i, "no shape");
        }
        Kit_parameter_values piece_values = placement.values.has_value()
            ? placement.values.value()
            : make_default_values(scene_root, *placement.shape);
        if (piece_values.get_shape() != placement.shape) {
            return fail(i, fmt::format("the parameter values are not of shape {}", placement.shape->id));
        }
        const std::string message = piece_values.validate();
        if (!message.empty()) {
            return fail(i, message);
        }
        if (placement.parent_index.has_value() && (placement.parent_index.value() >= i)) {
            return fail(i, fmt::format("parent_index {} does not name an earlier piece", placement.parent_index.value()));
        }
        if (any_surface_option(placement.surface) && !(std::abs(glm::length(placement.surface.direction) - 1.0f) < 1.0e-3f)) {
            return fail(i, "the surface direction must be a unit vector");
        }
        const std::string material_problem = check_slot_materials(m_library, *placement.shape, placement.materials);
        if (!material_problem.empty()) {
            return fail(i, material_problem);
        }
        if (!std::isfinite(placement.scale.x) || !std::isfinite(placement.scale.y) || !std::isfinite(placement.scale.z) ||
            !(placement.scale.x > 0.0f) || !(placement.scale.y > 0.0f) || !(placement.scale.z > 0.0f)) {
            return fail(i, fmt::format("the scale ({}, {}, {}) must be positive", placement.scale.x, placement.scale.y, placement.scale.z));
        }
        std::optional<Kit_join_pose> join{};
        if (placement.connect.has_value() && (placement.scale != glm::vec3{1.0f})) {
            return fail(i, "a connector join places a piece at scale 1");
        }
        if (placement.connect.has_value()) {
            Kit_join_pose pose{};
            // A new piece has every connector enabled (U33).
            const std::string connect_problem = resolve_connect(scene_root, piece_values, Kit_connector_switches{}, placement.connect.value(), glm::mat4{1.0f}, pose);
            if (!connect_problem.empty()) {
                return fail(i, connect_problem);
            }
            // K6.2: one connection per connector; a placement that records one needs the target's connector free, also of
            // the earlier placements of the call.
            if (placement.connect.value().attach == Kit_attach::attach) {
                for (std::size_t j = 0; j < i; ++j) {
                    const std::optional<Kit_connect>& earlier = placements[j].connect;
                    if (
                        earlier.has_value() && (earlier.value().attach == Kit_attach::attach) &&
                        (earlier.value().target == placement.connect.value().target) &&
                        (earlier.value().target_connector == placement.connect.value().target_connector)
                    ) {
                        return fail(i, fmt::format(
                            "placements {} and {} both connect to connector '{}' of '{}' (one connection per connector)",
                            j, i, placement.connect.value().target_connector, placement.connect.value().target->get_name()
                        ));
                    }
                }
                const std::shared_ptr<erhe::scene::Node> other = find_connection_at(scene_root, *placement.connect.value().target, placement.connect.value().target_connector);
                if (other) {
                    return fail(i, fmt::format(
                        "connector '{}' of '{}' is connected to '{}' already (detach it first)",
                        placement.connect.value().target_connector, placement.connect.value().target->get_name(), other->get_name()
                    ));
                }
            }
            // D3.12: the joined world pose has scale 1; under the parent it must be a local transform without shear.
            const std::shared_ptr<erhe::scene::Node> parent_node = std::dynamic_pointer_cast<erhe::scene::Node>(placement.parent);
            if (parent_node && !has_orthogonal_axes(glm::mat3{parent_node->node_from_world() * apply_motion(pose, glm::mat4{1.0f})})) {
                return fail(i, sheared_join_message(placement.name.empty() ? placement.shape->display_name : placement.name, parent_node->get_name()));
            }
            join = pose;
        }
        joins.push_back(join);
        values.push_back(std::move(piece_values));
    }

    Palette_material_resolver                                         resolver{m_context, library};
    std::vector<std::shared_ptr<erhe::primitive::Material>>           materials;
    std::vector<std::shared_ptr<Operation>>                           insert_operations;
    std::map<const erhe::Hierarchy*, std::size_t>                     batch_children;
    for (std::size_t i = 0, end = placements.size(); i < end; ++i) {
        const Kit_placement&        placement = placements[i];
        const Kit_shape_definition& shape     = *placement.shape;

        std::shared_ptr<Brush> brush = get_brush(values[i]);
        if (!brush) {
            return fail(i, fmt::format("{}: the shape made no geometry", shape.id));
        }

        materials.clear();
        for (std::size_t slot = 0, slot_end = shape.material_slots.size(); slot < slot_end; ++slot) {
            const Kit_slot_material none{};
            const Kit_slot_material& given = (slot < placement.materials.size()) ? placement.materials[slot] : none;
            materials.push_back(resolver.resolve(m_library, shape.material_slots[slot], given));
        }

        // The earlier pieces of this call are not surfaces yet; a connector join is exact, so it is not snapped to the surface.
        const Surface_placement surface = joins[i].has_value()
            ? Surface_placement{.pose = Surface_pose{.position = joins[i]->position, .rotation = joins[i]->rotation, .scale = glm::vec3{1.0f}}}
            : snapped_pose(scene_root, brush->get_bounding_box(), placement.position, placement.rotation, placement.scale, snap, lengths.module, placement.surface);
        const glm::vec3 position        = surface.pose.position;
        const glm::quat rotation        = surface.pose.rotation;
        const glm::mat4 world_from_node =
            erhe::math::create_translation<float>(position) * glm::mat4_cast(rotation) * erhe::math::create_scale<float>(placement.scale);

        std::shared_ptr<erhe::Hierarchy> parent = placement.parent;
        if (placement.parent_index.has_value()) {
            parent = result.pieces[placement.parent_index.value()].node;
        }
        if (!parent) {
            parent = scene_root.get_scene().get_root_node();
        }
        // Appended after the parent's children and the earlier pieces of this call under it, unless placed at an index.
        std::size_t& earlier = batch_children[parent.get()];
        const std::size_t index_in_parent = placement.index_in_parent.has_value()
            ? placement.index_in_parent.value()
            : (parent->get_children().size() + earlier);
        ++earlier;

        const Brush_instance_insert insert = make_brush_instance_insert(
            m_context, *brush.get(), scene_root, world_from_node, materials, 1.0,
            erhe::physics::Motion_mode::e_static, parent, index_in_parent
        );
        if (!insert.node) {
            return fail(i, fmt::format("{}: the brush made no node", shape.id));
        }
        insert.node->set_name(placement.name.empty() ? shape.display_name : placement.name);
        if (get_placed_ground(shape) == Kit_piece_ground::pass_through) {
            insert.node->enable_flag_bits(Editor_item_flags::pass_through);
        }
        write_kit_piece(*insert.node.get(), Kit_piece_data{.shape = shape.id, .parameters = values[i].to_canonical_text()});
        // Step 41: a join records its attachment on the new piece (in no chain yet), unless a locked layout owns either side;
        // K13: a new host joined onto a rider records it on the rider, written after the inserts.
        if (placement.connect.has_value() && (placement.connect.value().attach == Kit_attach::attach) &&
            !locked_layout_child_refusal(*parent).has_value() && !find_locking_kit_layout(*placement.connect.value().target)) {
            Kit_new_connection connection = make_join_connection(insert.node, values[i], placement.connect.value());
            if (connection.node == insert.node) {
                insert.node->set_value(Kit_piece::attachment_property, make_attachment_text(connection.attachment));
            } else {
                recorded_on_targets.push_back(connection);
            }
            made.push_back(std::move(connection));
        }
        // The lights ride the piece's insert as its children (D3.15).
        add_piece_lights(scene_root, insert.node, values[i]);
        insert_operations.push_back(insert.operation);
        result.pieces.push_back(
            Kit_placed_piece{
                .node          = insert.node,
                .brush         = brush,
                .materials     = materials,
                .values        = values[i],
                .position      = position,
                .rotation      = rotation,
                .surface_found = surface.surface_found
            }
        );
    }

    // Materials are inserted first, so an undo takes the pieces out before the materials they use.
    out_operations = resolver.take_operations();
    for (std::shared_ptr<Operation>& operation : insert_operations) {
        out_operations.push_back(std::move(operation));
    }
    // C3.4: the end that records each placed piece's connection takes the values it supplies, in the same entry.
    std::vector<std::shared_ptr<erhe::scene::Node>> requesting;
    for (const Kit_new_connection& connection : recorded_on_targets) {
        std::shared_ptr<Operation> record = make_attachment_operation(connection.node, connection.attachment);
        if (record) {
            out_operations.push_back(std::move(record));
        }
    }
    for (const Kit_new_connection& connection : made) {
        if (std::find(requesting.begin(), requesting.end(), connection.node) == requesting.end()) {
            requesting.push_back(connection.node);
        }
    }
    if (!requesting.empty()) {
        std::shared_ptr<Operation> flow = make_attachment_follow_operation(scene_root, {}, {}, nullptr, requesting, made);
        if (flow) {
            out_operations.push_back(std::move(flow));
        }
    }
    return true;
}

namespace {

// A node of the scene other than its root whose place in the tree may change; returns the refusal.
[[nodiscard]] auto check_scene_node(const Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& node) -> std::string
{
    if (!node) {
        return "no node";
    }
    if (node->get_item_host() != &scene_root) {
        return fmt::format("node '{}' is not in scene '{}'", node->get_name(), scene_root.get_name());
    }
    if (!node->get_parent().lock()) {
        return fmt::format("'{}' is the scene root", node->get_name());
    }
    const std::optional<std::string> refusal = instance_structure_refusal(*node);
    return refusal.has_value() ? refusal.value() : std::string{};
}

void include_subtree_bounds(const erhe::Hierarchy& hierarchy, erhe::math::Aabb& mesh_bounds, erhe::math::Aabb& origin_bounds)
{
    const erhe::scene::Node* const node = dynamic_cast<const erhe::scene::Node*>(&hierarchy);
    if (node != nullptr) {
        origin_bounds.include(glm::vec3{node->world_from_node()[3]});
        const erhe::scene::Mesh* const mesh = dynamic_cast<const erhe::scene::Mesh*>(node);
        if (mesh != nullptr) {
            const erhe::math::Aabb world = mesh->get_aabb_world();
            if (world.is_valid_3d()) {
                mesh_bounds.include(world);
            }
        }
    }
    for (const std::shared_ptr<erhe::Hierarchy>& child : hierarchy.get_children()) {
        include_subtree_bounds(*child, mesh_bounds, origin_bounds);
    }
}

// The node descendants of the hierarchy, parents before their children; a locked kit layout node's children ride with it.
void collect_descendant_nodes(const erhe::Hierarchy& hierarchy, std::vector<std::shared_ptr<erhe::scene::Node>>& out_nodes)
{
    for (const std::shared_ptr<erhe::Hierarchy>& child : hierarchy.get_children()) {
        std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
        const bool locked_layout = node && is_locked_kit_layout(*node);
        if (node) {
            out_nodes.push_back(std::move(node));
        }
        if (!locked_layout) {
            collect_descendant_nodes(*child, out_nodes);
        }
    }
}

} // anonymous namespace

auto Level_kits::get_local_connectors(const erhe::scene::Node& node, std::vector<Kit_connector>& out_connectors, const Kit_connector_set set) const -> std::string
{
    out_connectors.clear();
    const std::optional<Kit_piece_data> record = read_kit_piece(node);
    if (!record.has_value()) {
        // K6: a floor layout node is a connection end with one wall-floor connector per edge (all enabled).
        if (is_floor_layout(node)) {
            return get_floor_layout_connectors(node, out_connectors);
        }
        return fmt::format("'{}' is not a kit piece", node.get_name());
    }
    Kit_parameter_values values;
    const std::string problem = read_piece_values(record.value(), values);
    if (!problem.empty()) {
        return fmt::format("'{}': {}", node.get_name(), problem);
    }
    get_shape_connectors(values, out_connectors);
    if (set == Kit_connector_set::enabled) {
        remove_disabled_connectors(get_connector_switches(node, out_connectors), out_connectors);
    }
    return {};
}

auto is_floor_layout(const erhe::scene::Node& node) -> bool
{
    if ((dynamic_cast<const erhe::scene::Curve*>(&node) == nullptr) || !carries_kit_layout(node)) {
        return false;
    }
    Kit_layout_data data;
    return read_kit_layout(node, data).empty() && (data.pattern.kind == Repeat_pattern_kind::floor);
}

auto get_floor_layout_connectors(const erhe::scene::Node& node, std::vector<Kit_connector>& out_connectors) -> std::string
{
    out_connectors.clear();
    const erhe::scene::Curve* const curve = dynamic_cast<const erhe::scene::Curve*>(&node);
    Kit_layout_data data;
    if ((curve == nullptr) || !read_kit_layout(node, data).empty() || (data.pattern.kind != Repeat_pattern_kind::floor)) {
        return fmt::format("'{}' is not a floor layout", node.get_name());
    }
    static thread_local std::vector<glm::vec2> outline;
    std::string error = read_floor_outline(curve->get_table(), outline);
    if (error.empty()) {
        error = check_floor_outline(outline);
    }
    if (!error.empty()) {
        return fmt::format("'{}': {}", node.get_name(), error);
    }
    get_floor_connectors(outline, data.pattern.floor, out_connectors);
    return {};
}

auto Level_kits::get_layout_child(const erhe::scene::Node& node) const -> std::optional<Kit_layout_child>
{
    // A Curve node carries a path, segments or stairs layout only (check_layout_kind()); unlocked, its children are ordinary pieces.
    const std::shared_ptr<erhe::scene::Node> parent = node.get_parent_node();
    const erhe::scene::Curve* const curve = dynamic_cast<const erhe::scene::Curve*>(parent.get());
    if ((curve == nullptr) || !is_locked_kit_layout(*curve)) {
        return {};
    }
    const erhe::scene::Trs_transform& local = node.parent_from_node_transform();
    return Kit_layout_child{
        .layout_id        = static_cast<std::uint64_t>(curve->get_id()),
        .hiding           = read_layout_connector_hiding(*curve),
        .role             = get_layout_child_role(node.get_index_in_parent(), curve->get_child_count(), curve->get_path().closure),
        .scale            = local.get_scale()
    };
}

auto Level_kits::get_connector_switches(const erhe::scene::Node& node) const -> Kit_connector_switches
{
    const std::optional<Kit_layout_child> child = get_layout_child(node);
    if (!child.has_value() || !child->hiding.hides_any()) {
        return read_piece_connector_switches(node);
    }
    std::vector<Kit_connector> connectors;
    static_cast<void>(get_local_connectors(node, connectors, Kit_connector_set::all));
    return get_connector_switches(node, connectors);
}

auto Level_kits::get_connector_switches(const erhe::scene::Node& node, const std::span<const Kit_connector> local_connectors) const -> Kit_connector_switches
{
    Kit_connector_switches switches = read_piece_connector_switches(node);
    const std::optional<Kit_layout_child> child = get_layout_child(node);
    if (!child.has_value() || !child->hiding.hides_any()) {
        return switches;
    }
    for (std::size_t i = 0, end = local_connectors.size(); i < end; ++i) {
        if (get_layout_connector_verdict(child.value(), local_connectors[i].outward).reason != Kit_layout_hide_reason::shown) {
            switches.hide_by_layout(i);
        }
    }
    return switches;
}

auto Level_kits::get_world_connectors(const erhe::scene::Node& node, std::vector<Kit_connector>& out_connectors, const Kit_connector_set set) const -> std::string
{
    out_connectors.clear();
    std::vector<Kit_connector> local_connectors;
    const std::string problem = get_local_connectors(node, local_connectors, set);
    if (!problem.empty()) {
        return problem;
    }
    if (!transform_connectors(local_connectors, node.world_from_node(), out_connectors)) {
        return fmt::format("'{}' has a zero scale axis in its world transform: its connector frames degenerate", node.get_name());
    }
    return {};
}

auto Level_kits::resolve_connect(
    const Scene_root&             scene_root,
    const Kit_parameter_values&   values,
    const Kit_connector_switches& switches,
    const Kit_connect&            connect,
    const glm::mat4&              world_from_node,
    Kit_join_pose&                out_motion
) const -> std::string
{
    const Kit_shape_definition* const shape = values.get_shape();
    if (shape == nullptr) {
        return "no shape";
    }
    std::vector<Kit_connector> connectors;
    get_shape_connectors(values, connectors);
    const Kit_connector* const piece_connector = find_connector(connectors, connect.connector);
    if (piece_connector == nullptr) {
        return fmt::format("{} has no connector '{}' (its connectors: {})", shape->id, connect.connector, connector_names_text(connectors));
    }
    const std::size_t piece_index = static_cast<std::size_t>(piece_connector - connectors.data());
    if (!switches.is_enabled(connect.connector, piece_index)) {
        const bool by_layout = switches.is_hidden_by_layout(piece_index);
        remove_disabled_connectors(switches, connectors);
        return fmt::format(
            "{} connector '{}' is {} (its enabled connectors: {})",
            shape->id, connect.connector, by_layout ? "hidden by its kit layout's connector switches" : "disabled", connector_names_text(connectors)
        );
    }
    if (!connect.target) {
        return "no target piece";
    }
    if (connect.target->get_item_host() != &scene_root) {
        return fmt::format("target '{}' is not in scene '{}'", connect.target->get_name(), scene_root.get_name());
    }
    if (!std::isfinite(connect.gap)) {
        return "the gap must be a finite number";
    }
    std::vector<Kit_connector> target_connectors;
    const std::string target_problem = get_world_connectors(*connect.target, target_connectors, Kit_connector_set::all);
    if (!target_problem.empty()) {
        return "target " + target_problem;
    }
    const Kit_connector* const target_connector = find_connector(target_connectors, connect.target_connector);
    if (target_connector == nullptr) {
        const std::optional<Kit_piece_data> record = read_kit_piece(*connect.target);
        return fmt::format(
            "target '{}' ({}) has no connector '{}' (its connectors: {})",
            connect.target->get_name(), record.has_value() ? record->shape : std::string{}, connect.target_connector,
            connector_names_text(target_connectors)
        );
    }
    const Kit_connector_switches target_switches = get_connector_switches(*connect.target);
    const std::size_t target_index = static_cast<std::size_t>(target_connector - target_connectors.data());
    if (!target_switches.is_enabled(connect.target_connector, target_index)) {
        const bool by_layout = target_switches.is_hidden_by_layout(target_index);
        remove_disabled_connectors(target_switches, target_connectors);
        return fmt::format(
            "target '{}' connector '{}' is {} (its enabled connectors: {})",
            connect.target->get_name(), connect.target_connector,
            by_layout ? "hidden by its kit layout's connector switches" : "disabled", connector_names_text(target_connectors)
        );
    }
    // C1.9: a join the author named is refused with both connectors' types.
    if (!is_compatible(*piece_connector, *target_connector)) {
        return fmt::format(
            "connector '{}' ({}) of {} and connector '{}' ({}) of target '{}' share no connector type",
            connect.connector, connector_types_text(piece_connector->types), shape->id,
            connect.target_connector, connector_types_text(target_connector->types), connect.target->get_name()
        );
    }
    const std::optional<Kit_join_pose> motion = join_motion(*piece_connector, world_from_node, *target_connector, connect.twist_steps, connect.gap);
    if (!motion.has_value()) {
        return fmt::format("{} connector '{}' degenerates under a zero scale axis", shape->id, connect.connector);
    }
    out_motion = motion.value();
    return {};
}

auto Level_kits::connect_piece(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& node, const Kit_connect& connect) -> Kit_connect_result
{
    Kit_connect_result result;
    result.node  = node;
    result.error = check_scene_node(scene_root, node);
    if (!result.error.empty()) {
        return result;
    }
    const std::optional<std::string> refusal = locked_layout_refusal(*node);
    if (refusal.has_value()) {
        result.error = refusal.value();
        return result;
    }
    const std::optional<Kit_piece_data> record = read_kit_piece(*node);
    if (!record.has_value()) {
        result.error = fmt::format("'{}' is not a kit piece", node->get_name());
        return result;
    }
    Kit_parameter_values values;
    const std::string problem = read_piece_values(record.value(), values);
    if (!problem.empty()) {
        result.error = fmt::format("'{}': {}", node->get_name(), problem);
        return result;
    }
    // The target stays where it is, so it may not move with the piece.
    for (std::shared_ptr<erhe::Hierarchy> ancestor = connect.target; ancestor; ancestor = ancestor->get_parent().lock()) {
        if (ancestor.get() == node.get()) {
            result.error = (connect.target == node)
                ? fmt::format("'{}' cannot connect to itself", node->get_name())
                : fmt::format("target '{}' is below '{}' and would move with it", connect.target->get_name(), node->get_name());
            return result;
        }
    }
    Kit_join_pose motion{};
    result.error = resolve_connect(scene_root, values, get_connector_switches(*node), connect, node->world_from_node(), motion);
    // A join without an attachment declines its record: Auto-connect of this change leaves the piece alone. The follow
    // operations made while the join is built keep the piece; nothing is left set after it.
    class Excluded_scope
    {
    public:
        explicit Excluded_scope(std::vector<std::shared_ptr<erhe::scene::Node>>& excluded) : m_excluded{excluded} {}
        ~Excluded_scope() noexcept { m_excluded.clear(); }
        Excluded_scope(const Excluded_scope&) = delete;
        auto operator=(const Excluded_scope&) -> Excluded_scope& = delete;
    private:
        std::vector<std::shared_ptr<erhe::scene::Node>>& m_excluded;
    };
    m_auto_connect_excluded.clear();
    const Excluded_scope excluded_scope{m_auto_connect_excluded};
    if (connect.attach == Kit_attach::none) {
        m_auto_connect_excluded.push_back(node);
    }
    if (!result.error.empty()) {
        return result;
    }
    // Step 41: the attachment the join records; a locked layout's child on either side makes the join without one.
    std::shared_ptr<Operation> attach_operation;
    Kit_new_connection         made;
    if (connect.attach == Kit_attach::attach) {
        // K6.2: one connection per connector, counting both ends; the piece's own record of its connector is replaced.
        result.error = check_one_connection_per_connector(scene_root, *node, connect.connector, *connect.target, connect.target_connector);
        if (!result.error.empty()) {
            return result;
        }
        // C3.2: cycles are allowed; a cycle that does not close is kept and reported apart (C3.7).
        Kit_attach_check check = check_attach(*node, *connect.target);
        if (check.refusal == Kit_attach_refusal::none) {
            made = make_join_connection(node, values, connect);
            attach_operation = make_attachment_operation(made.node, made.attachment);
            // The same pair recorded on the other end: the record moves to the holder, so the connection is kept once.
            std::shared_ptr<Operation> moved_record = make_connection_removal_operation(made.target, made.attachment.target_connector);
            const std::vector<Kit_attachment> target_records = read_kit_attachments(*made.target);
            const bool target_holds_pair = std::any_of(target_records.begin(), target_records.end(), [&](const Kit_attachment& a) {
                return (a.connector == made.attachment.target_connector) && (a.target == made.node->get_gltf_uid()) && (a.target_connector == made.attachment.connector);
            });
            if (target_holds_pair && moved_record && attach_operation) {
                attach_operation = std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = {std::move(moved_record), attach_operation}});
            }
        } else {
            result.not_attached = std::move(check.message);
        }
    }
    // D3.12: the piece keeps its own scale; the rigid motion is applied to its world matrix.
    const glm::mat4                  world_from_node = apply_motion(motion, node->world_from_node());
    const erhe::scene::Trs_transform world_after{world_from_node};
    result.position = glm::vec3{world_from_node[3]};
    result.rotation = world_after.get_rotation();
    if (is_identity_motion(motion)) {
        if (attach_operation) {
            // C3.4: the piece takes the values its new connection supplies, in the same entry.
            std::vector<std::shared_ptr<Operation>> operations{attach_operation};
            std::shared_ptr<Operation> flow = make_attachment_follow_operation(
                scene_root, {}, {}, nullptr, std::span<const std::shared_ptr<erhe::scene::Node>>{&made.node, 1}, std::span<const Kit_new_connection>{&made, 1}
            );
            if (flow) {
                operations.push_back(std::move(flow));
            }
            result.error   = execute_entry(std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)}));
            result.changed = result.error.empty();
        }
        return result;
    }

    // The local transform is stored as translation, rotation and scale, so a local matrix with shear cannot be held.
    const std::shared_ptr<erhe::scene::Node> parent = node->get_parent_node();
    const glm::mat4 parent_from_node = parent ? (parent->node_from_world() * world_from_node) : world_from_node;
    if (!has_orthogonal_axes(glm::mat3{parent_from_node})) {
        result.error = sheared_join_message(node->get_name(), parent ? parent->get_name() : std::string{});
        return result;
    }
    const erhe::scene::Trs_transform before = node->parent_from_node_transform();
    // Rebuilt from its components, so the stored local matrix is an exact TRS; the node's own scale is kept as stored.
    const erhe::scene::Trs_transform after = make_joined_local_transform(parent_from_node, before.get_scale());
    const std::optional<erhe::scene::Xform_op_stack> xform_op_stack_before = node->copy_xform_op_stack();
    // Applied first, so the layout move hook sees the moved pose (as set_node_transform does); the operation re-applies it.
    node->set_parent_from_node(after);
    node->update_world_from_node();
    std::vector<std::shared_ptr<Operation>> operations;
    operations.push_back(
        std::make_shared<Node_transform_operation>(
            Node_transform_operation::Parameters{
                .node                    = node,
                .parent_from_node_before = before,
                .parent_from_node_after  = after,
                .xform_op_stack_before   = xform_op_stack_before
            }
        )
    );
    // The attachment is written before the move hook's following, which then finds the piece on its new join.
    if (attach_operation) {
        operations.push_back(attach_operation);
    }
    append_layout_move_operations(scene_root, std::span<const std::shared_ptr<erhe::scene::Node>>{&node, 1}, operations);
    if (attach_operation) {
        // C3.4: the end that records the new connection takes the values it supplies, in the same entry.
        std::shared_ptr<Operation> flow = make_attachment_follow_operation(
            scene_root, {}, {}, nullptr, std::span<const std::shared_ptr<erhe::scene::Node>>{&made.node, 1}, std::span<const Kit_new_connection>{&made, 1}
        );
        if (flow) {
            operations.push_back(std::move(flow));
        }
    }
    result.error = execute_entry(
        (operations.size() == 1)
            ? operations.front()
            : std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)})
    );
    result.changed = result.error.empty();
    return result;
}

namespace {

// U33: a replaced piece keeps the switches of the connector names its new shape has; the others are cleared in the entry.
void append_connector_switch_carry(
    const std::shared_ptr<erhe::scene::Mesh>& mesh,
    const Kit_parameter_values&               values,
    std::vector<std::shared_ptr<Operation>>&  out_operations
)
{
    std::vector<Kit_connector> connectors;
    get_shape_connectors(values, connectors);
    const erhe::property::Dependency_property& enabled_property  = Kit_piece::connectors_enabled_property.get();
    const erhe::property::Dependency_property& disabled_property = Kit_piece::disabled_connectors_property.get();
    const std::optional<erhe::property::Local_state> enabled_before = mesh->read_local_state(enabled_property);
    if (connectors.empty() && enabled_before.has_value()) {
        out_operations.push_back(std::make_shared<Property_set_operation>(mesh, enabled_property, enabled_before, std::optional<erhe::property::Local_state>{}));
    }
    const std::optional<erhe::property::Local_state> disabled_before = mesh->read_local_state(disabled_property);
    if (!disabled_before.has_value()) {
        return;
    }
    std::vector<std::string> kept;
    for (const std::string& name : parse_connector_names(mesh->get_value(Kit_piece::disabled_connectors_property))) {
        if (find_connector(connectors, name) != nullptr) {
            kept.push_back(name);
        }
    }
    const std::string text = make_connector_names_text(std::move(kept));
    if (text == mesh->get_value(Kit_piece::disabled_connectors_property)) {
        return;
    }
    out_operations.push_back(
        std::make_shared<Property_set_operation>(
            mesh, disabled_property, disabled_before,
            text.empty() ? std::optional<erhe::property::Local_state>{} : std::optional<erhe::property::Local_state>{erhe::property::Property_value{text}}
        )
    );
}

} // anonymous namespace

auto Level_kits::check_connector_switch_write(
    const Kit_piece_data&                      record,
    const erhe::property::Dependency_property& property,
    const erhe::property::Property_value&      value
) const -> std::string
{
    if (!is_connector_switch_property(property)) {
        return {};
    }
    Kit_parameter_values values;
    const std::string problem = read_piece_values(record, values);
    if (!problem.empty()) {
        return fmt::format("the piece's record does not read: {}", problem);
    }
    std::vector<Kit_connector> connectors;
    get_shape_connectors(values, connectors);
    if (&property == &Kit_piece::connectors_enabled_property.get()) {
        if (std::get_if<bool>(&value) == nullptr) {
            return "Kit_piece.connectors_enabled takes true or false";
        }
        return connectors.empty() ? fmt::format("{} has no connectors to switch", record.shape) : std::string{};
    }
    const std::string* const text = std::get_if<std::string>(&value);
    if (text == nullptr) {
        return "Kit_piece.disabled_connectors takes a text";
    }
    const std::string error = check_disabled_connectors_text(*text, connectors);
    return error.empty() ? std::string{} : fmt::format("{}: {}", record.shape, error);
}

auto Level_kits::check_connector_switch_write(
    const erhe::Item_base&                     item,
    const erhe::property::Dependency_property& property,
    const erhe::property::Property_value&      value
) const -> std::string
{
    if (!is_connector_switch_property(property)) {
        return {};
    }
    const erhe::scene::Node* const node = dynamic_cast<const erhe::scene::Node*>(&item);
    const std::optional<Kit_piece_data> record = (node != nullptr) ? read_kit_piece(*node) : std::optional<Kit_piece_data>{};
    if (!record.has_value()) {
        return fmt::format("'{}' is not a kit piece", item.get_name());
    }
    return check_connector_switch_write(record.value(), property, value);
}

auto Level_kits::set_connector_switches(
    Scene_root&                               scene_root,
    const std::shared_ptr<erhe::scene::Node>& node,
    const Kit_connector_switch_edit&          edit
) -> Kit_connector_switch_result
{
    Kit_connector_switch_result result;
    result.error = check_scene_node(scene_root, node);
    if (!result.error.empty()) {
        return result;
    }
    const std::optional<std::string> refusal = locked_layout_refusal(*node);
    if (refusal.has_value()) {
        result.error = refusal.value();
        return result;
    }
    std::vector<Kit_connector> connectors;
    result.error = get_local_connectors(*node, connectors, Kit_connector_set::all);
    if (!result.error.empty()) {
        return result;
    }
    if (connectors.empty()) {
        const std::optional<Kit_piece_data> record = read_kit_piece(*node);
        result.error = fmt::format("'{}' ({}) has no connectors to switch", node->get_name(), record.has_value() ? record->shape : std::string{});
        return result;
    }
    const Kit_connector_switches before = read_piece_connector_switches(*node);
    Kit_connector_switches after = before;
    if (edit.piece.has_value()) {
        after.connectors_enabled = (edit.piece.value() == Kit_connector_state::enabled);
    }
    for (const Kit_connector_switch_edit::Connector& connector : edit.connectors) {
        if (find_connector(connectors, connector.name) == nullptr) {
            const std::optional<Kit_piece_data> record = read_kit_piece(*node);
            result.error = fmt::format(
                "'{}' ({}) has no connector '{}' (its connectors: {})",
                node->get_name(), record.has_value() ? record->shape : std::string{}, connector.name, connector_names_text(connectors)
            );
            return result;
        }
        after.disabled_connectors = set_connector_state(after.disabled_connectors, connector.name, connector.state);
    }
    result.switches = after;
    std::vector<std::shared_ptr<Operation>> operations;
    // The default value is written as no local value, so a piece with every switch on carries none in the file.
    if (after.connectors_enabled != before.connectors_enabled) {
        operations.push_back(
            std::make_shared<Property_set_operation>(
                node, Kit_piece::connectors_enabled_property.get(),
                node->read_local_state(Kit_piece::connectors_enabled_property.get()),
                after.connectors_enabled ? std::optional<erhe::property::Local_state>{} : std::optional<erhe::property::Local_state>{erhe::property::Property_value{false}}
            )
        );
    }
    if (after.disabled_connectors != before.disabled_connectors) {
        operations.push_back(
            std::make_shared<Property_set_operation>(
                node, Kit_piece::disabled_connectors_property.get(),
                node->read_local_state(Kit_piece::disabled_connectors_property.get()),
                after.disabled_connectors.empty()
                    ? std::optional<erhe::property::Local_state>{}
                    : std::optional<erhe::property::Local_state>{erhe::property::Property_value{after.disabled_connectors}}
            )
        );
    }
    if (operations.empty()) {
        return result;
    }
    result.error = execute_entry(
        (operations.size() == 1)
            ? operations.front()
            : std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)})
    );
    result.changed = result.error.empty();
    return result;
}

auto Level_kits::get_subtree_bounds(const erhe::scene::Node& node) -> erhe::math::Aabb
{
    erhe::math::Aabb mesh_bounds{};
    erhe::math::Aabb origin_bounds{};
    include_subtree_bounds(node, mesh_bounds, origin_bounds);
    return mesh_bounds.is_valid_3d() ? mesh_bounds : origin_bounds;
}

auto Level_kits::group(Scene_root& scene_root, const std::span<const std::shared_ptr<erhe::scene::Node>> nodes, const std::string_view name) -> Kit_group_result
{
    Kit_group_result result;
    if (nodes.empty()) {
        result.error = "no nodes to group";
        return result;
    }
    for (std::size_t i = 0, end = nodes.size(); i < end; ++i) {
        result.error = check_scene_node(scene_root, nodes[i]);
        if (!result.error.empty()) {
            return result;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (nodes[j] == nodes[i]) {
                result.error = fmt::format("node '{}' is listed twice", nodes[i]->get_name());
                return result;
            }
        }
    }
    // A node whose ancestor is in the set goes with that ancestor.
    std::vector<std::shared_ptr<erhe::scene::Node>> top_level;
    for (const std::shared_ptr<erhe::scene::Node>& node : nodes) {
        const bool has_listed_ancestor = std::any_of(
            nodes.begin(), nodes.end(),
            [&node](const std::shared_ptr<erhe::scene::Node>& other) -> bool { return (other != node) && node->is_ancestor(other.get()); }
        );
        if (!has_listed_ancestor) {
            top_level.push_back(node);
        }
    }
    std::shared_ptr<erhe::Hierarchy> parent = top_level.front()->get_parent().lock();
    for (const std::shared_ptr<erhe::scene::Node>& node : top_level) {
        if (node->get_parent().lock() != parent) {
            parent = scene_root.get_scene().get_root_node();
            break;
        }
    }
    const std::optional<std::string> child_refusal = instance_child_refusal(*parent);
    if (child_refusal.has_value()) {
        result.error = child_refusal.value();
        return result;
    }

    erhe::math::Aabb mesh_bounds{};
    erhe::math::Aabb origin_bounds{};
    for (const std::shared_ptr<erhe::scene::Node>& node : top_level) {
        include_subtree_bounds(*node, mesh_bounds, origin_bounds);
    }
    const glm::vec3 origin = get_group_origin(mesh_bounds.is_valid_3d() ? mesh_bounds : origin_bounds);

    std::shared_ptr<erhe::scene::Xform> group_node = std::make_shared<erhe::scene::Xform>(name.empty() ? std::string_view{"Group"} : name);
    group_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    // Detached, the world transform is the local one; the insert keeps it.
    group_node->set_world_from_node(erhe::math::create_translation<float>(origin));
    const std::size_t index_in_parent = (top_level.front()->get_parent().lock() == parent)
        ? top_level.front()->get_index_in_parent()
        : parent->get_child_count();

    Compound_operation::Parameters parameters;
    parameters.operations.push_back(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context         = m_context,
                .item            = group_node,
                .parent          = parent,
                .mode            = Item_insert_remove_operation::Mode::insert,
                .index_in_parent = index_in_parent
            }
        )
    );
    for (const std::shared_ptr<erhe::scene::Node>& node : top_level) {
        // A parent change keeps the world transform (Xformable::set_parent).
        parameters.operations.push_back(
            std::make_shared<Item_parent_change_operation>(group_node, node, std::shared_ptr<erhe::Hierarchy>{}, std::shared_ptr<erhe::Hierarchy>{})
        );
    }
    result.error = execute_entry(std::make_shared<Compound_operation>(std::move(parameters)));
    result.node  = result.error.empty() ? group_node : std::shared_ptr<erhe::scene::Node>{};
    return result;
}

auto Level_kits::ungroup(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& group) -> Kit_ungroup_result
{
    Kit_ungroup_result result;
    result.error = check_scene_node(scene_root, group);
    if (!result.error.empty()) {
        return result;
    }
    if (dynamic_cast<const erhe::scene::Xform*>(group.get()) == nullptr) {
        result.error = fmt::format("'{}' is a {}, not a group node (an Xform)", group->get_name(), group->get_class_type_name());
        return result;
    }
    result.parent   = group->get_parent().lock();
    result.children = group->get_children();
    // Removing a node moves its children to its parent, world transforms kept, and restores them on undo.
    m_context.operation_stack->execute_now(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = group,
                .parent  = result.parent,
                .mode    = Item_insert_remove_operation::Mode::remove
            }
        )
    );
    return result;
}

auto Level_kits::duplicate(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& node, const Kit_group_copy& copy) -> Kit_group_result
{
    Kit_group_result result;
    result.error = check_scene_node(scene_root, node);
    if (!result.error.empty()) {
        return result;
    }
    const std::shared_ptr<erhe::Hierarchy> parent = node->get_parent().lock();
    const std::optional<std::string> child_refusal = instance_child_refusal(*parent);
    if (child_refusal.has_value()) {
        result.error = child_refusal.value();
        return result;
    }
    // The clone holds every node of the subtree with its values: records, brush placements and bodies.
    std::shared_ptr<erhe::scene::Node> clone = std::dynamic_pointer_cast<erhe::scene::Node>(node->clone());
    if (!clone) {
        result.error = fmt::format("'{}' cannot be copied", node->get_name());
        return result;
    }
    clone->set_name(copy.name.empty() ? (node->get_name() + " Copy") : copy.name);
    glm::mat4 world_from_node = node->world_from_node();
    const glm::vec3 source_position{world_from_node[3]};
    const glm::vec3 position = (copy.placement == Kit_copy_placement::position) ? copy.value : (source_position + copy.value);
    world_from_node[3] = glm::vec4{position, 1.0f};
    clone->set_world_from_node(world_from_node);
    set_copied_brush_collision_shapes(scene_root, *clone);
    m_context.operation_stack->execute_now(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context         = m_context,
                .item            = clone,
                .parent          = parent,
                .mode            = Item_insert_remove_operation::Mode::insert,
                .index_in_parent = node->get_index_in_parent() + 1
            }
        )
    );
    result.node = clone;
    return result;
}

auto Level_kits::mirror(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& group, const Mirror_axis axis) -> Kit_mirror_result
{
    Kit_mirror_result result;
    result.error = check_scene_node(scene_root, group);
    if (!result.error.empty()) {
        return result;
    }
    std::vector<std::shared_ptr<erhe::scene::Node>> nodes;
    collect_descendant_nodes(*group, nodes);
    if (nodes.empty()) {
        result.error = fmt::format("'{}' has no child nodes to mirror", group->get_name());
        return result;
    }
    for (const std::shared_ptr<erhe::scene::Node>& node : nodes) {
        const std::optional<std::string> refusal = instance_structure_refusal(*node);
        if (refusal.has_value()) {
            result.error = refusal.value();
            return result;
        }
    }
    const erhe::scene::Trs_transform group_world = group->world_from_node_transform();
    result.plane_point  = group_world.get_translation();
    result.plane_normal = glm::normalize(group_world.get_rotation() * get_axis_vector(axis));

    // Every node's new world transform first, then its local transform under its parent's new one.
    std::map<const erhe::scene::Node*, glm::mat4> new_worlds;
    std::vector<std::shared_ptr<Operation>>       operations;
    for (const std::shared_ptr<erhe::scene::Node>& node : nodes) {
        const erhe::scene::Trs_transform world = node->world_from_node_transform();
        const erhe::scene::Mesh* const   mesh  = dynamic_cast<const erhe::scene::Mesh*>(node.get());
        const std::optional<Kit_piece_data> record = read_kit_piece(*node);
        const Kit_shape_definition* const shape = record.has_value() ? m_library.find(record.value().shape) : nullptr;
        const Mirrored_pose mirrored = mirror_pose(
            Group_pose{.position = world.get_translation(), .rotation = world.get_rotation(), .scale = world.get_scale()},
            result.plane_point,
            result.plane_normal,
            (shape != nullptr) ? std::span<const Kit_mirror_plane>{shape->mirror_planes} : std::span<const Kit_mirror_plane>{}
        );
        // A node without a mesh has no image to be exact about; a locked kit layout's run keeps its own direction.
        const Mirror_image image = is_locked_kit_layout(*node)
            ? Mirror_image::not_exact
            : ((mesh == nullptr) ? Mirror_image::exact : mirrored.image);
        const glm::mat4 new_world =
            erhe::math::create_translation<float>(mirrored.pose.position) *
            glm::mat4_cast(mirrored.pose.rotation) *
            erhe::math::create_scale<float>(mirrored.pose.scale);
        new_worlds.emplace(node.get(), new_world);
        const std::shared_ptr<erhe::scene::Node> parent = node->get_parent_node();
        const std::map<const erhe::scene::Node*, glm::mat4>::const_iterator parent_new = new_worlds.find(parent.get());
        const glm::mat4 parent_world = (parent_new != new_worlds.end())
            ? parent_new->second
            : (parent ? parent->world_from_node() : glm::mat4{1.0f});
        operations.push_back(
            std::make_shared<Node_transform_operation>(
                Node_transform_operation::Parameters{
                    .node                    = node,
                    .parent_from_node_before = node->parent_from_node_transform(),
                    .parent_from_node_after  = erhe::scene::Trs_transform{glm::inverse(parent_world) * new_world},
                    .xform_op_stack_before   = node->copy_xform_op_stack()
                }
            )
        );
        // A curve's points reflect across its local x = 0 plane, the plane its new rotation reflects across, so the
        // curve is the exact mirror image; a locked path layout lays out again along it (Curve_set_operation).
        const std::shared_ptr<erhe::scene::Curve> curve = std::dynamic_pointer_cast<erhe::scene::Curve>(node);
        if (curve) {
            erhe::math::Curve_path reflected;
            transform_curve_path(curve->get_path(), erhe::math::create_scale<float>(glm::vec3{-1.0f, 1.0f, 1.0f}), reflected);
            operations.push_back(std::make_shared<Curve_set_operation>(curve, curve->get_path(), reflected, "mirror"));
        }
        result.nodes.push_back(
            Kit_mirrored_node{
                .node     = node,
                .position = mirrored.pose.position,
                .rotation = mirrored.pose.rotation,
                .plane    = mirrored.plane,
                .image    = image
            }
        );
        if (image == Mirror_image::not_exact) {
            ++result.not_exact_count;
        }
    }
    // Step 41: the pieces attached to the group's content follow it; its pieces moved off a join outside it detach.
    std::shared_ptr<Operation> follow = make_attachment_follow_operation(scene_root, std::span<const std::shared_ptr<erhe::scene::Node>>{&group, 1}, {});
    if (follow) {
        operations.push_back(std::move(follow));
    }
    result.error = execute_entry(std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)}));
    return result;
}

namespace {

void apply_preview_primitives(App_context& context, const std::shared_ptr<erhe::scene::Mesh>& mesh, const std::vector<erhe::scene::Mesh_primitive>& primitives)
{
    erhe::Item_host* const item_host = mesh->get_item_host();
    if (item_host == nullptr) {
        // A removed piece: no raytrace scene to update and nothing shows it.
        mesh->set_primitives(primitives);
        return;
    }
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};
        swap_mesh_primitives(mesh, primitives);
    }
    context.app_message_bus->mesh_geometry_changed.send_message(Mesh_geometry_changed_message{.mesh = mesh});
}

// The checks set_parameters() and preview_parameters() share; returns the mesh and record, or the refusal.
[[nodiscard]] auto check_piece_values(
    const Kit_shape_library&                  library,
    const std::shared_ptr<erhe::scene::Node>& node,
    const Kit_parameter_values&               values,
    std::shared_ptr<erhe::scene::Mesh>&       out_mesh,
    Kit_piece_data&                           out_record
) -> std::string
{
    out_mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(node);
    if (!out_mesh || (out_mesh->get_item_host() == nullptr)) {
        return "the node is not a mesh of a scene";
    }
    const std::optional<std::string> refusal = locked_layout_refusal(*out_mesh);
    if (refusal.has_value()) {
        return refusal.value();
    }
    const std::optional<Kit_piece_data> record = read_kit_piece(*out_mesh);
    if (!record.has_value()) {
        return fmt::format("node '{}' is not a kit piece", out_mesh->get_name());
    }
    out_record = record.value();
    const Kit_shape_definition* const shape = values.get_shape();
    if ((shape == nullptr) || (library.find(out_record.shape) != shape)) {
        return fmt::format("the parameter values are not of the piece's shape {}", out_record.shape);
    }
    return values.validate();
}

} // anonymous namespace

auto Level_kits::is_previewing(const erhe::scene::Node& node) const -> bool
{
    return m_parameter_preview.has_value() && (m_parameter_preview.value().mesh.get() == &node);
}

void Level_kits::cancel_parameter_preview()
{
    if (!m_parameter_preview.has_value()) {
        return;
    }
    Parameter_preview preview = std::move(m_parameter_preview.value());
    m_parameter_preview.reset();
    apply_preview_primitives(m_context, preview.mesh, preview.before.primitives);
    restore_preview_lights(*preview.mesh);
    restore_follow_preview(m_parameter_follow_preview);
}

void Level_kits::restore_preview_lights(const erhe::scene::Mesh& mesh)
{
    erhe::Item_host* const item_host = mesh.get_item_host();
    if (item_host == nullptr) {
        m_light_sync.restore_preview();
        return;
    }
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};
    m_light_sync.restore_preview();
}

void Level_kits::on_selection()
{
    // The Level Kit window publishes its Connect choice again for the new selection.
    m_connector_indicators.clear_connect_choice();
    m_layout_indicators.end_pending_unless_selected();
    if (m_parameter_preview.has_value() && !m_parameter_preview.value().mesh->is_selected()) {
        cancel_parameter_preview();
    }
    if (is_layout_preview_lost()) {
        queue_end_layout_preview();
    }
}

auto Level_kits::is_layout_preview_lost() const -> bool
{
    for (const Layout_preview_entry& entry : m_layout_preview) {
        if ((entry.selected_at_begin && !entry.node->is_selected()) || (entry.node->get_item_host() == nullptr)) {
            return true;
        }
    }
    return false;
}

void Level_kits::queue_end_layout_preview()
{
    // Not inside the message's dispatch: the end removes and inserts pieces, which sends messages of its own.
    if (m_layout_preview_end_queued) {
        return;
    }
    m_layout_preview_end_queued = true;
    m_context.imgui_windows->queue(
        [this]() {
            m_layout_preview_end_queued = false;
            // A preview begun since the message keeps running.
            if (is_layout_preview_lost()) {
                end_layout_preview();
            }
        }
    );
}

void Level_kits::on_items_removed(const Removed_items& removed)
{
    // The cached connector frames of removed pieces go (a regenerated layout's children are new nodes).
    m_connector_indicators.forget_removed(removed);
    m_layout_indicators.forget_removed(removed);
    if (m_parameter_preview.has_value() && removed.lookup.contains(m_parameter_preview.value().mesh.get())) {
        cancel_parameter_preview();
    }
    for (const Layout_preview_entry& entry : m_layout_preview) {
        if (removed.lookup.contains(static_cast<const erhe::Item_base*>(entry.node.get()))) {
            queue_end_layout_preview();
            break;
        }
    }
}

void Level_kits::on_close_scene(const erhe::Item_host* const closing_host)
{
    // A closing scene's queued refresh goes with it; its on_done is not called.
    std::erase_if(m_scene_refreshes, [closing_host](const Scene_refresh& refresh) -> bool { return refresh.scene_root.get() == closing_host; });
    clear_flow_summary();
    m_connector_indicators.forget_all();
    m_layout_indicators.forget_all();
    if (m_parameter_preview.has_value() && (m_parameter_preview.value().mesh->get_item_host() == closing_host)) {
        cancel_parameter_preview();
    }
    // The closing scene's layouts go with it; nothing is restored.
    if (!m_layout_preview.empty() && (m_layout_preview.front().node->get_item_host() == closing_host)) {
        m_layout_preview.clear();
        m_layout_transform_preview = false;
    }
    // So do its followers and its flow preview.
    if ((m_parameter_follow_preview.scene_root != nullptr) && (static_cast<const erhe::Item_host*>(m_parameter_follow_preview.scene_root) == closing_host)) {
        m_parameter_follow_preview.moves.clear();
        restore_follow_preview(m_parameter_follow_preview);
    }
    if ((m_flow_preview.scene_root != nullptr) && (static_cast<const erhe::Item_host*>(m_flow_preview.scene_root) == closing_host)) {
        forget_flow_preview();
    }
}

auto Level_kits::preview_parameters(const std::shared_ptr<erhe::scene::Node>& node, const Kit_parameter_values& values) -> std::string
{
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    std::shared_ptr<erhe::scene::Mesh> mesh;
    Kit_piece_data                     record;
    const std::string error = check_piece_values(m_library, node, values, mesh, record);
    if (!error.empty()) {
        return error;
    }
    std::shared_ptr<Brush>     brush  = get_brush(values);
    const Brush::Scaled* const scaled = brush ? brush->get_scaled(1.0) : nullptr;
    if (scaled == nullptr) {
        return fmt::format("{}: the shape made no geometry", record.shape);
    }
    if (mesh->get_primitives().size() != scaled->part_primitives.size()) {
        return part_count_problem(*mesh, *scaled);
    }
    if (m_parameter_preview.has_value() && (m_parameter_preview.value().mesh != mesh)) {
        cancel_parameter_preview();
    }
    if (!m_parameter_preview.has_value()) {
        const std::optional<Brush_placement_data> placement = read_brush_placement(*mesh);
        m_parameter_preview = Parameter_preview{
            .mesh   = mesh,
            .before = Kit_piece_mesh_state{
                .shape           = record.shape,
                .primitives      = mesh->get_primitives(),
                .collision_shape = get_node_source_collision_shape(*mesh),
                .brush           = placement.has_value() ? placement.value().brush : std::shared_ptr<Brush>{},
                .parameters      = record.parameters,
                .motion_mode     = get_piece_motion_mode(*mesh),
                .ground          = get_piece_ground(*mesh)
            },
            .brush  = {}
        };
    }
    // The brush shown holds its cache entry; the previous step's brush goes with its last holder (D2.2).
    m_parameter_preview.value().brush = brush;
    apply_preview_primitives(m_context, mesh, with_brush_primitives(m_parameter_preview.value().before.primitives, *scaled));
    // U43: the kit lights follow the step, as the release will put them.
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{mesh->get_item_host()->item_host_mutex};
        m_light_sync.preview(mesh, values);
    }
    // Step 41: the pieces attached to it (and the piece itself when attached) join its step's connectors.
    step_parameter_follow_preview(mesh, values);
    log_scene->trace(
        "Kit piece '{}' preview step {}: {:.3f} ms", mesh->get_name(), get_mesh_key(values),
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()
    );
    return {};
}

auto Level_kits::set_parameters(const std::shared_ptr<erhe::scene::Node>& node, const Kit_parameter_values& values) -> Kit_set_parameters_result
{
    Kit_set_parameters_result          result;
    std::shared_ptr<erhe::scene::Mesh> mesh;
    Kit_piece_data                     record;
    result.error = check_piece_values(m_library, node, values, mesh, record);
    if (!result.error.empty()) {
        if (is_previewing(*node)) {
            cancel_parameter_preview();
        }
        return result;
    }
    result.brush = get_brush(values);
    const Brush::Scaled* const scaled = result.brush ? result.brush->get_scaled(1.0) : nullptr;
    if (scaled == nullptr) {
        result.brush.reset();
        result.error = fmt::format("{}: the shape made no geometry", record.shape);
        if (is_previewing(*node)) {
            cancel_parameter_preview();
        }
        return result;
    }
    if (mesh->get_primitives().size() != scaled->part_primitives.size()) {
        result.brush.reset();
        result.error = part_count_problem(*mesh, *scaled);
        if (is_previewing(*node)) {
            cancel_parameter_preview();
        }
        return result;
    }

    // With a live preview the change goes from the state before the drag (D3.6).
    Kit_piece_mesh_state before;
    if (is_previewing(*mesh)) {
        before = std::move(m_parameter_preview.value().before);
        m_parameter_preview.reset();
        // The release's light moves are recorded from where the lights stood before the drag.
        restore_preview_lights(*mesh);
        // The followers' moves too: the entry's following records them from their poses before the drag.
        restore_follow_preview(m_parameter_follow_preview);
    } else {
        const std::optional<Brush_placement_data> placement = read_brush_placement(*mesh);
        before = Kit_piece_mesh_state{
            .shape           = record.shape,
            .primitives      = mesh->get_primitives(),
            .collision_shape = get_node_source_collision_shape(*mesh),
            .brush           = placement.has_value() ? placement.value().brush : std::shared_ptr<Brush>{},
            .parameters      = record.parameters,
            .motion_mode     = get_piece_motion_mode(*mesh),
            .ground          = get_piece_ground(*mesh)
        };
    }
    const std::string parameters = values.to_canonical_text();
    if (parameters == before.parameters) {
        // A drag that ends where it began shows the state it began with and records nothing.
        apply_preview_primitives(m_context, mesh, before.primitives);
        return result;
    }

    // The piece keeps its materials, slot by slot; only the shared primitives change.
    Kit_piece_mesh_state after{
        .shape           = values.get_shape()->id, // a former id migrates to the shape's id
        .primitives      = with_brush_primitives(before.primitives, *scaled),
        .collision_shape = carries_node_physics(*mesh) ? scaled->collision_shape : before.collision_shape,
        .brush           = result.brush,
        .parameters      = parameters,
        .motion_mode     = before.motion_mode,
        .ground          = before.ground
    };
    std::vector<std::shared_ptr<Operation>> operations;
    operations.push_back(std::make_shared<Kit_piece_parameters_operation>(mesh, std::move(before), std::move(after)));
    Scene_root* const scene_root = dynamic_cast<Scene_root*>(mesh->get_item_host());
    if (scene_root != nullptr) {
        m_light_sync.append(m_context, *scene_root, mesh, values, operations);
        // Step 41: the pieces attached to it follow its moved connectors, and the piece its own when attached.
        const std::shared_ptr<erhe::scene::Node> changed = mesh;
        std::shared_ptr<Operation> follow = make_attachment_follow_operation(*scene_root, {}, std::span<const std::shared_ptr<erhe::scene::Node>>{&changed, 1});
        if (follow) {
            operations.push_back(std::move(follow));
        }
    }
    result.error = execute_entry(
        (operations.size() == 1)
            ? operations.front()
            : std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)})
    );
    return result;
}

auto Level_kits::set_slot_material(
    Scene_root&                               scene_root,
    const std::shared_ptr<erhe::scene::Node>& node,
    const std::size_t                         slot,
    const Kit_slot_material&                  material
) -> Kit_set_material_result
{
    Kit_set_material_result result;
    const std::shared_ptr<erhe::scene::Mesh> mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(node);
    if (!mesh || (mesh->get_item_host() != &scene_root)) {
        result.error = fmt::format("the node is not a mesh of scene '{}'", scene_root.get_name());
        return result;
    }
    const std::optional<std::string> refusal = locked_layout_refusal(*mesh);
    if (refusal.has_value()) {
        result.error = refusal.value();
        return result;
    }
    const std::optional<Kit_piece_data> record = read_kit_piece(*mesh);
    if (!record.has_value()) {
        result.error = fmt::format("node '{}' is not a kit piece", mesh->get_name());
        return result;
    }
    const Kit_shape_definition* const shape = m_library.find(record.value().shape);
    if (shape == nullptr) {
        result.error = fmt::format("the kits have no shape '{}'", record.value().shape);
        return result;
    }
    if (slot >= shape->material_slots.size()) {
        result.error = fmt::format("{} has {} material slots, not slot {}", shape->id, shape->material_slots.size(), slot);
        return result;
    }
    if (mesh->get_primitives().size() != shape->material_slots.size()) {
        result.error = fmt::format(
            "the mesh has {} primitives, the shape {} material slots", mesh->get_primitives().size(), shape->material_slots.size()
        );
        return result;
    }
    const std::shared_ptr<Content_library> library = scene_root.get_content_library();
    if (!library) {
        result.error = "the scene has no content library";
        return result;
    }
    std::vector<Kit_slot_material> given(slot + 1);
    given[slot] = material;
    result.error = check_slot_materials(m_library, *shape, given);
    if (!result.error.empty()) {
        return result;
    }
    result.previous = mesh->get_primitives()[slot].material;
    Palette_material_resolver resolver{m_context, library};
    result.material = resolver.resolve(m_library, shape->material_slots[slot], material);
    // A palette material already in the library is found there: an unchanged slot records nothing.
    const std::shared_ptr<Mesh_material_assign_operation> assign = make_mesh_material_assign_operation(mesh, slot, result.material);
    if (!assign) {
        return result;
    }
    result.changed = true;
    Compound_operation::Parameters parameters;
    parameters.operations = resolver.take_operations();
    parameters.operations.push_back(assign);
    // C3.6, K7.5: the edited piece's neighbours take the material it now supplies, in the same entry.
    std::shared_ptr<Operation> flow = make_attachment_follow_operation(scene_root, {}, std::span<const std::shared_ptr<erhe::scene::Node>>{&node, 1});
    if (flow) {
        parameters.operations.push_back(std::move(flow));
    }
    result.error = execute_entry(
        (parameters.operations.size() == 1)
            ? parameters.operations.front()
            : std::make_shared<Compound_operation>(std::move(parameters))
    );
    result.changed = result.error.empty();
    return result;
}

auto Level_kits::make_replace_values(
    const Scene_root&                 scene_root,
    const erhe::scene::Node&          node,
    const Kit_shape_definition&       shape,
    Kit_parameter_values&             out_values,
    std::vector<Kit_parameter_carry>& out_carry
) const -> std::string
{
    const std::optional<Kit_piece_data> record = read_kit_piece(node);
    if (!record.has_value()) {
        return fmt::format("node '{}' is not a kit piece", node.get_name());
    }
    Kit_parameter_values old_values;
    const std::string problem = read_piece_values(record.value(), old_values);
    if (!problem.empty()) {
        return fmt::format("piece '{}': {}", node.get_name(), problem);
    }
    carry_parameter_values(old_values, shape, get_lengths(scene_root), out_values, out_carry);
    return {};
}

auto Level_kits::find_replace_skip(const std::shared_ptr<erhe::scene::Node>& node_in) const -> std::optional<Kit_replace_skipped>
{
    const erhe::scene::Node&                 node   = *node_in;
    const std::shared_ptr<erhe::scene::Mesh> mesh   = std::dynamic_pointer_cast<erhe::scene::Mesh>(node_in);
    const std::optional<std::string>         locked = locked_layout_refusal(node);
    const std::optional<Kit_piece_data>      record = mesh ? read_kit_piece(*mesh) : std::optional<Kit_piece_data>{};
    Kit_parameter_values old_values;
    const std::string record_problem = record.has_value() ? read_piece_values(record.value(), old_values) : std::string{};
    const Kit_shape_definition* const old_shape = (record.has_value() && record_problem.empty()) ? old_values.get_shape() : nullptr;
    const Kit_replace_target_facts facts{
        .kit_layout_node     = carries_kit_layout(node),
        .below_locked_layout = locked.has_value(),
        .mesh                = static_cast<bool>(mesh),
        .group_node          = (dynamic_cast<const erhe::scene::Xform*>(&node) != nullptr),
        .kit_piece_record    = record.has_value(),
        .record_reads        = (old_shape != nullptr),
        .slot_count_matches  = (old_shape != nullptr) && (mesh->get_primitives().size() == old_shape->material_slots.size())
    };
    const std::optional<Kit_replace_skip> skip = classify_replace_target(facts);
    if (!skip.has_value()) {
        return {};
    }
    std::string message;
    switch (skip.value()) {
        case Kit_replace_skip::kit_layout_node:
            message = fmt::format("'{}' is a kit layout node: the shapes of its items are its own values (level_set_layout)", node.get_name());
            break;
        case Kit_replace_skip::locked_layout_child:
            message = locked.value();
            break;
        case Kit_replace_skip::unreadable_record:
            message = fmt::format("piece '{}': {}", node.get_name(), record_problem);
            break;
        case Kit_replace_skip::loaded_mesh:
            message = fmt::format(
                "piece '{}' keeps a loaded mesh: it has {} primitives, its shape {} material slots",
                node.get_name(), mesh->get_primitives().size(), old_shape->material_slots.size()
            );
            break;
        default:
            message = fmt::format("'{}' is {}", node.get_name(), describe(skip.value()));
            break;
    }
    return Kit_replace_skipped{.node = node_in, .skip = skip.value(), .message = std::move(message)};
}

auto Level_kits::replace_shape(
    Scene_root&                            scene_root,
    const Kit_shape_definition&            shape,
    const std::span<const Kit_replacement> replacements,
    const Surface_options&                 surface
) -> Kit_replace_result
{
    Kit_replace_result result;
    const auto fail = [](const std::size_t index, std::string message) -> Kit_replace_result {
        Kit_replace_result failed;
        failed.failed_index = index;
        failed.error        = std::move(message);
        return failed;
    };
    if (replacements.empty()) {
        result.error = "no pieces to replace";
        return result;
    }
    const std::shared_ptr<Content_library> library = scene_root.get_content_library();
    if (!library) {
        result.error = "the scene has no content library";
        return result;
    }
    if (any_surface_option(surface)) {
        if (!(std::abs(glm::length(surface.direction) - 1.0f) < 1.0e-3f)) {
            result.error = "the surface direction must be a unit vector";
            return result;
        }
        std::string distance_problem = check_surface_max_distance(surface.forward_distance, "surface forward distance");
        if (distance_problem.empty()) {
            distance_problem = check_surface_max_distance(surface.backward_distance, "surface backward distance");
        }
        if (!distance_problem.empty()) {
            result.error = distance_problem;
            return result;
        }
    }
    const Kit_lengths lengths = get_lengths(scene_root);

    // Every node is checked before anything changes; the ones that cannot be replaced are skipped (U24).
    std::vector<std::size_t> piece_replacement; // per result piece, its index in replacements
    for (std::size_t i = 0, end = replacements.size(); i < end; ++i) {
        const Kit_replacement& replacement = replacements[i];
        if (!replacement.node) {
            return fail(i, "no node");
        }
        const erhe::scene::Node& node = *replacement.node;
        if (node.get_item_host() != &scene_root) {
            return fail(i, fmt::format("'{}' is not a node of scene '{}'", node.get_name(), scene_root.get_name()));
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (replacements[j].node == replacement.node) {
                return fail(i, fmt::format("'{}' is listed twice", node.get_name()));
            }
        }
        std::optional<Kit_replace_skipped> skipped = find_replace_skip(replacement.node);
        if (skipped.has_value()) {
            result.skipped.push_back(std::move(skipped.value()));
            continue;
        }
        const std::shared_ptr<erhe::scene::Mesh> mesh   = std::dynamic_pointer_cast<erhe::scene::Mesh>(replacement.node);
        const std::optional<Kit_piece_data>      record = read_kit_piece(*mesh);
        Kit_parameter_values old_values;
        const std::string record_problem = read_piece_values(record.value(), old_values);
        ERHE_VERIFY(record_problem.empty()); // find_replace_skip() skips a record that does not read
        const Kit_shape_definition* const old_shape = old_values.get_shape();
        Kit_replaced_piece piece;
        piece.mesh      = mesh;
        piece.old_shape = record.value().shape;
        if (old_shape == &shape) {
            piece.values = std::move(old_values);
            result.pieces.push_back(std::move(piece));
            piece_replacement.push_back(i);
            continue;
        }
        carry_parameter_values(old_values, shape, lengths, piece.values, piece.parameters);
        if (replacement.values.has_value()) {
            if (replacement.values.value().get_shape() != &shape) {
                return fail(i, fmt::format("the parameter values are not of shape {}", shape.id));
            }
            piece.values = replacement.values.value();
        }
        const std::string message = piece.values.validate();
        if (!message.empty()) {
            return fail(i, fmt::format("piece '{}': {}", node.get_name(), message));
        }
        const std::string material_problem = check_slot_materials(m_library, shape, replacement.materials);
        if (!material_problem.empty()) {
            return fail(i, material_problem);
        }
        match_material_slots(old_shape->material_slots, shape.material_slots, piece.slot_matches);
        piece.replaced = true;
        result.pieces.push_back(std::move(piece));
        piece_replacement.push_back(i);
    }

    // The brushes last: a call refused by a check above builds none.
    std::vector<std::shared_ptr<Brush>> brushes;
    for (std::size_t i = 0, end = result.pieces.size(); i < end; ++i) {
        if (!result.pieces[i].replaced) {
            brushes.push_back({});
            continue;
        }
        std::shared_ptr<Brush>     brush  = get_brush(result.pieces[i].values);
        const Brush::Scaled* const scaled = brush ? brush->get_scaled(1.0) : nullptr;
        if ((scaled == nullptr) || (scaled->part_primitives.size() != shape.material_slots.size())) {
            return fail(piece_replacement[i], fmt::format("{}: the shape made no geometry of one part per material slot", shape.id));
        }
        brushes.push_back(std::move(brush));
    }

    // The replaced pieces are not surfaces for each other (their old geometry is still in the raytrace scene).
    const bool snap = any_surface_option(surface);
    std::vector<const erhe::scene::Mesh*> excluded;
    if (snap) {
        for (const Kit_replaced_piece& piece : result.pieces) {
            if (piece.replaced) {
                excluded.push_back(piece.mesh.get());
            }
        }
        scene_root.get_raytrace_scene().commit();
    }

    Palette_material_resolver               resolver{m_context, library};
    std::vector<std::shared_ptr<Operation>> piece_operations;
    std::vector<Surface_snap_request>       snap_requests;
    std::vector<std::size_t>                snap_pieces; // per snap request, its index in result.pieces
    for (std::size_t i = 0, end = result.pieces.size(); i < end; ++i) {
        Kit_replaced_piece& piece = result.pieces[i];
        if (!piece.replaced) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Mesh>& mesh   = piece.mesh;
        const Brush::Scaled&                      scaled = *brushes[i]->get_scaled(1.0);
        if (is_previewing(*mesh)) {
            cancel_parameter_preview();
        }
        const std::optional<Brush_placement_data> placement = read_brush_placement(*mesh);
        Kit_piece_mesh_state before{
            .shape           = piece.old_shape,
            .primitives      = mesh->get_primitives(),
            .collision_shape = get_node_source_collision_shape(*mesh),
            .brush           = placement.has_value() ? placement.value().brush : std::shared_ptr<Brush>{},
            .parameters      = read_kit_piece(*mesh).value().parameters,
            .motion_mode     = get_piece_motion_mode(*mesh),
            .ground          = get_piece_ground(*mesh)
        };
        const std::vector<Kit_slot_material>& given = replacements[piece_replacement[i]].materials;
        std::vector<erhe::scene::Mesh_primitive> primitives;
        for (std::size_t slot = 0, slot_end = shape.material_slots.size(); slot < slot_end; ++slot) {
            const bool is_given = (slot < given.size()) && (given[slot].material || !given[slot].palette_name.empty());
            std::optional<std::size_t>& match = piece.slot_matches[slot];
            std::shared_ptr<erhe::primitive::Material> material;
            if (is_given) {
                material = resolver.resolve(m_library, shape.material_slots[slot], given[slot]);
                match.reset();
            } else if (match.has_value() && before.primitives[match.value()].material) {
                material = before.primitives[match.value()].material;
            } else {
                material = resolver.resolve(m_library, shape.material_slots[slot], Kit_slot_material{});
                match.reset();
            }
            piece.materials.push_back(material);
            primitives.emplace_back(scaled.part_primitives[slot], material);
        }
        if (snap) {
            snap_requests.push_back(Surface_snap_request{
                .mesh         = mesh,
                .local_bounds = mesh_local_bounds(std::span<const erhe::scene::Mesh_primitive>{primitives})
            });
            snap_pieces.push_back(i);
        }
        // Across a bodiless shape (P5) the piece gets what a fresh placement of the new shape gets; between shapes with
        // bodies it keeps its own body (or the lack of one) and flag.
        const Kit_shape_definition* const old_shape = m_library.find(piece.old_shape);
        const bool was_bodiless = (old_shape != nullptr) && (old_shape->collision == Kit_collision::none);
        const bool is_bodiless  = (shape.collision == Kit_collision::none);
        const bool fresh        = was_bodiless || is_bodiless;
        const erhe::physics::Motion_mode motion_mode = fresh ? get_placed_motion_mode(scaled) : before.motion_mode;
        const Kit_piece_ground navigation = fresh ? get_placed_ground(shape) : before.ground;
        Kit_piece_mesh_state after{
            .shape           = shape.id,
            .primitives      = std::move(primitives),
            .collision_shape = (motion_mode != erhe::physics::Motion_mode::e_none)
                ? scaled.collision_shape
                : (fresh ? std::shared_ptr<erhe::physics::ICollision_shape>{} : before.collision_shape),
            .brush           = brushes[i],
            .parameters      = piece.values.to_canonical_text(),
            .motion_mode     = motion_mode,
            .ground          = navigation
        };
        piece_operations.push_back(std::make_shared<Kit_piece_parameters_operation>(mesh, std::move(before), std::move(after)));
        append_connector_switch_carry(mesh, piece.values, piece_operations);
        m_light_sync.append(m_context, scene_root, mesh, piece.values, piece_operations);
        ++result.replaced_count;
    }
    if (piece_operations.empty()) {
        return result;
    }
    // The search of D7.3 for the new shapes' bounds; a pose that rests already stays bit for bit.
    std::vector<std::shared_ptr<Operation>> snap_operations;
    if (snap) {
        std::vector<Surface_snap_plan> plans;
        plan_surface_snaps(scene_root.get_raytrace_scene(), snap_requests, surface, excluded, Snap_record::when_moved, plans);
        for (std::size_t j = 0, end = plans.size(); j < end; ++j) {
            Kit_replaced_piece& piece = result.pieces[snap_pieces[j]];
            piece.surface = plans[j].placement;
            piece.snapped = plans[j].moved;
            if (plans[j].operation) {
                snap_operations.push_back(plans[j].operation);
                ++result.snapped_count;
            }
        }
    }
    // Materials are inserted first, so an undo takes them out after the pieces stop using them; the snaps follow the shapes.
    Compound_operation::Parameters parameters;
    parameters.operations = resolver.take_operations();
    for (std::shared_ptr<Operation>& operation : piece_operations) {
        parameters.operations.push_back(std::move(operation));
    }
    for (std::shared_ptr<Operation>& operation : snap_operations) {
        parameters.operations.push_back(std::move(operation));
    }
    // K11.2: a connection is kept where the new shape has its connector with types meeting the other end's, else dropped.
    if (has_attachments(scene_root)) {
        append_replace_connection_drops(scene_root, result, parameters.operations);
    }
    // Step 41: the pieces attached to a replaced piece join its new connector of the same name, and a replaced attached
    // piece joins again with its own.
    std::vector<std::shared_ptr<erhe::scene::Node>> replaced_nodes;
    for (const Kit_replaced_piece& piece : result.pieces) {
        if (piece.replaced) {
            replaced_nodes.push_back(piece.mesh);
        }
    }
    std::shared_ptr<Operation> follow = make_attachment_follow_operation(scene_root, {}, replaced_nodes);
    if (follow) {
        parameters.operations.push_back(std::move(follow));
    }
    result.error = execute_entry(
        (parameters.operations.size() == 1)
            ? parameters.operations.front()
            : std::make_shared<Compound_operation>(std::move(parameters))
    );
    return result;
}


namespace {

enum class Kit_length_take : unsigned int {
    taken = 0,
    clamped,
    refused
};

// L23: a supplied length as given; only where the shape cannot take it, clamped to the declared range. A taken
// parameter becomes explicit (C3.5). Refused (io_values unchanged) when even the clamped value is not taken or changes nothing.
[[nodiscard]] auto take_supplied_length(
    const Kit_shape_definition& shape,
    const std::size_t           index,
    const char* const           target,
    const float                 length,
    Kit_parameter_values&       io_values
) -> Kit_length_take
{
    const auto accepts = [&shape](const Kit_parameter_values& values) {
        return values.validate().empty() && (!shape.check_combination || shape.check_combination(values).empty());
    };
    Kit_parameter_values trial = io_values;
    if (!trial.set_float(target, length)) {
        return Kit_length_take::refused;
    }
    Kit_length_take take = Kit_length_take::taken;
    if (!accepts(trial)) {
        const Kit_parameter& parameter = shape.parameters[index];
        const float clamped = std::clamp(length, parameter.float_min, parameter.float_max);
        if (!trial.set_float(target, clamped) || !accepts(trial) || (clamped == io_values.get_float(target))) {
            return Kit_length_take::refused;
        }
        take = Kit_length_take::clamped;
    }
    trial.set_value_source(index, Kit_value_source::explicit_value);
    io_values = std::move(trial);
    return take;
}

} // anonymous namespace

auto Level_kits::make_parameters_operation(const std::shared_ptr<erhe::scene::Node>& node, const Kit_parameter_values& values, std::string& out_error) -> std::shared_ptr<Operation>
{
    std::shared_ptr<erhe::scene::Mesh> mesh;
    Kit_piece_data                     record;
    out_error = check_piece_values(m_library, node, values, mesh, record);
    if (!out_error.empty()) {
        return {};
    }
    const std::shared_ptr<Brush> brush = get_brush(values);
    const Brush::Scaled* const scaled = brush ? brush->get_scaled(1.0) : nullptr;
    if ((scaled == nullptr) || (mesh->get_primitives().size() != scaled->part_primitives.size())) {
        out_error = fmt::format("{}: the shape made no geometry of its slots", record.shape);
        return {};
    }
    const std::optional<Brush_placement_data> placement = read_brush_placement(*mesh);
    Kit_piece_mesh_state before{
        .shape           = record.shape,
        .primitives      = mesh->get_primitives(),
        .collision_shape = get_node_source_collision_shape(*mesh),
        .brush           = placement.has_value() ? placement.value().brush : std::shared_ptr<Brush>{},
        .parameters      = record.parameters,
        .motion_mode     = get_piece_motion_mode(*mesh),
        .ground          = get_piece_ground(*mesh)
    };
    const std::string parameters = values.to_canonical_text();
    if (parameters == before.parameters) {
        return {};
    }
    Kit_piece_mesh_state after{
        .shape           = values.get_shape()->id,
        .primitives      = with_brush_primitives(before.primitives, *scaled),
        .collision_shape = carries_node_physics(*mesh) ? scaled->collision_shape : before.collision_shape,
        .brush           = brush,
        .parameters      = parameters,
        .motion_mode     = before.motion_mode,
        .ground          = before.ground
    };
    return std::make_shared<Kit_piece_parameters_operation>(mesh, std::move(before), std::move(after));
}

void Level_kits::build_connection_flow(
    Scene_root&                                         scene_root,
    std::span<const std::shared_ptr<erhe::scene::Node>> edited,
    std::span<const std::shared_ptr<erhe::scene::Node>> requesting,
    const Kit_snap_pair* const                          snap,
    std::vector<std::shared_ptr<Operation>>&            out_operations,
    std::vector<std::shared_ptr<erhe::scene::Node>>&    out_changed
)
{
    // C3.4 - C3.8, C4 (design K7, value flow): pieces take the supplies of their neighbours by priority, in rounds.
    std::vector<Attached_piece> attached;
    gather_attachments(scene_root, attached);
    class Edge
    {
    public:
        erhe::scene::Node* a{nullptr};
        std::string        a_connector;
        erhe::scene::Node* b{nullptr};
        std::string        b_connector;
        bool               is_new{false};
    };
    std::vector<Edge> edges;
    const Kit_connector_types mask = get_effective_flow_mask();
    for (const Attached_piece& piece : attached) {
        // C3.9: values flow over a connection only when a type both connectors accept is on in the flow mask.
        if ((piece.dormant() == nullptr) && piece.target && piece.own.types.intersects(Kit_connector_types{.bits = piece.target_local.types.bits & mask.bits})) {
            edges.push_back(Edge{piece.node.get(), piece.attachment.connector, piece.target.get(), piece.attachment.target_connector, false});
        }
    }
    // A snapped release takes part only where it records its connection (find_snap_attachment()), which replaces the
    // piece's connection of that connector.
    const std::optional<Kit_attachment> snap_record = ((snap != nullptr) && snap->piece && snap->target) ? find_snap_attachment(*snap) : std::optional<Kit_attachment>{};
    const bool snap_records = snap_record.has_value();
    if (snap_records) {
        std::erase_if(edges, [snap](const Edge& edge) {
            return ((edge.a == snap->piece.get()) && (edge.a_connector == snap->connector)) || ((edge.b == snap->piece.get()) && (edge.b_connector == snap->connector));
        });
        edges.push_back(Edge{snap->piece.get(), snap->connector, snap->target.get(), snap->target_connector, true});
    }
    if (edges.empty()) {
        return;
    }
    // Staged state per node: its values (a piece) and its slot material names.
    class Staged
    {
    public:
        std::shared_ptr<erhe::scene::Node> node;
        bool                               floor{false};
        Kit_parameter_values               values;
        std::vector<std::string>           materials; // piece: per slot; floor: [wall material, resolved]
        Kit_layout_data                    layout;
        std::vector<glm::vec2>             outline;
        bool                               values_changed{false};
        std::vector<bool>                  material_changed;
        bool                               changed_now{false};
    };
    std::deque<Staged> staged; // push_back keeps the references stage() hands out
    const auto stage = [&](erhe::scene::Node* node) -> Staged* {
        for (Staged& entry : staged) {
            if (entry.node.get() == node) {
                return &entry;
            }
        }
        Staged entry;
        entry.node = std::dynamic_pointer_cast<erhe::scene::Node>(node->shared_from_this());
        if (is_floor_layout(*node)) {
            const erhe::scene::Curve* const curve = dynamic_cast<const erhe::scene::Curve*>(node);
            if (!read_kit_layout(*node, entry.layout).empty() ||
                !read_floor_outline(curve->get_table(), entry.outline).empty()) {
                return nullptr;
            }
            entry.floor = true;
            const Kit_palette_material* const wall_default = m_library.find_slot_default(c_floor_wall_material_slot);
            entry.materials.push_back(!entry.layout.pattern.floor_wall_material.empty()
                ? entry.layout.pattern.floor_wall_material
                : ((wall_default != nullptr) ? wall_default->name : std::string{}));
        } else {
            const std::optional<Kit_piece_data> record = read_kit_piece(*node);
            const erhe::scene::Mesh* const mesh = dynamic_cast<const erhe::scene::Mesh*>(node);
            if (!record.has_value() || (mesh == nullptr) || !read_piece_values(record.value(), entry.values).empty()) {
                return nullptr;
            }
            for (const erhe::scene::Mesh_primitive& primitive : mesh->get_primitives()) {
                entry.materials.push_back(primitive.material ? primitive.material->get_name() : std::string{});
            }
        }
        entry.material_changed.resize(entry.materials.size(), false);
        staged.push_back(std::move(entry));
        return &staged.back();
    };
    const auto contains_node = [](std::span<const std::shared_ptr<erhe::scene::Node>> list, const erhe::scene::Node* node) {
        return std::any_of(list.begin(), list.end(), [node](const std::shared_ptr<erhe::scene::Node>& n) { return n.get() == node; });
    };
    // A neighbour's supply over its connector, from its staged state.
    std::vector<std::string_view> views;
    const auto supply_of = [&](Staged& other, const std::string& connector, Kit_supply_list& out_supply) {
        out_supply.clear();
        if (other.floor) {
            get_floor_connector_supply(other.outline, other.layout.pattern.floor, other.materials.front(), connector, out_supply);
            return;
        }
        const Kit_shape_definition* const shape = other.values.get_shape();
        if ((shape == nullptr) || !shape->get_supply) {
            return;
        }
        views.assign(other.materials.begin(), other.materials.end());
        shape->get_supply(Kit_piece_view{.values = &other.values, .slot_materials = views}, connector, out_supply);
    };
    std::vector<erhe::scene::Node*> pending;
    std::vector<erhe::scene::Node*> changed_this; // E and the pieces the flow changed: a tie prefers them (C4.3)
    for (const std::shared_ptr<erhe::scene::Node>& node : edited) {
        changed_this.push_back(node.get());
    }
    const auto neighbours_pending = [&](const erhe::scene::Node* node) {
        for (const Edge& edge : edges) {
            erhe::scene::Node* const other = (edge.a == node) ? edge.b : ((edge.b == node) ? edge.a : nullptr);
            if ((other != nullptr) && (std::find(pending.begin(), pending.end(), other) == pending.end())) {
                pending.push_back(other);
            }
        }
    };
    for (const std::shared_ptr<erhe::scene::Node>& node : edited) {
        neighbours_pending(node.get());
    }
    for (const std::shared_ptr<erhe::scene::Node>& node : requesting) {
        pending.push_back(node.get());
    }
    // C3.4: the end that records the snapped connection takes its values (a floor snapped under a wall: the wall, K6.1).
    if (snap_records) {
        pending.push_back((snap_record.value().owner == snap->piece->get_gltf_uid()) ? snap->piece.get() : snap->target.get());
    }
    Kit_supply_list supply;
    std::vector<Kit_connector> own_connectors;
    // One supplied value offered to the piece through one connection (C3.4).
    // Its material views a string of the supplier's staged state, which this mapping's evaluation does not change.
    class Offer
    {
    public:
        Kit_supplied_value  value;
        erhe::scene::Node*  supplier{nullptr};
        const std::string*  supplier_connector{nullptr};
    };
    std::vector<Kit_supply_candidate> candidates;
    std::vector<Offer>                offers;
    Kit_flow_report* const report = get_flow_report();
    const auto skipped = [&](const erhe::scene::Node& piece, const Kit_supplied_value& value, const erhe::scene::Node& supplier, const Kit_flow_skip reason) {
        report->add_not_taken(
            Kit_flow_skipped_value{
                .piece    = make_flow_piece(piece),
                .property = value.property,
                .length   = value.length,
                .material = std::string{value.material},
                .supplier = make_flow_piece(supplier),
                .priority = value.priority,
                .reason   = reason
            }
        );
    };
    // C3.13: every value the piece's connections supply that it does not look at, with the reason.
    const auto report_unreceived = [&](erhe::scene::Node* const node, const Kit_flow_skip reason) {
        for (const Edge& edge : edges) {
            const bool as_a = (edge.a == node);
            if (!as_a && (edge.b != node)) {
                continue;
            }
            Staged* const other = stage(as_a ? edge.b : edge.a);
            if (other == nullptr) {
                continue;
            }
            supply_of(*other, as_a ? edge.b_connector : edge.a_connector, supply);
            for (const Kit_supplied_value& value : supply.get()) {
                skipped(*node, value, *other->node, reason);
            }
        }
    };
    std::size_t round = 0;
    const std::size_t limit = get_connection_step_limit();
    while (!pending.empty() && (round < limit)) {
        ++round;
        std::vector<erhe::scene::Node*> current;
        current.swap(pending);
        for (erhe::scene::Node* const node : current) {
            // C3.6: the author's edit is kept; C3.11: a locked layout's child takes nothing.
            const bool author_edit = contains_node(edited, node);
            if (author_edit || find_locking_kit_layout(*node)) {
                if (report != nullptr) {
                    report_unreceived(node, author_edit ? Kit_flow_skip::author_edit : Kit_flow_skip::locked_layout_child);
                }
                continue;
            }
            Staged* const self = stage(node);
            if (self == nullptr) {
                continue;
            }
            if (self->floor) {
                if (report != nullptr) {
                    report_unreceived(node, Kit_flow_skip::floor); // a floor takes no values (design K3.0)
                }
                continue;
            }
            const Kit_shape_definition* const shape = self->values.get_shape();
            if (shape == nullptr) {
                continue;
            }
            if (report != nullptr) {
                // The supplied properties the shape maps nothing to.
                for (const Edge& edge : edges) {
                    const bool as_a = (edge.a == node);
                    Staged* const other = (as_a || (edge.b == node)) ? stage(as_a ? edge.b : edge.a) : nullptr;
                    if (other == nullptr) {
                        continue;
                    }
                    supply_of(*other, as_a ? edge.b_connector : edge.a_connector, supply);
                    for (const Kit_supplied_value& value : supply.get()) {
                        const bool mapped = std::any_of(
                            shape->property_mappings.begin(), shape->property_mappings.end(),
                            [&value](const Kit_property_mapping& mapping) { return mapping.property == value.property; }
                        );
                        if (!mapped) {
                            skipped(*node, value, *other->node, Kit_flow_skip::no_mapping);
                        }
                    }
                }
            }
            if (shape->property_mappings.empty()) {
                continue;
            }
            get_shape_connectors(self->values, own_connectors);
            bool changed = false;
            for (const Kit_property_mapping& mapping : shape->property_mappings) {
                candidates.clear();
                offers.clear();
                for (const Edge& edge : edges) {
                    const bool as_a = (edge.a == node);
                    if (!as_a && (edge.b != node)) {
                        continue;
                    }
                    erhe::scene::Node* const other_node = as_a ? edge.b : edge.a;
                    Staged* const other = stage(other_node);
                    if (other == nullptr) {
                        continue;
                    }
                    const std::string& other_connector = as_a ? edge.b_connector : edge.a_connector;
                    supply_of(*other, other_connector, supply);
                    const Kit_supplied_value* const value = supply.find(mapping.property);
                    if (value == nullptr) {
                        continue;
                    }
                    const std::string& own_name = as_a ? edge.a_connector : edge.b_connector;
                    const Kit_connector* const own = find_connector(own_connectors, own_name);
                    candidates.push_back(
                        Kit_supply_candidate{
                            .priority         = value->priority,
                            .supplier_changed = std::find(changed_this.begin(), changed_this.end(), other_node) != changed_this.end(),
                            .new_connection   = edge.is_new,
                            .connector_index  = (own != nullptr) ? static_cast<std::size_t>(own - own_connectors.data()) : own_connectors.size()
                        }
                    );
                    offers.push_back(Offer{.value = *value, .supplier = other_node, .supplier_connector = &other_connector});
                }
                if (candidates.empty()) {
                    continue;
                }
                const Kit_supply_choice choice = choose_supply(candidates);
                Offer& best = offers[choice.index];
                if (report != nullptr) {
                    for (std::size_t i = 0, end = offers.size(); i < end; ++i) {
                        if (i != choice.index) {
                            skipped(*node, offers[i].value, *offers[i].supplier, get_supply_loss(candidates[i], candidates[choice.index]));
                        }
                    }
                }
                bool took = false;
                if (get_kind(mapping.property) == Kit_property_kind::length) {
                    const std::optional<std::size_t> index = shape->find_parameter(mapping.target);
                    if (index.has_value() && (self->values.get_float(mapping.target) != best.value.length)) {
                        const Kit_length_take take = take_supplied_length(*shape, index.value(), mapping.target, best.value.length, self->values);
                        if (take == Kit_length_take::refused) {
                            log_scene->log(flow_log_level(spdlog::level::warn), "Kit piece '{}' does not take {} = {} m", node->get_name(), c_str(mapping.property), best.value.length);
                            if (report != nullptr) {
                                skipped(*node, best.value, *best.supplier, Kit_flow_skip::shape_refuses);
                            }
                            continue;
                        }
                        if (take == Kit_length_take::clamped) {
                            log_scene->log(
                                flow_log_level(spdlog::level::info), "Kit piece '{}' takes {} = {} m clamped to {} m",
                                node->get_name(), c_str(mapping.property), best.value.length, self->values.get_float(mapping.target)
                            );
                        }
                        best.value.length = self->values.get_float(mapping.target);
                        self->values_changed = true;
                        took = true;
                    }
                } else {
                    for (std::size_t slot = 0; slot < shape->material_slots.size(); ++slot) {
                        if ((shape->material_slots[slot] == mapping.target) && (slot < self->materials.size()) &&
                            !best.value.material.empty() && (self->materials[slot] != best.value.material)) {
                            self->materials[slot] = std::string{best.value.material};
                            self->material_changed[slot] = true;
                            took = true;
                        }
                    }
                }
                if (took) {
                    changed = true;
                    if (report != nullptr) {
                        report->add_taken(
                            Kit_flow_value{
                                .piece              = make_flow_piece(*node),
                                .property           = mapping.property,
                                .length             = best.value.length,
                                .material           = std::string{best.value.material},
                                .supplier           = make_flow_piece(*best.supplier),
                                .supplier_connector = *best.supplier_connector,
                                .priority           = best.value.priority,
                                .tie                = choice.tie
                            }
                        );
                    }
                }
            }
            if (changed) {
                if (std::find(changed_this.begin(), changed_this.end(), node) == changed_this.end()) {
                    changed_this.push_back(node);
                }
                neighbours_pending(node);
            }
        }
    }
    if (report != nullptr) {
        report->rounds = std::max(report->rounds, round);
    }
    if (!pending.empty()) {
        log_scene->log(flow_log_level(spdlog::level::warn), "Kit connection flow stopped at the step limit ({} rounds) with {} pieces still changing", limit, pending.size());
        if (report != nullptr) {
            report->limit_reached = true;
            for (const erhe::scene::Node* const node : pending) {
                if (contains_node(edited, node)) {
                    continue; // the author's edit takes nothing: it is not still changing
                }
                const Kit_flow_piece piece = make_flow_piece(*node);
                if (std::find(report->pending.begin(), report->pending.end(), piece) == report->pending.end()) {
                    report->pending.push_back(piece);
                }
            }
        }
    }
    // K7.4: the staged values and materials as operations of the entry.
    const std::shared_ptr<Content_library> library = scene_root.get_content_library();
    for (Staged& entry : staged) {
        if (entry.floor) {
            continue;
        }
        bool touched = false;
        if (entry.values_changed) {
            std::string error;
            std::shared_ptr<Operation> operation = make_parameters_operation(entry.node, entry.values, error);
            if (!error.empty()) {
                log_scene->log(flow_log_level(spdlog::level::warn), "Kit piece '{}' takes no flowed values: {}", entry.node->get_name(), error);
            } else if (operation) {
                out_operations.push_back(std::move(operation));
                touched = true;
            }
        }
        const std::shared_ptr<erhe::scene::Mesh> mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(entry.node);
        const Kit_shape_definition* const shape = entry.values.get_shape();
        for (std::size_t slot = 0; (slot < entry.material_changed.size()) && library && mesh && (shape != nullptr); ++slot) {
            if (!entry.material_changed[slot] || (slot >= shape->material_slots.size())) {
                continue;
            }
            Kit_slot_material given;
            given.material = find_material_by_name(library, entry.materials[slot]);
            if (!given.material) {
                given.palette_name = entry.materials[slot];
            }
            Palette_material_resolver resolver{m_context, library};
            const std::shared_ptr<erhe::primitive::Material> material = resolver.resolve(m_library, shape->material_slots[slot], given);
            if (!material) {
                continue;
            }
            for (std::shared_ptr<Operation>& operation : resolver.take_operations()) {
                out_operations.push_back(std::move(operation));
            }
            std::shared_ptr<Operation> assign = make_mesh_material_assign_operation(mesh, slot, material);
            if (assign) {
                out_operations.push_back(std::move(assign));
                touched = true;
            }
        }
        if (touched) {
            log_scene->log(flow_log_level(spdlog::level::info), "Kit piece '{}' takes values over its connections", entry.node->get_name());
            out_changed.push_back(entry.node);
        }
    }
}


auto Level_kits::find_scene_material(const Scene_root& scene_root, const std::string_view name) -> std::shared_ptr<erhe::primitive::Material>
{
    const std::shared_ptr<Content_library> library = scene_root.get_content_library();
    return library ? find_material_by_name(library, name) : std::shared_ptr<erhe::primitive::Material>{};
}

auto Level_kits::get_join_supplied_values(
    const erhe::scene::Node&  target,
    const std::string_view    target_connector,
    const std::string_view    connector,
    Kit_parameter_values&     io_values,
    std::vector<std::string>& io_slot_materials
) -> bool
{
    // K9.2: what the placement's flow gives the new piece over its one new connection (C3.4, C3.5); C3.11: a locked
    // layout's child is joined without a connection, so nothing flows.
    const Kit_shape_definition* const shape = io_values.get_shape();
    if ((shape == nullptr) || shape->property_mappings.empty() || find_locking_kit_layout(target)) {
        return false;
    }
    std::vector<Kit_connector> target_frames;
    if (!get_local_connectors(target, target_frames, Kit_connector_set::all).empty()) {
        return false;
    }
    std::vector<Kit_connector> own_frames;
    get_shape_connectors(io_values, own_frames);
    const Kit_connector* const target_frame = find_connector(target_frames, target_connector);
    const Kit_connector* const own_frame    = find_connector(own_frames, connector);
    // C3.9: values flow only over a type both connectors accept that the flow mask lets through.
    if ((target_frame == nullptr) || (own_frame == nullptr) ||
        !own_frame->types.intersects(Kit_connector_types{.bits = target_frame->types.bits & get_effective_flow_mask().bits})) {
        return false;
    }
    Kit_supply_list               supply;
    std::vector<std::string>      target_materials;
    std::vector<std::string_view> views;
    Kit_parameter_values          target_values;
    if (is_floor_layout(target)) {
        const erhe::scene::Curve* const curve = dynamic_cast<const erhe::scene::Curve*>(&target);
        Kit_layout_data        layout;
        std::vector<glm::vec2> outline;
        if ((curve == nullptr) || !read_kit_layout(target, layout).empty() ||
            !read_floor_outline(curve->get_table(), outline).empty()) {
            return false;
        }
        const Kit_palette_material* const wall_default = m_library.find_slot_default(c_floor_wall_material_slot);
        const std::string wall_material = !layout.pattern.floor_wall_material.empty()
            ? layout.pattern.floor_wall_material
            : ((wall_default != nullptr) ? wall_default->name : std::string{});
        get_floor_connector_supply(outline, layout.pattern.floor, wall_material, target_connector, supply);
        return apply_join_supply(*shape, supply, io_values, io_slot_materials);
    }
    const std::optional<Kit_piece_data> record = read_kit_piece(target);
    const erhe::scene::Mesh* const      mesh   = dynamic_cast<const erhe::scene::Mesh*>(&target);
    if (!record.has_value() || (mesh == nullptr) || !read_piece_values(record.value(), target_values).empty()) {
        return false;
    }
    const Kit_shape_definition* const target_shape = target_values.get_shape();
    if ((target_shape == nullptr) || !target_shape->get_supply) {
        return false;
    }
    for (const erhe::scene::Mesh_primitive& primitive : mesh->get_primitives()) {
        target_materials.push_back(primitive.material ? primitive.material->get_name() : std::string{});
    }
    views.assign(target_materials.begin(), target_materials.end());
    target_shape->get_supply(Kit_piece_view{.values = &target_values, .slot_materials = views}, target_connector, supply);
    return apply_join_supply(*shape, supply, io_values, io_slot_materials);
}

auto Level_kits::apply_join_supply(
    const Kit_shape_definition& shape,
    const Kit_supply_list&      supply,
    Kit_parameter_values&       io_values,
    std::vector<std::string>&   io_slot_materials
) -> bool
{
    bool changed = false;
    for (const Kit_property_mapping& mapping : shape.property_mappings) {
        const Kit_supplied_value* const value = supply.find(mapping.property);
        if (value == nullptr) {
            continue;
        }
        if (get_kind(mapping.property) == Kit_property_kind::length) {
            const std::optional<std::size_t> index = shape.find_parameter(mapping.target);
            if (index.has_value() && (io_values.get_float(mapping.target) != value->length) &&
                (take_supplied_length(shape, index.value(), mapping.target, value->length, io_values) != Kit_length_take::refused)) {
                changed = true;
            }
            continue;
        }
        for (std::size_t slot = 0; (slot < shape.material_slots.size()) && (slot < io_slot_materials.size()); ++slot) {
            if ((shape.material_slots[slot] == mapping.target) && !value->material.empty() && (io_slot_materials[slot] != value->material)) {
                io_slot_materials[slot] = std::string{value->material};
                changed = true;
            }
        }
    }
    return changed;
}

auto Level_kits::is_moved(const erhe::scene::Node* const node, const std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes) -> bool
{
    for (const erhe::scene::Node* i = node; i != nullptr; i = i->get_parent_node().get()) {
        for (const std::shared_ptr<erhe::scene::Node>& moved : moved_nodes) {
            if (moved.get() == i) {
                return true;
            }
        }
    }
    return false;
}

auto Level_kits::build_floor_fits(
    Scene_root&                                         scene_root,
    std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes,
    std::vector<std::shared_ptr<Operation>>&            out_operations
) -> std::string
{
    // C3.15, K5.3, L20: each moved wall (at or below a moved node) standing on a floor's edge gives that edge the line of its
    // base, half a wall thickness toward the floor's outside. Per floor, a corner between two changed edges goes to their
    // lines' crossing; a corner between a changed and an unchanged edge slides along the unchanged edge's line.
    const auto cross2 = [](const glm::vec2& a, const glm::vec2& b) { return (a.x * b.y) - (a.y * b.x); };
    if (!get_effective_flow_mask().contains(Kit_connector_type::wall_floor) || moved_nodes.empty()) {
        return {}; // C3.9: nothing flows over wall-floor joins
    }
    class Edge_line
    {
    public:
        std::size_t                        edge{0};
        glm::vec2                          point{0.0f};
        glm::vec2                          run{0.0f};
        std::shared_ptr<erhe::scene::Node> wall;
    };
    class Floor_fit
    {
    public:
        std::shared_ptr<erhe::scene::Curve> curve;
        Kit_layout_data                     data;
        std::vector<glm::vec2>              outline;
        std::vector<Edge_line>              lines;
    };
    std::vector<Floor_fit> fits;
    std::vector<std::shared_ptr<erhe::scene::Node>> walls;
    scene_root.get_scene().for_each_node([&](const std::shared_ptr<erhe::scene::Node>& node) -> bool {
        if (node->has_local_value(Kit_piece::attachment_property.get()) && carries_kit_piece(*node) && is_moved(node.get(), moved_nodes)) {
            walls.push_back(node);
        }
        return true;
    });
    std::vector<Kit_connector> frames;
    for (const std::shared_ptr<erhe::scene::Node>& wall : walls) {
        for (const Kit_attachment& attachment : read_kit_attachments(*wall)) {
            std::shared_ptr<erhe::scene::Node> floor_node;
            scene_root.get_scene().for_each_node([&](const std::shared_ptr<erhe::scene::Node>& n) -> bool {
                if (n->get_gltf_uid() == attachment.target) { floor_node = n; return false; }
                return true;
            });
            const std::shared_ptr<erhe::scene::Curve> curve = std::dynamic_pointer_cast<erhe::scene::Curve>(floor_node);
            // A floor moved with the wall (both at or below a moved node) keeps its outline.
            if (!curve || !is_floor_edge_join(*curve, attachment.target_connector) || is_moved(curve.get(), moved_nodes)) {
                continue;
            }
            std::vector<Floor_fit>::iterator fit = std::find_if(fits.begin(), fits.end(), [&curve](const Floor_fit& f) { return f.curve == curve; });
            if (fit == fits.end()) {
                Floor_fit made{.curve = curve};
                if (!read_kit_layout(*curve, made.data).empty() || !read_floor_outline(curve->get_table(), made.outline).empty()) {
                    continue;
                }
                fits.push_back(std::move(made));
                fit = fits.end() - 1;
            }
            const std::size_t n = fit->outline.size();
            std::size_t edge = n;
            for (std::size_t k = 0; k < n; ++k) {
                if (attachment.target_connector == get_floor_edge_connector_name(k)) {
                    edge = k;
                }
            }
            if ((edge == n) || !get_world_connectors(*wall, frames, Kit_connector_set::all).empty()) {
                continue;
            }
            const Kit_connector* const base = find_connector(frames, attachment.connector);
            if (base == nullptr) {
                continue;
            }
            const glm::mat4 world_from_wall = wall->world_from_node();
            // The wall's base point and run (x) in the floor's plan; vertical moves and tilts drop out.
            const glm::mat4 floor_from_world = curve->node_from_world();
            const glm::vec3 p3   = glm::vec3{floor_from_world * glm::vec4{base->position, 1.0f}};
            const glm::vec3 run3 = glm::mat3{floor_from_world} * glm::vec3{world_from_wall * glm::vec4{1.0f, 0.0f, 0.0f, 0.0f}};
            const glm::vec2 run{run3.x, run3.z};
            if (glm::length(run) < 1.0e-6f) {
                continue;
            }
            fit->lines.push_back(Edge_line{.edge = edge, .point = glm::vec2{p3.x, p3.z}, .run = glm::normalize(run), .wall = wall});
        }
    }
    const std::size_t operations_begin = out_operations.size();
    for (Floor_fit& fit : fits) {
        const std::vector<glm::vec2>& outline = fit.outline;
        const std::size_t n = outline.size();
        if (fit.lines.empty() || (n < 3)) {
            continue;
        }
        // The outward side of each edge is the floor's (K5.3): the wall's own facing does not say it (a wall turned half a
        // turn or joined with a twist faces the other way).
        const auto outward_of = [&outline](const std::size_t k) { return -get_floor_edge_inward(outline, k); };
        // The line of each changed edge: a point on it and its direction.
        std::vector<std::optional<std::pair<glm::vec2, glm::vec2>>> changed(n);
        std::vector<const erhe::scene::Node*> changed_by(n, nullptr);
        for (const Edge_line& line : fit.lines) {
            changed_by[line.edge] = line.wall.get();
            glm::vec2 normal{-line.run.y, line.run.x};
            if (glm::dot(normal, outward_of(line.edge)) < 0.0f) {
                normal = -normal;
            }
            changed[line.edge] = std::pair<glm::vec2, glm::vec2>{line.point + (0.5f * fit.data.pattern.floor.wall_thickness * normal), line.run};
        }
        // The crossing of a line with another line; nullopt when the sine of their angle is below min_sine.
        const auto cross_lines_within = [&](const glm::vec2& p, const glm::vec2& d, const glm::vec2& q, const glm::vec2& e, const float min_sine) -> std::optional<glm::vec2> {
            const float length = glm::length(e);
            if ((length <= 0.0f) || (std::abs(cross2(d, e / length)) < min_sine)) {
                return std::nullopt;
            }
            return p + ((cross2(q - p, e) / cross2(d, e)) * d);
        };
        // A slide along a neighbour's line: not within c_segment_slide_min_sine of parallel.
        const auto cross_lines = [&](const glm::vec2& p, const glm::vec2& d, const glm::vec2& q, const glm::vec2& e) -> std::optional<glm::vec2> {
            return cross_lines_within(p, d, q, e, c_segment_slide_min_sine);
        };
        const auto distance_to_line = [](const glm::vec2& point, const glm::vec2& p, const glm::vec2& d) {
            return std::abs(glm::dot(point - p, glm::vec2{-d.y, d.x}));
        };
        // A corner moved straight along the normal of a changed edge's line (a neighbour near parallel).
        const auto along_normal = [](const glm::vec2& corner, const glm::vec2& p, const glm::vec2& d) {
            const glm::vec2 normal{-d.y, d.x};
            return corner + (glm::dot(p - corner, normal) * normal);
        };
        // Two changed lines that are one line (in plan, within 1 mm and 0.06 degrees).
        const auto same_line = [&cross2](const std::pair<glm::vec2, glm::vec2>& a, const std::pair<glm::vec2, glm::vec2>& b) {
            const glm::vec2 normal{-a.second.y, a.second.x};
            return (std::abs(cross2(a.second, b.second)) <= 1.0e-3f) && (std::abs(glm::dot(b.first - a.first, normal)) <= 1.0e-3f);
        };
        std::vector<glm::vec2> moved = outline;
        std::string corner_refusal;
        for (std::size_t c = 0; c < n; ++c) {
            const std::size_t before = (c + n - 1) % n; // the edge ending at corner c
            const std::size_t after  = c;               // the edge starting at corner c
            const std::optional<std::pair<glm::vec2, glm::vec2>>& in  = changed[before];
            const std::optional<std::pair<glm::vec2, glm::vec2>>& out = changed[after];
            if (in.has_value() && out.has_value()) {
                // Near parallel, the crossing is taken where it stays about as near the corner as the lines are; farther
                // away (or none: a step between the lines) one corner cannot meet both, and the change is refused.
                const std::optional<glm::vec2> crossing = cross_lines(out->first, out->second, in->first, in->second);
                const std::optional<glm::vec2> near_crossing = cross_lines_within(out->first, out->second, in->first, in->second, 1.0e-4f);
                const float reach = (2.0f * std::max(distance_to_line(outline[c], in->first, in->second), distance_to_line(outline[c], out->first, out->second))) + 1.0e-3f;
                if (crossing.has_value()) {
                    moved[c] = crossing.value();
                } else if (near_crossing.has_value() && (glm::distance(near_crossing.value(), outline[c]) <= reach)) {
                    moved[c] = near_crossing.value();
                } else if (same_line(in.value(), out.value())) {
                    moved[c] = along_normal(outline[c], out->first, out->second);
                } else if (corner_refusal.empty()) {
                    // Nearly in line and apart: one corner meets neither line (a step between them needs a corner the fit
                    // does not make).
                    corner_refusal = fmt::format(
                        "the walls '{}' and '{}' give edges {} and {} lines within 15 degrees of parallel that do not meet at corner {}",
                        changed_by[before]->get_name(), changed_by[after]->get_name(), before, after, c
                    );
                }
            } else if (out.has_value()) {
                const std::optional<glm::vec2> crossing = cross_lines(out->first, out->second, outline[before], outline[c] - outline[before]);
                moved[c] = crossing.value_or(along_normal(outline[c], out->first, out->second));
            } else if (in.has_value()) {
                const std::optional<glm::vec2> crossing = cross_lines(in->first, in->second, outline[c], outline[(c + 1) % n] - outline[c]);
                moved[c] = crossing.value_or(along_normal(outline[c], in->first, in->second));
            }
        }
        const std::shared_ptr<erhe::scene::Curve>& curve = fit.curve;
        if (!corner_refusal.empty()) {
            const std::string refusal = fmt::format("floor '{}' cannot fit the moved walls: {}", curve->get_name(), corner_refusal);
            log_scene->log(flow_log_level(spdlog::level::warn), "{}", refusal);
            out_operations.resize(operations_begin);
            return refusal;
        }
        // K0.3, C8.3: an outline the floor refuses refuses the whole change.
        const std::string problem = check_floor_outline_change(outline, moved);
        if (!problem.empty()) {
            const std::string refusal = fmt::format("floor '{}' cannot fit the moved wall '{}': {}", curve->get_name(), fit.lines.front().wall->get_name(), problem);
            log_scene->log(flow_log_level(spdlog::level::warn), "{}", refusal);
            out_operations.resize(operations_begin);
            return refusal;
        }
        bool any = false;
        for (std::size_t c = 0; c < n; ++c) {
            any = any || (glm::distance(moved[c], outline[c]) >= 1.0e-5f);
        }
        if (!any) {
            continue;
        }
        erhe::math::Curve_path after = curve->get_path();
        for (std::size_t c = 0; c < n; ++c) {
            if (glm::distance(moved[c], outline[c]) >= 1.0e-5f) {
                after.points[c].position = glm::vec3{moved[c].x, 0.0f, moved[c].y};
            }
        }
        out_operations.push_back(std::make_shared<Curve_set_operation>(curve, curve->get_path(), std::move(after), std::string{"floor fit"}));
        Kit_flow_report* const report = get_flow_report();
        for (const Edge_line& line : fit.lines) {
            log_scene->log(flow_log_level(spdlog::level::info), "Floor '{}' fits the moved wall '{}' (edge {})", curve->get_name(), line.wall->get_name(), line.edge);
            if (report != nullptr) {
                report->add_response(Kit_flow_response{.piece = make_flow_piece(*curve), .kind = Kit_flow_response_kind::floor_fit, .from = make_flow_piece(*line.wall)});
            }
        }
    }
    return {};
}



auto Level_kits::is_floor_edge_join(const erhe::scene::Node& target, const std::string_view target_connector) -> bool
{
    return is_floor_layout(target) && target_connector.starts_with("edge_");
}

auto Level_kits::get_wall_fit_wall(
    const Attached_piece&                                 entry,
    const std::function<bool(const erhe::scene::Node*)>& is_moved,
    const std::span<const Attached_piece>                 attached
) -> const std::shared_ptr<erhe::scene::Node>*
{
    if (!entry.target || (entry.dormant() != nullptr)) {
        return nullptr;
    }
    const bool node_moved = is_moved(entry.node.get());
    if (node_moved == is_moved(entry.target.get())) {
        return nullptr;
    }
    // Only joins of two wall connectors fit; the fitted wall is the end that did not move.
    if (!entry.own.types.contains(Kit_connector_type::wall) || !entry.target_local.types.contains(Kit_connector_type::wall)) {
        return nullptr;
    }
    const std::shared_ptr<erhe::scene::Node>& wall      = node_moved ? entry.target : entry.node;
    const std::string&                        near_name = node_moved ? entry.attachment.target_connector : entry.attachment.connector;
    if ((near_name != "start") && (near_name != "end")) {
        return nullptr;
    }
    // A wall standing on a floor is the floor's (C4).
    const bool stands_on_floor = std::any_of(attached.begin(), attached.end(), [&wall](const Attached_piece& p) {
        return ((p.node == wall) && p.target && is_floor_layout(*p.target)) || ((p.target == wall) && is_floor_layout(*p.node));
    });
    return stands_on_floor ? nullptr : &wall;
}

void Level_kits::build_wall_fits(
    Scene_root&                                         scene_root,
    std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes,
    std::vector<std::shared_ptr<Operation>>&            out_operations
)
{
    // C3.16, L22: a wall joined end to end to a moved wall keeps its far end where it is; its near end goes onto the moved
    // wall's connector, so its length and its turn about the up axis change (upright; a tilt is dropped). A wall whose
    // partners at both ends moved spans the two. A wall standing on a floor is the floor's (C4).
    m_fitted_pairs.clear();
    m_fitted_walls.clear();
    if (!get_effective_flow_mask().contains(Kit_connector_type::wall)) {
        return; // C3.9: nothing flows over wall joins
    }
    std::vector<Attached_piece> attached;
    gather_attachments(scene_root, attached);
    // Moved: a moved node or below one (a wall in a moved group moved with it), the rule of the floor fits too.
    const auto is_moved = [&moved_nodes](const erhe::scene::Node* node) { return Level_kits::is_moved(node, moved_nodes); };
    class Fit
    {
    public:
        std::shared_ptr<erhe::scene::Node>                              wall;
        std::optional<glm::vec3>                                        start; // where its start connector goes
        std::optional<glm::vec3>                                        end;
        std::vector<std::pair<const erhe::scene::Node*, const erhe::scene::Node*>> pairs;
        std::vector<const erhe::scene::Node*>                           movers;
    };
    std::vector<Fit> fits;
    std::vector<Kit_connector> moved_frames;
    for (const Attached_piece& entry : attached) {
        const std::shared_ptr<erhe::scene::Node>* const fitted = get_wall_fit_wall(entry, is_moved, attached);
        if (fitted == nullptr) {
            continue;
        }
        const bool node_moved = is_moved(entry.node.get());
        const std::shared_ptr<erhe::scene::Node>& wall       = *fitted;
        const std::shared_ptr<erhe::scene::Node>& mover      = node_moved ? entry.node : entry.target;
        const std::string&                        near_name  = node_moved ? entry.attachment.target_connector : entry.attachment.connector;
        const std::string&                        mover_name = node_moved ? entry.attachment.connector : entry.attachment.target_connector;
        if (!get_world_connectors(*mover, moved_frames, Kit_connector_set::all).empty()) {
            continue;
        }
        const Kit_connector* const target = find_connector(moved_frames, mover_name);
        if (target == nullptr) {
            continue;
        }
        std::vector<Fit>::iterator fit = std::find_if(fits.begin(), fits.end(), [&wall](const Fit& f) { return f.wall == wall; });
        if (fit == fits.end()) {
            fits.push_back(Fit{.wall = wall});
            fit = fits.end() - 1;
        }
        (near_name == "start" ? fit->start : fit->end) = target->position;
        fit->pairs.emplace_back(entry.node.get(), entry.target.get());
        fit->movers.push_back(mover.get());
    }
    std::vector<Kit_connector> wall_frames;
    for (const Fit& fit : fits) {
        const std::shared_ptr<erhe::scene::Node>& wall = fit.wall;
        const glm::vec3 scale = wall->parent_from_node_transform().get_scale();
        if (glm::any(glm::greaterThan(glm::abs(scale - glm::vec3{1.0f}), glm::vec3{1.0e-4f}))) {
            log_scene->log(flow_log_level(spdlog::level::info), "Wall '{}' is scaled ({}, {}, {}): it does not fit its moved neighbour", wall->get_name(), scale.x, scale.y, scale.z);
            continue;
        }
        const std::optional<Kit_piece_data> record = read_kit_piece(*wall);
        Kit_parameter_values values;
        if (!record.has_value() || !read_piece_values(record.value(), values).empty() || !get_world_connectors(*wall, wall_frames, Kit_connector_set::all).empty()) {
            continue;
        }
        const Kit_shape_definition* const shape = values.get_shape();
        const char* length_parameter = nullptr;
        for (const Kit_property_mapping& mapping : shape->property_mappings) {
            if (mapping.property == Kit_standard_property::child_length) {
                length_parameter = mapping.target;
            }
        }
        const Kit_connector* const old_start = find_connector(wall_frames, "start");
        const Kit_connector* const old_end   = find_connector(wall_frames, "end");
        if ((length_parameter == nullptr) || (old_start == nullptr) || (old_end == nullptr)) {
            continue;
        }
        // From start to end in plan: a moved partner's connector, else the wall's own far end where it is.
        const glm::vec3 start = fit.start.value_or(old_start->position);
        const glm::vec3 end   = fit.end.value_or(old_end->position);
        const float     y     = fit.start.has_value() && fit.end.has_value() ? start.y : (fit.start.has_value() ? end.y : start.y);
        glm::vec2 run{end.x - start.x, end.z - start.z};
        const float length = glm::length(run);
        if (!(length > 1.0e-4f)) {
            continue;
        }
        run /= length;
        // The partners' connectors are where this wall's ends are (a value edit that moved no end): nothing to fit.
        if ((glm::distance(start, old_start->position) < 1.0e-5f) && (glm::distance(end, old_end->position) < 1.0e-5f)) {
            continue;
        }
        if (std::abs(values.get_float(length_parameter) - length) > 1.0e-5f) {
            static_cast<void>(values.set_float(length_parameter, length));
            values.set_value_source(shape->find_parameter(length_parameter).value(), Kit_value_source::explicit_value);
            std::string error;
            std::shared_ptr<Operation> parameters = make_parameters_operation(wall, values, error);
            if (!error.empty()) {
                log_scene->log(flow_log_level(spdlog::level::warn), "Wall '{}' does not fit its moved neighbour: {}", wall->get_name(), error);
                continue;
            }
            if (parameters) {
                out_operations.push_back(std::move(parameters));
            }
        }
        const float     yaw      = std::atan2(-run.y, run.x);
        const glm::quat rotation = glm::angleAxis(yaw, glm::vec3{0.0f, 1.0f, 0.0f});
        const glm::vec3 center{0.5f * (start.x + end.x), y, 0.5f * (start.z + end.z)};
        const erhe::scene::Trs_transform before = wall->parent_from_node_transform();
        const std::shared_ptr<erhe::scene::Node> parent = wall->get_parent_node();
        const glm::mat4 world_after = glm::translate(glm::mat4{1.0f}, center) * glm::mat4_cast(rotation);
        const glm::mat4 parent_from_node = parent ? (parent->node_from_world() * world_after) : world_after;
        out_operations.push_back(
            std::make_shared<Node_transform_operation>(
                Node_transform_operation::Parameters{
                    .node                    = wall,
                    .parent_from_node_before = before,
                    .parent_from_node_after  = make_joined_local_transform(parent_from_node, before.get_scale()),
                    .xform_op_stack_before   = wall->copy_xform_op_stack()
                }
            )
        );
        m_fitted_pairs.insert(m_fitted_pairs.end(), fit.pairs.begin(), fit.pairs.end());
        m_fitted_walls.push_back(wall);
        log_scene->log(flow_log_level(spdlog::level::info), "Wall '{}' fits its moved neighbour (length {} m)", wall->get_name(), length);
        Kit_flow_report* const report = get_flow_report();
        for (const erhe::scene::Node* const mover : fit.movers) {
            if (report != nullptr) {
                report->add_response(Kit_flow_response{.piece = make_flow_piece(*wall), .kind = Kit_flow_response_kind::wall_fit, .from = make_flow_piece(*mover)});
            }
        }
    }
}


void Level_kits::build_auto_connections(
    Scene_root&                                         scene_root,
    std::span<const std::shared_ptr<erhe::scene::Node>> pieces,
    std::span<const std::shared_ptr<erhe::scene::Node>> excluded,
    std::vector<std::shared_ptr<Operation>>&            out_operations,
    std::vector<std::shared_ptr<erhe::scene::Node>>&    out_owners
)
{
    // C5, design K8: a free enabled connector of a changed piece on a free compatible connector of another piece is
    // connected: co-located within 0.1 mm and, but for two wall connectors (a room corner, where they meet at an angle),
    // facing it (outward axes opposed); up axes are not compared (L7). The record goes on the changed piece.
    if (!get_effective_auto_connect()) {
        return;
    }
    class Frame
    {
    public:
        std::shared_ptr<erhe::scene::Node> node;
        Kit_connector                      world;
        Kit_connector                      local;
    };
    std::vector<Frame> frames;
    std::vector<Kit_connector> local;
    std::vector<Kit_connector> world;
    scene_root.get_scene().for_each_node([&](const std::shared_ptr<erhe::scene::Node>& node) -> bool {
        if (!(carries_kit_piece(*node) || is_floor_layout(*node)) || !get_local_connectors(*node, local, Kit_connector_set::enabled).empty() ||
            !transform_connectors(local, node->world_from_node(), world)) {
            return true;
        }
        for (std::size_t i = 0; i < local.size(); ++i) {
            frames.push_back(Frame{node, world[i], local[i]});
        }
        return true;
    });
    std::vector<std::pair<const erhe::scene::Node*, std::string>> taken; // connectors connected in this pass
    const auto is_free = [&](const std::shared_ptr<erhe::scene::Node>& node, const char* name) {
        for (const std::pair<const erhe::scene::Node*, std::string>& t : taken) {
            if ((t.first == node.get()) && (t.second == name)) {
                return false;
            }
        }
        return !find_connection_at(scene_root, *node, name);
    };
    // The record lists written, one per node: the changed piece's, or a wall's for a join a floor made (K6.1).
    std::vector<std::pair<std::shared_ptr<erhe::scene::Node>, std::vector<Kit_attachment>>> lists;
    const auto list_of = [&lists](const std::shared_ptr<erhe::scene::Node>& node) -> std::vector<Kit_attachment>& {
        for (std::pair<std::shared_ptr<erhe::scene::Node>, std::vector<Kit_attachment>>& entry : lists) {
            if (entry.first == node) {
                return entry.second;
            }
        }
        lists.emplace_back(node, read_kit_attachments(*node));
        return lists.back().second;
    };
    std::vector<const erhe::scene::Node*> visited;
    for (const std::shared_ptr<erhe::scene::Node>& piece : pieces) {
        if (!piece || !(carries_kit_piece(*piece) || is_floor_layout(*piece)) || find_locking_kit_layout(*piece) ||
            (std::find(visited.begin(), visited.end(), piece.get()) != visited.end()) ||
            (std::find(excluded.begin(), excluded.end(), piece) != excluded.end())) {
            continue; // a piece joined with attach none declined a record in this change
        }
        visited.push_back(piece.get());
        for (const Frame& own : frames) {
            if ((own.node != piece) || !is_free(own.node, own.local.name)) {
                continue;
            }
            for (const Frame& other : frames) {
                if ((other.node == piece) || !is_compatible(own.local, other.local) || find_locking_kit_layout(*other.node)) {
                    continue;
                }
                // Only typed connectors (wall, wall-floor, floor, rail) auto-connect: touching generic pieces (tiles) do not chain.
                const Kit_connector_types shared{.bits = own.local.types.bits & other.local.types.bits & ~Kit_connector_types::of(Kit_connector_type::generic).bits};
                if (shared.is_empty()) {
                    continue;
                }
                // Two wall connectors meet at a corner at any angle; floors meet edge to edge, facing.
                const bool  wall_pair = shared.contains(Kit_connector_type::wall);
                const float facing    = glm::dot(own.world.outward, other.world.outward);
                if ((glm::distance(own.world.position, other.world.position) > 1.0e-4f) ||
                    (!wall_pair && (facing > -0.99999f)) || (wall_pair && (facing > 0.99f)) || !is_free(other.node, other.local.name)) {
                    continue; // a wall pair meets at any angle but not pointing the same way (a duplicate in place)
                }
                if (check_attach(*piece, *other.node).refusal != Kit_attach_refusal::none) {
                    continue;
                }
                // The quarter twist nearest the up axes' turn about the target's outward axis (followed rigidly where rigid).
                const float angle = std::atan2(glm::dot(glm::cross(other.world.up, own.world.up), other.world.outward), glm::dot(other.world.up, own.world.up));
                const int   twist = ((static_cast<int>(std::lround(angle / glm::half_pi<float>())) % 4) + 4) % 4;
                const Kit_attachment made{
                    .owner            = piece->get_gltf_uid(),
                    .target           = other.node->get_gltf_uid(),
                    .connector        = own.local.name,
                    .target_connector = other.local.name,
                    .twist_steps      = twist,
                    .gap              = 0.0f
                };
                // K6.1, K13: a floor's join to a wall is recorded on the wall, a host's join to a rider on the rider.
                const bool on_other = is_host_end_of_join(*piece, own.local, *other.node, other.local);
                const Kit_attachment record = on_other ? make_reversed_attachment(made) : made;
                const std::shared_ptr<erhe::scene::Node>& holder = on_other ? other.node : piece;
                const std::shared_ptr<erhe::scene::Node>& reached = on_other ? piece : other.node;
                std::vector<Kit_attachment>& records = list_of(holder);
                // A record of this connector whose target is gone (is_free() reads it as free) is replaced.
                std::erase_if(records, [&record](const Kit_attachment& r) { return r.connector == record.connector; });
                records.push_back(record);
                taken.emplace_back(piece.get(), own.local.name);
                taken.emplace_back(other.node.get(), other.local.name);
                log_scene->log(flow_log_level(spdlog::level::info), "Kit piece '{}' auto-connects '{}' to '{}' of '{}'", piece->get_name(), own.local.name, other.local.name, other.node->get_name());
                report_new_connection(Kit_new_connection{.node = holder, .attachment = record, .target = reached});
                if (std::find(out_owners.begin(), out_owners.end(), holder) == out_owners.end()) {
                    out_owners.push_back(holder);
                }
                break;
            }
        }
    }
    for (const std::pair<std::shared_ptr<erhe::scene::Node>, std::vector<Kit_attachment>>& entry : lists) {
        std::shared_ptr<Operation> operation = make_attachments_operation(entry.first, entry.second);
        if (operation) {
            out_operations.push_back(std::move(operation));
        }
    }
}

} // namespace editor
