#include "level/foliage_stroke.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "level/foliage_system.hpp"
#include "level/kit_layout.hpp"
#include "level/kit_shape_library.hpp"
#include "level/level_queries.hpp"
#include "operations/operation.hpp"
#include "operations/operation_stack.hpp"
#include "prefabs/instance_structure.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_raytrace/iscene.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace editor {

namespace {

// FD9.4: the candidate rays of a stamp against the scene's surface meshes; the token is the hit mesh's address,
// read back within the stamp.
class Scene_foliage_ray : public IFoliage_ray
{
public:
    explicit Scene_foliage_ray(erhe::raytrace::IScene& committed_rt_scene)
        : m_rt_scene{committed_rt_scene}
    {
    }

    auto find_surface(const Foliage_candidate_ray& ray, Foliage_ray_hit& out_hit) -> Foliage_ray_outcome override
    {
        const Surface_ray_hit hit = find_ray_surface(m_rt_scene, ray.origin, ray.direction, ray.length, {});
        if (!hit.found || (hit.mesh == nullptr)) {
            return Foliage_ray_outcome::no_surface;
        }
        // An instance's content comes from its referenced file: nothing is painted into it.
        if (is_instance_content(*hit.mesh)) {
            return Foliage_ray_outcome::sealed_support;
        }
        out_hit.point   = hit.position;
        out_hit.normal  = hit.normal;
        out_hit.support = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(hit.mesh));
        return Foliage_ray_outcome::surface;
    }

private:
    erhe::raytrace::IScene& m_rt_scene;
};

[[nodiscard]] auto is_finite(const glm::vec3& v) -> bool
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

[[nodiscard]] auto support_node(const std::uint64_t token) -> std::shared_ptr<erhe::scene::Node>
{
    const erhe::scene::Mesh* const mesh = reinterpret_cast<const erhe::scene::Mesh*>(static_cast<std::uintptr_t>(token));
    if (mesh == nullptr) {
        return {};
    }
    return std::const_pointer_cast<erhe::scene::Node>(std::dynamic_pointer_cast<const erhe::scene::Node>(mesh->shared_from_this()));
}

// F5.5: the yaw about the up axis, the up axis world +Y or the surface normal.
[[nodiscard]] auto make_foliage_rotation(const Foliage_up up, const glm::vec3& normal, const float yaw_degrees) -> glm::quat
{
    const glm::vec3 world_up{0.0f, 1.0f, 0.0f};
    const glm::quat yaw = glm::angleAxis(glm::radians(yaw_degrees), world_up);
    if (up == Foliage_up::world) {
        return yaw;
    }
    return glm::normalize(glm::rotation(world_up, normal) * yaw);
}

} // anonymous namespace

Foliage_stroke::Foliage_stroke(App_context& context)
    : m_context{context}
{
}

Foliage_stroke::~Foliage_stroke() noexcept
{
    // An owner that leaves an open stroke (an early return, an exception) gets it cancelled, never a dangling live edit.
    if (m_open) {
        log_tools->warn("A foliage stroke was destroyed open: cancelled");
        cancel();
    }
}

auto Foliage_stroke::is_open() const -> bool
{
    return m_open;
}

void Foliage_stroke::set_brush_radius(const float radius)
{
    ERHE_VERIFY(m_open && std::isfinite(radius) && (radius > 0.0f));
    m_settings.brush_radius = radius;
}

auto Foliage_stroke::get_result() const -> const Foliage_stroke_result&
{
    return m_result;
}

void Foliage_stroke::set_abandon_callback(std::function<void()> callback)
{
    m_abandon_callback = std::move(callback);
}

auto Foliage_stroke::get_placed_nodes() const -> std::span<const std::shared_ptr<erhe::scene::Node>>
{
    return m_placed_nodes;
}

auto Foliage_stroke::get_removed_nodes() const -> std::span<const std::shared_ptr<erhe::scene::Node>>
{
    return m_removed_nodes;
}

auto Foliage_stroke::resolve_entry(const Foliage_palette_entry& entry, Entry& out_entry) const -> std::string
{
    const Level_kits&        level_kits = *m_context.level_kits;
    const Kit_shape_library& library    = level_kits.get_library();
    Kit_parameter_values     base{};
    if (entry.parameters.empty()) {
        const Kit_shape_definition* const shape = library.find(entry.shape);
        ERHE_VERIFY(shape != nullptr); // check_foliage_palette() passed
        base = level_kits.make_default_values(*m_scene_root, *shape);
    } else {
        const std::string error = read_kit_piece_record(library, entry.shape, entry.parameters, base);
        if (!error.empty()) {
            return error;
        }
    }
    const Kit_shape_definition* const shape = base.get_shape();
    out_entry.shape  = shape;
    out_entry.radius = entry.radius;
    // FD7.2: at the painted scene's lengths.
    const std::string variant_error = derive_foliage_variants(entry, base, level_kits.get_lengths(*m_scene_root), out_entry.variants);
    if (!variant_error.empty()) {
        return variant_error;
    }
    out_entry.materials.assign(shape->material_slots.size(), Kit_slot_material{});
    for (std::size_t slot = 0, end = entry.materials.size(); slot < end; ++slot) {
        const std::string& name = entry.materials[slot];
        if (name.empty()) {
            continue;
        }
        out_entry.materials[slot].material = Level_kits::find_scene_material(*m_scene_root, name);
        if (out_entry.materials[slot].material) {
            continue;
        }
        if (library.find_palette_material(name) == nullptr) {
            return fmt::format("slot '{}': no material '{}' in the scene or the kit palette", shape->material_slots[slot], name);
        }
        out_entry.materials[slot].palette_name = name;
    }
    return {};
}

auto Foliage_stroke::begin(
    Scene_root&                                  scene_root,
    const Foliage_stroke_kind                    kind,
    const Foliage_stroke_settings&               settings,
    const std::span<const Foliage_palette_entry> palette,
    const int                                    seed
) -> std::string
{
    if (m_open) {
        return "a stroke is open already";
    }
    if (const char* const reason = m_context.operation_stack->get_undo_block_reason(); reason != nullptr) {
        return std::string{"a stroke cannot begin while "} + reason;
    }
    if (!std::isfinite(settings.brush_radius) || !(settings.brush_radius > 0.0f)) {
        return fmt::format("the brush radius {} is not a length above 0 m", settings.brush_radius);
    }
    m_result = Foliage_stroke_result{};
    m_placed_nodes.clear();
    m_removed_nodes.clear();
    if (kind == Foliage_stroke_kind::remove) {
        if (!std::isfinite(settings.remove_density) || (settings.remove_density < 0.0f) || (settings.remove_density > 1.0f)) {
            return fmt::format("the remove density {} is not a finite number 0 - 1", settings.remove_density);
        }
        const std::string error = begin_remove(settings.remove_filter, palette);
        if (!error.empty()) {
            return error;
        }
        m_scene_root       = scene_root.shared_from_this();
        m_kind             = kind;
        m_settings         = settings;
        m_random           = make_foliage_remove_random(seed);
        m_remove_threshold = get_remove_threshold(settings.remove_density);
        return open_live_edit();
    }
    if (!std::isfinite(settings.paint_density) || (settings.paint_density < 0.0f)) {
        return fmt::format("the paint density {} is not a finite number >= 0", settings.paint_density);
    }
    const std::string palette_error = check_foliage_palette(m_context.level_kits->get_library(), palette);
    if (!palette_error.empty()) {
        return "palette " + palette_error;
    }
    bool any_weight = false;
    for (const Foliage_palette_entry& entry : palette) {
        any_weight = any_weight || (entry.weight > 0);
    }
    if (!any_weight) {
        return "the palette has no entry of weight above 0: nothing to paint";
    }

    m_scene_root = scene_root.shared_from_this();
    m_kind       = kind;
    m_settings   = settings;
    m_entries.resize(palette.size());
    m_sampler_entries.clear();
    for (std::size_t i = 0, end = palette.size(); i < end; ++i) {
        const std::string error = resolve_entry(palette[i], m_entries[i]);
        if (!error.empty()) {
            m_entries.clear();
            m_scene_root.reset();
            return fmt::format("palette entry {} ('{}'): {}", i, palette[i].shape, error);
        }
        m_sampler_entries.push_back(
            Foliage_sampler_entry{
                .weight          = palette[i].weight,
                .radius          = palette[i].radius,
                .yaw_min_degrees = palette[i].yaw_min_degrees,
                .yaw_max_degrees = palette[i].yaw_max_degrees,
                .variants        = static_cast<std::uint32_t>(m_entries[i].variants.size())
            }
        );
    }
    m_sampler_palette.assign(m_sampler_entries);
    m_random = make_foliage_paint_random(seed);
    return open_live_edit();
}

auto Foliage_stroke::begin_remove(const Foliage_remove_filter filter, const std::span<const Foliage_palette_entry> palette) -> std::string
{
    m_remove_shapes.clear();
    if (filter == Foliage_remove_filter::all_shapes) {
        return {};
    }
    const Kit_shape_library& library = m_context.level_kits->get_library();
    for (std::size_t i = 0, end = palette.size(); i < end; ++i) {
        const Kit_shape_definition* const shape = library.find(palette[i].shape);
        if (shape == nullptr) {
            m_remove_shapes.clear();
            return fmt::format("palette entry {}: unknown shape '{}'", i, palette[i].shape);
        }
        m_remove_shapes.push_back(shape);
    }
    return {};
}

auto Foliage_stroke::open_live_edit() -> std::string
{
    // FD9.2: a live edit of the stack; undo, redo, saves and other parties' operations wait for its end.
    std::string live_edit_refusal;
    m_token = m_context.operation_stack->begin_live_edit(
        "a foliage stroke is in progress (finish or cancel it first)",
        [this]() {
            on_abandoned();
            if (m_abandon_callback) {
                m_abandon_callback();
            }
        },
        live_edit_refusal
    );
    if (!live_edit_refusal.empty()) {
        m_entries.clear();
        m_remove_shapes.clear();
        m_scene_root.reset();
        return std::string{"a stroke cannot begin while "} + live_edit_refusal;
    }
    m_open = true;
    return {};
}

auto Foliage_stroke::stamp(const glm::vec3 center, const glm::vec3 normal) -> std::string
{
    ERHE_VERIFY(m_open);
    if (!is_finite(center)) {
        return fmt::format("the brush center ({}, {}, {}) is not finite", center.x, center.y, center.z);
    }
    if (m_kind == Foliage_stroke_kind::remove) {
        ++m_result.stamps;
        return remove_stamp(center);
    }
    const float length = glm::length(normal);
    if (!is_finite(normal) || !std::isfinite(length) || !(length > 0.0f)) {
        return fmt::format("the brush normal ({}, {}, {}) is not a direction", normal.x, normal.y, normal.z);
    }
    ++m_result.stamps;
    const Foliage_brush_frame frame = make_brush_frame(center, normal / length);
    return paint_stamp(frame);
}

auto Foliage_stroke::paint_stamp(const Foliage_brush_frame& frame) -> std::string
{
    Scene_root& scene_root = *m_scene_root;
    erhe::raytrace::IScene& rt_scene = scene_root.get_raytrace_scene();
    rt_scene.commit();
    // The index reads captured world positions: the moves of ancestors and the last stamp's inserts are applied first.
    scene_root.get_scene().update_node_transforms();
    const Foliage_index& index = scene_root.get_foliage_system().get_index();

    Scene_foliage_ray ray{rt_scene};
    const Foliage_stamp_counts counts = run_foliage_paint_stamp(
        m_random, frame, m_settings.brush_radius, m_settings.paint_density, m_sampler_palette, index, ray, m_scratch, m_placements
    );
    m_result.attempts           += counts.attempts;
    m_result.refused_by_spacing += counts.refused_by_spacing;
    m_result.without_surface    += counts.without_surface;
    m_result.on_sealed_support  += counts.on_sealed_support;
    if (m_placements.empty()) {
        return {};
    }

    m_kit_placements.clear();
    for (const Foliage_placement& placement : m_placements) {
        const Entry& entry = m_entries[placement.entry];
        Kit_placement kit_placement{};
        kit_placement.shape             = entry.shape;
        kit_placement.values            = entry.variants[placement.variant];
        kit_placement.position          = placement.point;
        kit_placement.rotation          = make_foliage_rotation(m_settings.up, placement.normal, placement.yaw_degrees);
        kit_placement.materials         = entry.materials;
        kit_placement.foliage_radius    = entry.radius;
        kit_placement.foliage_support   = support_node(placement.support);
        kit_placement.surface.direction = -placement.normal;
        // The record rests the origin (anchor) with the pose's own rotation, so a carry gives the stroke's pose back.
        kit_placement.surface.orient    = Surface_orient::off;
        kit_placement.surface.rest      = Surface_snap_rest::anchor;
        m_kit_placements.push_back(std::move(kit_placement));
    }
    Kit_place_result                 result;
    const std::shared_ptr<Operation> operation = m_context.level_kits->make_foliage_operation(scene_root, m_kit_placements, result);
    m_kit_placements.clear();
    if (!result.error.empty()) {
        return fmt::format("item {}: {}", result.error_index, result.error);
    }
    if (!operation) {
        return {};
    }
    if (!m_context.operation_stack->execute_in_live_edit(m_token, operation)) {
        return "the stroke's live edit has ended";
    }
    if (operation->is_refused()) {
        return operation->get_error();
    }
    m_result.placed += result.pieces.size();
    for (const Kit_placed_piece& piece : result.pieces) {
        m_placed_nodes.push_back(piece.node);
    }
    return {};
}

auto Foliage_stroke::is_removable(const erhe::scene::Node& node) const -> bool
{
    if (m_settings.remove_filter == Foliage_remove_filter::all_shapes) {
        return true;
    }
    // A mesh that is no kit piece has no shape; a former id names the same definition as the current one.
    const std::optional<Kit_piece_data> record = read_kit_piece(node);
    if (!record.has_value()) {
        return false;
    }
    const Kit_shape_definition* const shape = m_context.level_kits->get_library().find(record->shape);
    return (shape != nullptr) && (std::find(m_remove_shapes.begin(), m_remove_shapes.end(), shape) != m_remove_shapes.end());
}

auto Foliage_stroke::remove_stamp(const glm::vec3 center) -> std::string
{
    Scene_root& scene_root = *m_scene_root;
    // The index reads captured world positions: the moves of ancestors and the last stamp's removals are applied first.
    scene_root.get_scene().update_node_transforms();
    Foliage_system&      system = scene_root.get_foliage_system();
    const Foliage_index& index  = system.get_index();

    // FD10.1: the items whose placement point lies in the brush sphere, filtered, in remove order.
    index.find_within(center, m_settings.brush_radius, m_scratch.handles);
    m_remove_candidates.clear();
    for (const Foliage_index::Handle handle : m_scratch.handles) {
        const std::shared_ptr<erhe::scene::Node> node = system.get_node(handle);
        if (!node || !is_removable(*node)) {
            continue;
        }
        m_remove_candidates.push_back(Foliage_remove_candidate{.entry = index.get_entry(handle), .handle = handle});
    }
    std::sort(
        m_remove_candidates.begin(), m_remove_candidates.end(),
        [](const Foliage_remove_candidate& lhs, const Foliage_remove_candidate& rhs) -> bool {
            return is_before_in_remove_order(lhs.entry, rhs.entry);
        }
    );

    // FD10.2: one draw per candidate, removed or not, so the result follows from the seed and the item set.
    // An item the locked kit layout rule protects takes its draw and is kept.
    m_remove_nodes.clear();
    m_remove_items.clear();
    for (const Foliage_remove_candidate& candidate : m_remove_candidates) {
        if (!draw_remove(m_random, m_remove_threshold)) {
            continue;
        }
        std::shared_ptr<erhe::scene::Node> node = system.get_node(candidate.handle);
        if (locked_layout_refusal(*node).has_value()) {
            continue;
        }
        m_remove_items.push_back(node);
        m_remove_nodes.push_back(std::move(node));
    }
    m_remove_candidates.clear();

    // FD10.3: the Delete command's operation on exactly the drawn items, run in the stroke's live edit; it resolves
    // nesting and edit locks as it does for a selection.
    const std::shared_ptr<Operation> operation = m_context.selection->make_delete_operation(m_remove_items);
    m_remove_items.clear();
    if (!operation) {
        m_remove_nodes.clear();
        return {};
    }
    if (!m_context.operation_stack->execute_in_live_edit(m_token, operation)) {
        m_remove_nodes.clear();
        return "the stroke's live edit has ended";
    }
    if (operation->is_refused()) {
        m_remove_nodes.clear();
        return operation->get_error();
    }
    // A node the Delete command keeps (an edit-locked item) is still in the scene and is not counted.
    for (std::shared_ptr<erhe::scene::Node>& node : m_remove_nodes) {
        if (node->get_parent().lock()) {
            continue;
        }
        ++m_result.removed;
        m_removed_nodes.push_back(std::move(node));
    }
    m_remove_nodes.clear();
    return {};
}

void Foliage_stroke::close()
{
    m_open = false;
    m_entries.clear();
    m_remove_shapes.clear();
    m_scene_root.reset();
}

auto Foliage_stroke::end() -> Foliage_stroke_result
{
    if (!m_open) {
        return {};
    }
    m_result.recorded = (m_context.operation_stack->end_live_edit(m_token) == Live_edit_end::recorded);
    close();
    // A finished stroke holds no node: the undo history owns them.
    m_placed_nodes.clear();
    m_removed_nodes.clear();
    return m_result;
}

void Foliage_stroke::cancel()
{
    if (!m_open) {
        return;
    }
    static_cast<void>(m_context.operation_stack->cancel_live_edit(m_token));
    on_abandoned();
}

void Foliage_stroke::on_abandoned()
{
    m_placed_nodes.clear();
    m_removed_nodes.clear();
    m_result.placed  = 0;
    m_result.removed = 0;
    close();
}

} // namespace editor
