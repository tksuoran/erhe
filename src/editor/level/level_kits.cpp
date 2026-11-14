#include "level/level_kits.hpp"

#include "level/kit_layout.hpp"
#include "level/kit_mesh_builder.hpp"
#include "level/kit_piece.hpp"
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
#include "operations/item_insert_remove_operation.hpp"
#include "operations/item_parent_change_operation.hpp"
#include "operations/library_attach_operation.hpp"
#include "operations/mesh_material_assign_operation.hpp"
#include "operations/mesh_primitive_swap.hpp"
#include "operations/node_transform_operation.hpp"
#include "operations/operation_stack.hpp"
#include "prefabs/instance_structure.hpp"
#include "scene/collision_shape_from_mesh.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_physics_system.hpp"
#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item_host.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include <fmt/format.h>
#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>

namespace editor {

namespace {

// Smooth shapes keep their base rims hard: an edge sharper than this is a crease.
constexpr float c_smooth_crease_angle_degrees = 80.0f;

[[nodiscard]] auto to_normal_style(const Kit_normal_style style) -> erhe::primitive::Normal_style
{
    switch (style) {
        case Kit_normal_style::smooth:
        case Kit_normal_style::smooth_with_hard_edges: return erhe::primitive::Normal_style::corner_normals;
        case Kit_normal_style::faceted:
        default:                                       return erhe::primitive::Normal_style::polygon_normals;
    }
}

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

// The whole mesh of the values, every slot; the brush's geometry, hull and triangle mesh collision shape.
[[nodiscard]] auto make_kit_geometry(const Kit_parameter_values& values) -> std::shared_ptr<erhe::geometry::Geometry>
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

// One geometry per material slot of the shape (D2.1); returns the problem, empty when every slot has facets.
[[nodiscard]] auto make_kit_part_geometries(
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

[[nodiscard]] auto make_palette_material(const Kit_palette_material& palette) -> std::shared_ptr<erhe::primitive::Material>
{
    erhe::primitive::Material_values material_values{};
    material_values.base_color = palette.base_color;
    material_values.roughness  = glm::vec2{palette.roughness, palette.roughness};
    material_values.metallic   = 0.0f;
    return std::make_shared<erhe::primitive::Material>(
        erhe::primitive::Material_create_info{.name = palette.name, .values = material_values}
    );
}

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

// The module snap first, then the surface options (D3.2, D4.4); excludes nothing from the rays.
[[nodiscard]] auto rest_pose(
    Scene_root&             scene_root,
    const erhe::math::Aabb& local_bounds,
    const glm::vec3&        position,
    const glm::quat&        rotation,
    const Kit_snap          snap,
    const float             module,
    const Surface_options&  surface
) -> Surface_placement
{
    const glm::vec3 snapped = (snap == Kit_snap::module) ? Level_kits::snap_to_module(position, module) : position;
    return find_surface_placement(scene_root, local_bounds, Surface_pose{.position = snapped, .rotation = rotation}, surface, {});
}

} // anonymous namespace

Level_kits::Level_kits(App_context& context, App_message_bus& app_message_bus)
    : m_context{context}
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
    settings.rotation_step_degrees = static_cast<float>(config.rotation_step_degrees);
    settings.seed_per_piece        = config.new_seed_per_piece ? Kit_seed_per_piece::next : Kit_seed_per_piece::same;
    return settings;
}

auto Level_kits::get_surface_settings() const -> Surface_options
{
    Surface_options options;
    if (m_context.editor_settings == nullptr) {
        return options;
    }
    const Surface_snap_config& config = m_context.editor_settings->surface_snap;
    options.snap   = config.snap_to_surface   ? Surface_snap::on   : Surface_snap::off;
    options.orient = config.orient_to_surface ? Surface_orient::on : Surface_orient::off;
    return options;
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

auto Level_kits::make_placement_pose(
    Scene_root&                   scene_root,
    const erhe::math::Aabb&       local_bounds,
    const glm::vec3&              point,
    const int                     yaw_steps,
    const Kit_placement_settings& settings
) const -> Kit_placement_pose
{
    const Kit_lengths lengths  = get_lengths(scene_root);
    const Kit_snap    snap     = (lengths.module > 0.0f) ? settings.snap : Kit_snap::none;
    const float       yaw      = glm::radians(static_cast<float>(yaw_steps) * settings.rotation_step_degrees);
    const glm::quat   rotation = glm::angleAxis(yaw, glm::vec3{0.0f, 1.0f, 0.0f});
    const Surface_placement placement = rest_pose(scene_root, local_bounds, point, rotation, snap, lengths.module, settings.surface);
    return Kit_placement_pose{
        .position      = placement.pose.position,
        .rotation      = placement.pose.rotation,
        .surface_found = placement.surface_found
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
        .normal_style       = to_normal_style(shape.normal_style),
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
    // A convex shape takes the brush's convex hull (Brush::late_initialize); the others their own triangles (D2.3).
    if (shape.collision == Kit_collision::triangle_mesh) {
        brush_data.collision_shape_generator = [values](const float scale) -> std::shared_ptr<erhe::physics::ICollision_shape> {
            const std::shared_ptr<erhe::geometry::Geometry> geometry = make_kit_geometry(values);
            return build_triangle_mesh_shape(geometry->get_mesh(), scale);
        };
    }
    std::shared_ptr<Brush> brush = std::make_shared<Brush>(brush_data);
    if (!brush->get_geometry()) {
        log_scene->warn("Kit shape '{}' made no geometry for {}", shape.id, values.to_canonical_text());
        return {};
    }
    return brush;
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
    const Kit_shape_definition* const shape = m_library.find(record.shape);
    if (shape == nullptr) {
        return fmt::format("the kits have no shape '{}'", record.shape);
    }
    out_values = Kit_parameter_values{*shape};
    const std::string parse_error = parse_canonical_text(*shape, record.parameters, out_values);
    if (!parse_error.empty()) {
        return fmt::format("the parameters do not read: {}", parse_error);
    }
    return out_values.validate();
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

auto Level_kits::refresh_scene(Scene_root& scene_root) -> Kit_refresh_report
{
    Kit_refresh_report report;
    std::vector<std::shared_ptr<erhe::scene::Mesh>> pieces;
    scene_root.get_scene().for_each_node(
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
    report.piece_count = pieces.size();
    Kit_parameter_values values;
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : pieces) {
        std::string problem = read_piece_values(read_kit_piece(*mesh).value(), values);
        std::shared_ptr<Brush> brush = problem.empty() ? get_brush(values) : std::shared_ptr<Brush>{};
        const Brush::Scaled* const scaled = brush ? brush->get_scaled(1.0) : nullptr;
        if (problem.empty() && (scaled == nullptr)) {
            problem = "the shape made no geometry";
        }
        if (problem.empty() && (mesh->get_primitives().size() != scaled->part_primitives.size())) {
            problem = part_count_problem(*mesh, *scaled);
        }
        if (!problem.empty()) {
            ++report.problem_count;
            log_scene->warn("Kit piece '{}' (id {}) keeps its loaded mesh: {}", mesh->get_name(), mesh->get_id(), problem);
            continue;
        }
        // As Kit_piece_parameters_operation applies it, without an undo entry: loading is not an edit.
        const std::vector<erhe::scene::Mesh_primitive> primitives = with_brush_primitives(mesh->get_primitives(), *scaled);
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root.item_host_mutex};
        swap_mesh_primitives(mesh, primitives);
        if (carries_node_physics(*mesh)) {
            set_node_collision_shape(*mesh, scaled->collision_shape);
        }
        set_brush_placement(*mesh, brush, GEO::NO_INDEX, GEO::NO_INDEX);
        ++report.follow_count;
    }
    if (report.piece_count > 0) {
        log_scene->info(
            "Scene '{}': {} kit pieces, {} follow the kit, {} keep their loaded mesh",
            scene_root.get_name(), report.piece_count, report.follow_count, report.problem_count
        );
    }
    return report;
}

auto Level_kits::snap_to_surface(
    Scene_root&                                               scene_root,
    const std::span<const std::shared_ptr<erhe::scene::Mesh>> meshes,
    const Surface_options&                                    options,
    const Kit_snap_apply                                      apply,
    std::vector<Kit_snap_result>&                             out_results
) -> std::size_t
{
    out_results.clear();
    std::vector<const erhe::scene::Mesh*> excluded;
    excluded.reserve(meshes.size());
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : meshes) {
        excluded.push_back(mesh.get());
    }
    std::vector<std::shared_ptr<Operation>> operations;
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : meshes) {
        const erhe::scene::Trs_transform world = mesh->world_from_node_transform();
        const Surface_placement placement = find_surface_placement(
            scene_root,
            mesh_local_bounds(*mesh),
            Surface_pose{.position = world.get_translation(), .rotation = world.get_rotation(), .scale = world.get_scale()},
            options,
            excluded
        );
        out_results.push_back(Kit_snap_result{.mesh = mesh, .placement = placement});
        if ((apply == Kit_snap_apply::query_only) || !placement.surface_found) {
            continue;
        }
        const glm::mat4 world_from_node =
            erhe::math::create_translation<float>(placement.pose.position) *
            glm::mat4_cast(placement.pose.rotation) *
            erhe::math::create_scale<float>(placement.pose.scale);
        const std::shared_ptr<erhe::scene::Node> parent = mesh->get_parent_node();
        const glm::mat4 parent_from_node = parent ? (parent->node_from_world() * world_from_node) : world_from_node;
        operations.push_back(
            std::make_shared<Node_transform_operation>(
                Node_transform_operation::Parameters{
                    .node                    = mesh,
                    .parent_from_node_before = mesh->parent_from_node_transform(),
                    .parent_from_node_after  = erhe::scene::Trs_transform{parent_from_node},
                    .xform_op_stack_before   = mesh->copy_xform_op_stack()
                }
            )
        );
    }
    const std::size_t moved_count = operations.size();
    if (!operations.empty()) {
        m_context.operation_stack->execute_now(
            (operations.size() == 1)
                ? operations.front()
                : std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)})
        );
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
    m_context.operation_stack->execute_now(std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)}));
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
    std::vector<Kit_parameter_values> values;
    values.reserve(placements.size());
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

        // The earlier pieces of this call are not surfaces yet.
        const Surface_placement surface = rest_pose(
            scene_root, brush->get_bounding_box(), placement.position, placement.rotation, snap, lengths.module, placement.surface
        );
        const glm::vec3 position        = surface.pose.position;
        const glm::quat rotation        = surface.pose.rotation;
        const glm::mat4 world_from_node = erhe::math::create_translation<float>(position) * glm::mat4_cast(rotation);

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
        write_kit_piece(*insert.node.get(), Kit_piece_data{.shape = shape.id, .parameters = values[i].to_canonical_text()});
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
    m_context.operation_stack->execute_now(std::make_shared<Compound_operation>(std::move(parameters)));
    result.node = group_node;
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
    m_context.operation_stack->execute_now(std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)}));
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
    if ((shape == nullptr) || (shape->id != out_record.shape)) {
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
}

void Level_kits::on_selection()
{
    if (m_parameter_preview.has_value() && !m_parameter_preview.value().mesh->is_selected()) {
        cancel_parameter_preview();
    }
}

void Level_kits::on_items_removed(const Removed_items& removed)
{
    if (m_parameter_preview.has_value() && removed.lookup.contains(m_parameter_preview.value().mesh.get())) {
        cancel_parameter_preview();
    }
}

void Level_kits::on_close_scene(const erhe::Item_host* const closing_host)
{
    if (m_parameter_preview.has_value() && (m_parameter_preview.value().mesh->get_item_host() == closing_host)) {
        cancel_parameter_preview();
    }
}

auto Level_kits::preview_parameters(const std::shared_ptr<erhe::scene::Node>& node, const Kit_parameter_values& values) -> std::string
{
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    std::shared_ptr<erhe::scene::Mesh> mesh;
    Kit_piece_data                     record;
    const std::string error = check_piece_values(node, values, mesh, record);
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
                .primitives      = mesh->get_primitives(),
                .collision_shape = get_node_collision_shape(*mesh),
                .brush           = placement.has_value() ? placement.value().brush : std::shared_ptr<Brush>{},
                .parameters      = record.parameters
            },
            .brush  = {}
        };
    }
    // The brush shown holds its cache entry; the previous step's brush goes with its last holder (D2.2).
    m_parameter_preview.value().brush = brush;
    apply_preview_primitives(m_context, mesh, with_brush_primitives(m_parameter_preview.value().before.primitives, *scaled));
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
    result.error = check_piece_values(node, values, mesh, record);
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
    } else {
        const std::optional<Brush_placement_data> placement = read_brush_placement(*mesh);
        before = Kit_piece_mesh_state{
            .primitives      = mesh->get_primitives(),
            .collision_shape = get_node_collision_shape(*mesh),
            .brush           = placement.has_value() ? placement.value().brush : std::shared_ptr<Brush>{},
            .parameters      = record.parameters
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
        .primitives      = with_brush_primitives(before.primitives, *scaled),
        .collision_shape = carries_node_physics(*mesh) ? scaled->collision_shape : before.collision_shape,
        .brush           = result.brush,
        .parameters      = parameters
    };
    m_context.operation_stack->execute_now(
        std::make_shared<Kit_piece_parameters_operation>(mesh, std::move(before), std::move(after))
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
    m_context.operation_stack->execute_now(
        (parameters.operations.size() == 1)
            ? parameters.operations.front()
            : std::make_shared<Compound_operation>(std::move(parameters))
    );
    return result;
}

} // namespace editor
