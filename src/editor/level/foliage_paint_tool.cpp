#include "level/foliage_paint_tool.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_settings.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/foliage_paint_config.hpp"
#include "editor_log.hpp"
#include "graphics/icon_set.hpp"
#include "level/foliage_sampler.hpp"
#include "level/kit_parameter_rows.hpp"
#include "level/kit_shape_library.hpp"
#include "level/kit_thumbnails.hpp"
#include "level/level_kits.hpp"
#include "level/level_queries.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/render_context.hpp"
#include "time.hpp"
#include "scene/hover_placement.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_scene_view.hpp"
#include "tools/tools.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_commands/input_arguments.hpp"
#include "erhe_imgui/imgui_item_recorder.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_window/window_event_handler.hpp"

#include <glm/gtc/constants.hpp>
#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstddef>
#include <optional>
#include <utility>

namespace editor {

namespace {

constexpr float c_brush_line_width = -2.0f; // pixels

[[nodiscard]] auto is_finite(const glm::vec3& v) -> bool
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

[[nodiscard]] auto get_normal_or_up(const std::optional<glm::vec3>& normal) -> glm::vec3
{
    if (normal.has_value() && is_finite(normal.value()) && (glm::length(normal.value()) > 0.0f)) {
        return glm::normalize(normal.value());
    }
    return glm::vec3{0.0f, 1.0f, 0.0f};
}

// The entry's values at the default level lengths, as level_set_foliage_paint_settings reads a parameters object.
void read_entry_values(const Kit_shape_library& library, const Foliage_palette_entry& entry, Foliage_palette_entry_rows& out_rows)
{
    out_rows.values = Kit_parameter_values{};
    out_rows.problem.clear();
    if (!entry.parameters.empty()) {
        out_rows.problem = read_kit_piece_record(library, entry.shape, entry.parameters, out_rows.values);
        return;
    }
    const Kit_shape_definition* const shape = library.find(entry.shape);
    if (shape == nullptr) {
        out_rows.problem = fmt::format("the kits have no shape '{}'", entry.shape);
        return;
    }
    const Kit_lengths lengths{};
    out_rows.values = make_default_values(*shape, lengths.module, lengths.wall_height);
}

} // anonymous namespace

auto c_str(const Foliage_stroke_outcome outcome) -> const char*
{
    switch (outcome) {
        case Foliage_stroke_outcome::none:      return "none";
        case Foliage_stroke_outcome::ended:     return "ended";
        case Foliage_stroke_outcome::cancelled: return "cancelled";
        case Foliage_stroke_outcome::abandoned: return "abandoned";
        case Foliage_stroke_outcome::refused:   return "refused";
    }
    return "";
}

#pragma region Commands
Foliage_paint_stroke_command::Foliage_paint_stroke_command(
    erhe::commands::Commands& commands,
    const char* const         name,
    Foliage_paint_tool&       tool,
    const Foliage_stroke_kind kind
)
    : Command{commands, name}
    , m_tool {tool}
    , m_kind {kind}
{
}

void Foliage_paint_stroke_command::try_ready()
{
    if (m_tool.is_stroke_ready(m_kind)) {
        set_ready();
    }
}

auto Foliage_paint_stroke_command::try_call_with_input(erhe::commands::Input_arguments&) -> bool
{
    // The press (called while Ready) begins the stroke, or does nothing; motion while the button is held is consumed,
    // the stamps follow the brush's travel (FD9.5).
    if (get_command_state() == erhe::commands::State::Ready) {
        if (m_tool.is_stroke_open()) {
            // The other button's press during a stroke is consumed and starts nothing: no lower command sees it.
            set_inactive();
            return true;
        }
        m_tool.begin_stroke(m_kind);
        return true;
    }
    return get_command_state() == erhe::commands::State::Active;
}

void Foliage_paint_stroke_command::on_inactive()
{
    m_tool.end_stroke(m_kind);
}

Foliage_paint_radius_command::Foliage_paint_radius_command(erhe::commands::Commands& commands, Foliage_paint_tool& tool)
    : Command{commands, "Foliage_paint.radius"}
    , m_tool {tool}
{
}

auto Foliage_paint_radius_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    return m_tool.adjust_radius(input.variant.vector2.relative_value.y);
}

Foliage_paint_walk_command::Foliage_paint_walk_command(erhe::commands::Commands& commands, Foliage_paint_tool& tool)
    : Command{commands, "Foliage_paint.walk"}
    , m_tool {tool}
{
}

auto Foliage_paint_walk_command::try_call() -> bool
{
    m_tool.continue_walk();
    return false;
}

Foliage_paint_cancel_command::Foliage_paint_cancel_command(erhe::commands::Commands& commands, Foliage_paint_tool& tool)
    : Command{commands, "Foliage_paint.cancel"}
    , m_tool {tool}
{
}

auto Foliage_paint_cancel_command::try_call() -> bool
{
    return m_tool.cancel_stroke();
}
#pragma endregion Commands

Foliage_paint_tool::Foliage_paint_tool(
    Foliage_paint_config&     config,
    Level_kits&               level_kits,
    erhe::commands::Commands& commands,
    App_context&              context,
    App_message_bus&          app_message_bus,
    Icon_set&                 icon_set,
    Tools&                    tools
)
    : Tool            {context, tools, Tool_flags::toolbox}
    , m_config        {config}
    , m_level_kits    {level_kits}
    , m_paint_command {commands, "Foliage_paint.paint",  *this, Foliage_stroke_kind::paint}
    , m_remove_command{commands, "Foliage_paint.remove", *this, Foliage_stroke_kind::remove}
    , m_radius_command{commands, *this}
    , m_cancel_command{commands, *this}
    , m_walk_command  {commands, *this}
    , m_stroke        {context}
{
    ERHE_PROFILE_FUNCTION();

    set_base_priority(c_priority);
    set_description  ("Foliage Paint Tool");
    set_icon         (icon_set.custom_icons, icon_set.icons.brush_small);

    m_paint_command .set_host(this);
    m_remove_command.set_host(this);
    m_radius_command.set_host(this);
    m_cancel_command.set_host(this);
    m_walk_command  .set_host(this);
    commands.register_command(&m_paint_command);
    commands.register_command(&m_remove_command);
    commands.register_command(&m_radius_command);
    commands.register_command(&m_cancel_command);
    commands.register_command(&m_walk_command);
    commands.bind_command_to_update(&m_walk_command);
    // Called at the press, with exactly the binding's modifiers: a press with others reaches the camera's drags.
    commands.bind_command_to_mouse_drag (&m_paint_command,  erhe::window::Mouse_button_left,  true, 0u, erhe::commands::Drag_modifier_match::exact);
    commands.bind_command_to_mouse_drag (&m_remove_command, erhe::window::Mouse_button_right, true, 0u, erhe::commands::Drag_modifier_match::exact);
    commands.bind_command_to_mouse_wheel(&m_radius_command, erhe::window::Key_modifier_bit_shift);
    // Any modifiers, so a Shift still held from the wheel does not block it; it declines without a stroke.
    commands.bind_command_to_key        (&m_cancel_command, erhe::window::Key_escape, erhe::commands::Button_trigger::Button_pressed);

    m_hover_scene_view_subscription = app_message_bus.hover_scene_view.subscribe(
        [this](Hover_scene_view_message& message) {
            on_hover_scene_view(message);
        }
    );
    m_hover_mesh_subscription = app_message_bus.hover_mesh.subscribe(
        [this](Hover_mesh_message&) {
            update_brush();
        }
    );
    m_close_scene_subscription = app_message_bus.close_scene.subscribe(
        [this](Close_scene_message& message) {
            on_close_scene(static_cast<const erhe::Item_host*>(message.scene_root.get()));
        }
    );
    m_stroke.set_abandon_callback([this]() { on_stroke_abandoned(); });

    m_config.brush_radius   = std::clamp(m_config.brush_radius, c_foliage_min_brush_radius, c_foliage_max_brush_radius);
    m_config.paint_density  = std::clamp(m_config.paint_density, c_foliage_min_paint_density, c_foliage_max_paint_density);
    m_config.remove_density = std::clamp(m_config.remove_density, 0.0f, 1.0f);
    m_config.seed           = std::clamp(m_config.seed, 0, kit_seed_max);
    parse_palette();
}

Foliage_paint_tool::~Foliage_paint_tool() noexcept
{
    // The shutdown abandons an open stroke before the parts go (Operation_stack::abandon_live_edit()).
    ERHE_VERIFY(!m_stroke.is_open());
}

void Foliage_paint_tool::parse_palette()
{
    m_palette.clear();
    m_palette_problem.clear();
    if (!m_config.palette.empty()) {
        std::vector<Foliage_palette_entry> entries;
        const std::string error = parse_foliage_palette_text(m_config.palette, entries);
        if (error.empty()) {
            m_palette = std::move(entries);
        } else {
            m_palette_problem = "the saved palette does not read: " + error;
            log_tools->warn("Foliage Paint: {}; the palette is empty", m_palette_problem);
        }
    }
    m_entry_rows.clear();
    for (const Foliage_palette_entry& entry : m_palette) {
        read_entry_values(m_level_kits.get_library(), entry, m_entry_rows.emplace_back());
    }
    m_paint_refusal.clear();
    if (!m_palette_problem.empty()) {
        m_paint_refusal = m_palette_problem;
        return;
    }
    if (m_palette.empty()) {
        m_paint_refusal = "the palette is empty: add an entry";
        return;
    }
    const std::string problem = check_foliage_palette(m_level_kits.get_library(), m_palette);
    if (!problem.empty()) {
        m_paint_refusal = "palette " + problem;
        return;
    }
    const bool any_weight = std::any_of(m_palette.begin(), m_palette.end(), [](const Foliage_palette_entry& entry) { return entry.weight > 0; });
    if (!any_weight) {
        m_paint_refusal = "the palette has no entry of weight above 0: nothing to paint";
    }
}

void Foliage_paint_tool::touch_settings()
{
    if (m_context.app_settings != nullptr) {
        m_context.app_settings->settings_store().touch();
    }
}

auto Foliage_paint_tool::get_config() const -> const Foliage_paint_config&
{
    return m_config;
}

void Foliage_paint_tool::set_brush_radius(const float radius)
{
    m_config.brush_radius = std::clamp(radius, c_foliage_min_brush_radius, c_foliage_max_brush_radius);
    // The open stroke follows: its next stamp and its travel step take the new radius.
    if (m_stroke.is_open()) {
        m_stroke.set_brush_radius(m_config.brush_radius);
    }
    update_brush_circle();
    touch_settings();
}

void Foliage_paint_tool::set_paint_density(const float density)
{
    m_config.paint_density = std::clamp(density, c_foliage_min_paint_density, c_foliage_max_paint_density);
    touch_settings();
}

void Foliage_paint_tool::set_remove_density(const float density)
{
    m_config.remove_density = std::clamp(density, 0.0f, 1.0f);
    touch_settings();
}

void Foliage_paint_tool::set_up_axis(const Foliage_up up)
{
    m_config.align_to_surface = (up == Foliage_up::surface_normal);
    touch_settings();
}

void Foliage_paint_tool::set_remove_filter(const Foliage_remove_filter filter)
{
    m_config.remove_only_palette_shapes = (filter == Foliage_remove_filter::palette_shapes);
    touch_settings();
}

void Foliage_paint_tool::set_seed(const int seed)
{
    m_config.seed = std::clamp(seed, 0, kit_seed_max);
    touch_settings();
}

void Foliage_paint_tool::set_palette(const std::span<const Foliage_palette_entry> entries)
{
    m_config.palette = entries.empty() ? std::string{} : make_foliage_palette_text(entries);
    parse_palette();
    touch_settings();
}

auto Foliage_paint_tool::get_palette() const -> std::span<const Foliage_palette_entry>
{
    return m_palette;
}

auto Foliage_paint_tool::get_paint_refusal() const -> const std::string&
{
    return m_paint_refusal;
}

auto Foliage_paint_tool::get_brush() const -> const std::optional<Foliage_brush>&
{
    return m_brush;
}

auto Foliage_paint_tool::is_stroke_open() const -> bool
{
    return m_stroke.is_open();
}

auto Foliage_paint_tool::get_stroke_kind() const -> Foliage_stroke_kind
{
    return m_stroke_kind;
}

auto Foliage_paint_tool::get_stroke_result() const -> const Foliage_stroke_result&
{
    return m_stroke.get_result();
}

auto Foliage_paint_tool::get_stroke_scene() const -> std::shared_ptr<Scene_root>
{
    return m_stroke.is_open() ? m_stroke_scene.lock() : std::shared_ptr<Scene_root>{};
}

auto Foliage_paint_tool::get_last_stroke() const -> const Foliage_paint_last_stroke&
{
    return m_last_stroke;
}

void Foliage_paint_tool::handle_priority_update(const int old_priority, const int new_priority)
{
    if (new_priority < old_priority) {
        // A drag in progress ends with its stroke; an inactive command leaves no active mouse command behind.
        m_paint_command .set_inactive();
        m_remove_command.set_inactive();
        if (m_stroke.is_open()) {
            finish_stroke(Foliage_stroke_outcome::ended);
        }
        disable_command_host();
        m_brush.reset();
    }
    if (new_priority > old_priority) {
        enable_command_host();
        update_brush();
    }
}

void Foliage_paint_tool::on_hover_scene_view(Hover_scene_view_message& message)
{
    Tool::on_message(message);
    // A stroke whose view is destroyed (its viewport closed) ends as a release would.
    if (message.destroyed_scene_view != nullptr) {
        on_stroke_view_gone(message.destroyed_scene_view);
    }
    update_brush();
}

void Foliage_paint_tool::on_close_scene(const erhe::Item_host* const closing_host)
{
    if (m_brush.has_value()) {
        const std::shared_ptr<Scene_root> scene_root = m_brush->scene_root.lock();
        if (!scene_root || (static_cast<const erhe::Item_host*>(scene_root.get()) == closing_host)) {
            m_brush.reset();
        }
    }
}

auto Foliage_paint_tool::get_stamp_centers() const -> std::span<const glm::vec3>
{
    return std::span<const glm::vec3>{m_stamp_centers.data(), m_stamp_center_count};
}

auto Foliage_paint_tool::is_stamp_centers_truncated() const -> bool
{
    return m_stamp_centers_truncated;
}

auto Foliage_paint_tool::is_pointer_in_view(const Scene_view& scene_view) const -> bool
{
    const Viewport_scene_view* const viewport_scene_view = dynamic_cast<const Viewport_scene_view*>(&scene_view);
    if (viewport_scene_view == nullptr) {
        return true;
    }
    const std::optional<glm::vec2> position = viewport_scene_view->get_position_in_viewport();
    const erhe::math::Viewport&    viewport = viewport_scene_view->get_window_viewport();
    return
        position.has_value() && (position->x >= 0.0f) && (position->y >= 0.0f) &&
        (position->x < static_cast<float>(viewport.width)) && (position->y < static_cast<float>(viewport.height));
}

void Foliage_paint_tool::update_brush()
{
    if (!is_enabled()) {
        m_brush.reset();
        return;
    }
    Scene_view* const scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        m_brush.reset();
        return;
    }
    const std::shared_ptr<Scene_root> scene_root = scene_view->get_scene_root();
    // A drag keeps its viewport hovered (pointer capture) while the pointer is over another window: the brush is
    // only where the viewport shows it.
    if (!is_pointer_in_view(*scene_view)) {
        m_brush.reset();
        // Leaving the viewport breaks the travel: the re-entry starts a new segment.
        m_travel_broken = m_stroke.is_open();
        return;
    }
    // FD9.7: a stroke belongs to the scene of its press.
    if (m_stroke.is_open() && (scene_root != m_stroke_scene.lock())) {
        return;
    }
    const std::optional<glm::vec3>           ray_origin    = scene_view->get_control_ray_origin_in_world();
    const std::optional<glm::vec3>           ray_direction = scene_view->get_control_ray_direction_in_world();
    if (!scene_root || !ray_origin.has_value() || !ray_direction.has_value() || !(glm::length(ray_direction.value()) > 0.0f)) {
        m_brush.reset();
        return;
    }
    std::optional<Hover_placement_hit> hit = m_level_kits.get_placement_hover(*scene_view);
    // F4.1: the brush is on a surface mesh; a grid or analytic hover (the grid in a floor's top plane wins a tie by
    // rounding) or a pass_through mesh (water) gives way to the surface hit along the same ray.
    if (!hit.has_value() || (hit->source != Hover_placement_source::surface) || !hit->mesh || !is_placement_surface(*hit->mesh)) {
        hit = m_level_kits.find_placement_surface(*scene_root, ray_origin.value(), glm::normalize(ray_direction.value()));
    }
    if (!hit.has_value() || !hit->mesh || !is_finite(hit->position)) {
        m_brush.reset();
        return;
    }
    m_brush = Foliage_brush{
        .scene_root    = scene_root,
        .center        = hit->position,
        .normal        = get_normal_or_up(hit->normal),
        .ray_origin    = ray_origin.value(),
        .ray_direction = glm::normalize(ray_direction.value())
    };
    update_brush_circle();
    stamp_by_travel();
}

void Foliage_paint_tool::update_brush_circle()
{
    if (!m_brush.has_value()) {
        return;
    }
    const Foliage_brush_frame frame  = make_brush_frame(m_brush->center, m_brush->normal);
    const float               radius = m_config.brush_radius;
    for (std::size_t i = 0; i < c_foliage_brush_segments; ++i) {
        const float angle = (glm::two_pi<float>() * static_cast<float>(i)) / static_cast<float>(c_foliage_brush_segments);
        const float c     = std::cos(angle);
        const float s     = std::sin(angle);
        m_circle_points[i] = frame.center + (radius * ((c * frame.tangent0) + (s * frame.tangent1)));
    }
}

auto Foliage_paint_tool::is_stroke_ready(const Foliage_stroke_kind kind) const -> bool
{
    if (!is_enabled()) {
        return false;
    }
    // F9.1: while the tool is active the plain right press is its own, with a brush or not; during a stroke either
    // button's plain press is (try_call_with_input() consumes it and starts nothing).
    if ((kind == Foliage_stroke_kind::remove) || m_stroke.is_open()) {
        return true;
    }
    if (!m_brush.has_value() || m_brush->scene_root.expired()) {
        return false;
    }
    if ((m_context.operation_stack == nullptr) || (m_context.operation_stack->get_undo_block_reason() != nullptr)) {
        return false;
    }
    return m_paint_refusal.empty();
}

void Foliage_paint_tool::begin_stroke(const Foliage_stroke_kind kind)
{
    if (!m_brush.has_value() || m_stroke.is_open()) {
        return;
    }
    const std::shared_ptr<Scene_root> scene_root = m_brush->scene_root.lock();
    if (!scene_root) {
        return;
    }
    // FD9.1: the tool's values at the press.
    const Foliage_stroke_settings settings{
        .brush_radius   = m_config.brush_radius,
        .paint_density  = m_config.paint_density,
        .up             = m_config.align_to_surface ? Foliage_up::surface_normal : Foliage_up::world,
        .remove_density = m_config.remove_density,
        .remove_filter  = m_config.remove_only_palette_shapes ? Foliage_remove_filter::palette_shapes : Foliage_remove_filter::all_shapes
    };
    const int         seed    = m_config.seed;
    const std::string refusal = m_stroke.begin(*scene_root, kind, settings, m_palette, seed);
    if (!refusal.empty()) {
        log_tools->warn("Foliage Paint: the {} stroke did not begin: {}", (kind == Foliage_stroke_kind::paint) ? "paint" : "remove", refusal);
        m_last_stroke = Foliage_paint_last_stroke{
            .kind       = kind,
            .outcome    = Foliage_stroke_outcome::refused,
            .result     = {},
            .refusal    = refusal,
            .scene_name = scene_root->get_name(),
            .seed       = seed
        };
        return;
    }
    m_stroke_kind       = kind;
    m_stroke_seed       = seed;
    m_stroke_scene      = scene_root;
    m_stroke_scene_name = scene_root->get_name();
    clear_walk();
    m_stamp_center_count      = 0;
    m_stamp_centers_truncated = false;
    m_walk_from         = m_brush.value();
    m_stroke_scene_view = get_hover_scene_view();
    m_press_button_bits = (m_context.commands != nullptr) ? m_context.commands->last_mouse_button_bits() : 0u;
    stamp_at(m_brush->center, m_brush->normal);
}

void Foliage_paint_tool::stamp_at(const glm::vec3 center, const glm::vec3 normal)
{
    m_last_stamp_center = center;
    if (m_stamp_center_count < m_stamp_centers.size()) {
        m_stamp_centers[m_stamp_center_count] = center;
        ++m_stamp_center_count;
    } else {
        m_stamp_centers_truncated = true;
    }
    const std::string error = m_stroke.stamp(center, normal);
    if (!error.empty()) {
        log_tools->warn("Foliage Paint: a stamp was refused: {}", error);
    }
}

void Foliage_paint_tool::stamp_by_travel()
{
    // F5.1, G4: each hover change queues the travel to the new brush; the walk stamps every half radius of it.
    if (!m_stroke.is_open() || !m_brush.has_value()) {
        return;
    }
    push_walk_point(m_brush.value(), m_travel_broken ? Foliage_walk_start::new_segment : Foliage_walk_start::continued);
    m_travel_broken = false;
    walk_travel(Foliage_walk_limit::frame_budget);
}

void Foliage_paint_tool::continue_walk()
{
    // Not a poll of derived state: the frame's share of the walk queue the stroke owns, while it is not empty.
    if (m_walk_count > 0) {
        walk_travel(Foliage_walk_limit::frame_budget);
    }
}

void Foliage_paint_tool::push_walk_point(const Foliage_brush& brush, const Foliage_walk_start start)
{
    // A full ring walks its head segment to the end first: the queue's storage is fixed, no travel is dropped.
    while (m_walk_count == m_walk_ring.size()) {
        walk_travel(Foliage_walk_limit::head_segment);
    }
    m_walk_ring[(m_walk_head + m_walk_count) % m_walk_ring.size()] = Foliage_walk_point{.brush = brush, .start = start};
    ++m_walk_count;
}

void Foliage_paint_tool::clear_walk()
{
    m_walk_head     = 0;
    m_walk_count    = 0;
    m_walked        = 0.0f;
    m_since_stamp   = 0.0f;
    m_travel_broken = false;
}

auto Foliage_paint_tool::get_pending_stamps() const -> std::size_t
{
    const float step = 0.5f * m_config.brush_radius;
    if (!(step > 0.0f)) {
        return 0;
    }
    float       travel = m_since_stamp;
    glm::vec3   from   = m_walk_from.center;
    for (std::size_t i = 0; i < m_walk_count; ++i) {
        const Foliage_walk_point& point = m_walk_ring[(m_walk_head + i) % m_walk_ring.size()];
        if (point.start == Foliage_walk_start::continued) {
            travel += glm::distance(from, point.brush.center) - ((i == 0) ? m_walked : 0.0f);
        }
        from = point.brush.center;
    }
    return static_cast<std::size_t>(std::floor(travel / step));
}

void Foliage_paint_tool::walk_travel(const Foliage_walk_limit limit)
{
    // F5.1: half-radius steps along each queued chord, the distance since the last stamp carried
    // (doc/editor/level_tools.md "Foliage paint", Stamps).
    const std::shared_ptr<Scene_root> scene_root = m_stroke_scene.lock();
    if (!m_stroke.is_open() || !scene_root) {
        clear_walk();
        return;
    }
    // One budget per frame for every path: 16 stamps, and everything above 256 pending stamps.
    std::size_t allowed = std::numeric_limits<std::size_t>::max();
    if (limit == Foliage_walk_limit::frame_budget) {
        const std::uint64_t frame = (m_context.time != nullptr) ? m_context.time->get_frame_number() : 0;
        if (frame != m_budget_frame) {
            m_budget_frame = frame;
            m_budget_used  = 0;
        }
        const std::size_t pending = get_pending_stamps();
        const std::size_t budget  = c_foliage_walk_stamps_per_frame + ((pending > c_foliage_walk_pending_stamps) ? (pending - c_foliage_walk_pending_stamps) : 0);
        allowed = (budget > m_budget_used) ? (budget - m_budget_used) : 0;
    }
    std::size_t stamps = 0;
    while (m_walk_count > 0) {
        const Foliage_walk_point& point = m_walk_ring[m_walk_head];
        const Foliage_brush&      to    = point.brush;
        const float               step  = 0.5f * m_config.brush_radius;
        if (point.start == Foliage_walk_start::new_segment) {
            // The pointer left the viewport: no travel across the outside; the re-entry stamps as any step does.
            if (glm::distance(to.center, m_last_stamp_center) >= step) {
                if (stamps >= allowed) {
                    break;
                }
                stamp_at(to.center, to.normal);
                ++stamps;
                m_since_stamp = 0.0f;
            }
        } else {
            const float length = glm::distance(m_walk_from.center, to.center);
            bool        paused = false;
            while ((step > 0.0f) && (length > 0.0f) && ((m_walked + std::max(0.0f, step - m_since_stamp)) <= length)) {
                if (stamps >= allowed) {
                    paused = true;
                    break;
                }
                // A step already exceeded (Shift + wheel shrank it) stamps at the walk's current point, so t >= 0.
                m_walked      += std::max(0.0f, step - m_since_stamp);
                m_since_stamp  = 0.0f;
                const float t  = m_walked / length;
                ++stamps;
                if (t >= 1.0f) {
                    stamp_at(to.center, to.normal);
                } else {
                    const glm::vec3 origin    = glm::mix(m_walk_from.ray_origin, to.ray_origin, t);
                    const glm::vec3 target    = glm::mix(m_walk_from.center, to.center, t);
                    const glm::vec3 direction = target - origin;
                    if (glm::length(direction) > 0.0f) {
                        const std::optional<Hover_placement_hit> hit = m_level_kits.find_placement_surface(*scene_root, origin, glm::normalize(direction));
                        if (hit.has_value()) {
                            stamp_at(hit->position, get_normal_or_up(hit->normal));
                        }
                    }
                }
                if (!m_stroke.is_open()) {
                    clear_walk();
                    return;
                }
            }
            if (paused) {
                break;
            }
            m_since_stamp += std::max(0.0f, length - m_walked);
        }
        m_walk_from = to;
        m_walked    = 0.0f;
        m_walk_head = (m_walk_head + 1) % m_walk_ring.size();
        --m_walk_count;
        if (limit == Foliage_walk_limit::head_segment) {
            break;
        }
    }
    if (limit == Foliage_walk_limit::frame_budget) {
        m_budget_used += stamps;
    }
}

void Foliage_paint_tool::on_stroke_view_gone(const Scene_view* const scene_view)
{
    // The stroke's view hidden or destroyed: the stroke ends as a release would, Active drag or not.
    if (!m_stroke.is_open() || (scene_view == nullptr) || (scene_view != m_stroke_scene_view)) {
        return;
    }
    finish_stroke(Foliage_stroke_outcome::ended);
    m_paint_command .set_inactive();
    m_remove_command.set_inactive();
}

void Foliage_paint_tool::walk_to_release()
{
    // The hover of the release event's motion is read a frame later: the pointer ray at the release is taken here.
    Viewport_scene_view* const scene_view = dynamic_cast<Viewport_scene_view*>(get_hover_scene_view());
    const std::shared_ptr<Scene_root> scene_root = m_stroke_scene.lock();
    const erhe::math::Viewport* const window_viewport = (scene_view != nullptr) ? &scene_view->get_window_viewport() : nullptr;
    const glm::vec2 pointer = (m_context.commands != nullptr) ? m_context.commands->last_mouse_position() : glm::vec2{-1.0f};
    // Only a release over the stroke's viewport moves the brush; a pointer that left it (onto a window) moves nothing.
    const bool over_viewport =
        (window_viewport != nullptr) &&
        (pointer.x >= static_cast<float>(window_viewport->x)) && (pointer.x < static_cast<float>(window_viewport->x + window_viewport->width)) &&
        (pointer.y >= static_cast<float>(window_viewport->y)) && (pointer.y < static_cast<float>(window_viewport->y + window_viewport->height));
    if (over_viewport && scene_root && (scene_view->get_scene_root() == scene_root)) {
        scene_view->update_pointer_2d_position(scene_view->get_viewport_from_window(pointer));
        scene_view->update_hover(true);
        const std::optional<glm::vec3> ray_origin    = scene_view->get_control_ray_origin_in_world();
        const std::optional<glm::vec3> ray_direction = scene_view->get_control_ray_direction_in_world();
        if (ray_origin.has_value() && ray_direction.has_value() && (glm::length(ray_direction.value()) > 0.0f)) {
            const glm::vec3 direction = glm::normalize(ray_direction.value());
            const std::optional<Hover_placement_hit> hit = m_level_kits.find_placement_surface(*scene_root, ray_origin.value(), direction);
            if (hit.has_value() && is_finite(hit->position)) {
                m_brush = Foliage_brush{
                    .scene_root    = scene_root,
                    .center        = hit->position,
                    .normal        = get_normal_or_up(hit->normal),
                    .ray_origin    = ray_origin.value(),
                    .ray_direction = direction
                };
                update_brush_circle();
                push_walk_point(m_brush.value(), m_travel_broken ? Foliage_walk_start::new_segment : Foliage_walk_start::continued);
                m_travel_broken = false;
            }
        }
    }
    // The release completes the walk: no travel is dropped.
    walk_travel(Foliage_walk_limit::complete);
}

void Foliage_paint_tool::end_stroke(const Foliage_stroke_kind kind)
{
    if (m_stroke.is_open() && (m_stroke_kind == kind)) {
        // Only a release (its button no longer down) walks to the pointer; a binding rebuild or deactivation does not.
        const std::uint32_t buttons = (m_context.commands != nullptr) ? m_context.commands->last_mouse_button_bits() : 0u;
        if ((m_press_button_bits & ~buttons) != 0u) {
            walk_to_release();
        }
        if (m_stroke.is_open()) {
            finish_stroke(Foliage_stroke_outcome::ended);
        }
    }
}

auto Foliage_paint_tool::cancel_stroke() -> bool
{
    if (!m_stroke.is_open()) {
        return false;
    }
    finish_stroke(Foliage_stroke_outcome::cancelled);
    return true;
}

void Foliage_paint_tool::finish_stroke(const Foliage_stroke_outcome outcome)
{
    clear_walk();
    m_stroke_scene_view = nullptr;
    Foliage_stroke_result result{};
    switch (outcome) {
        case Foliage_stroke_outcome::ended: {
            result = m_stroke.end();
            break;
        }
        case Foliage_stroke_outcome::cancelled: {
            m_stroke.cancel();
            result = m_stroke.get_result();
            break;
        }
        default: {
            result = m_stroke.get_result();
            break;
        }
    }
    m_last_stroke = Foliage_paint_last_stroke{
        .kind       = m_stroke_kind,
        .outcome    = outcome,
        .result     = result,
        .refusal    = {},
        .scene_name = m_stroke_scene_name,
        .seed       = m_stroke_seed
    };
    m_stroke_scene.reset();
    // F5.6, FD8.3: the seed advances after every stroke that began.
    m_config.seed = kit_next_seed(m_stroke_seed);
    touch_settings();
    log_tools->info(
        "Foliage Paint: {} stroke {} in scene '{}': {} stamp(s), {} placed, {} removed{}",
        (m_stroke_kind == Foliage_stroke_kind::paint) ? "paint" : "remove", c_str(outcome), m_stroke_scene_name,
        result.stamps, result.placed, result.removed, result.recorded ? ", one undo entry" : ""
    );
}

void Foliage_paint_tool::on_stroke_abandoned()
{
    // The stack ended the stroke (state only: no operation runs here).
    finish_stroke(Foliage_stroke_outcome::abandoned);
}

auto Foliage_paint_tool::adjust_radius(const float wheel_steps) -> bool
{
    // F4.2: over a viewport, with a brush or not.
    const Scene_view* const scene_view = get_hover_scene_view();
    if (!is_enabled() || (scene_view == nullptr) || !is_pointer_in_view(*scene_view) || (wheel_steps == 0.0f) || !std::isfinite(wheel_steps)) {
        return false;
    }
    set_brush_radius(m_config.brush_radius * std::pow(c_foliage_radius_wheel_step, wheel_steps));
    return true;
}

void Foliage_paint_tool::tool_render(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    if (!is_enabled() || !m_brush.has_value() || !context.scene_root || (m_brush->scene_root.lock() != context.scene_root) ||
        (context.app_context.editor_settings == nullptr)) {
        return;
    }
    const Debug_visualizations_style& style = context.app_context.editor_settings->debug_visualizations_style;
    const bool       removing = m_stroke.is_open() && (m_stroke_kind == Foliage_stroke_kind::remove);
    const glm::vec4& color    = removing ? style.foliage_remove_color : style.foliage_paint_color;
    erhe::renderer::Primitive_renderer line_renderer = context.get(
        {
            .primitive_type    = erhe::graphics::Primitive_type::line,
            .stencil_reference = 2,
            .draw_visible      = true,
            .draw_hidden       = true,
            .thin_lines        = false,
            .xray              = true
        }
    );
    for (std::size_t i = 0; i < c_foliage_brush_segments; ++i) {
        const glm::vec3& p0 = m_circle_points[i];
        const glm::vec3& p1 = m_circle_points[(i + 1) % c_foliage_brush_segments];
        line_renderer.add_line(color, c_brush_line_width, p0, color, c_brush_line_width, p1);
    }
    const glm::vec3 tip = m_brush->center + ((0.25f * m_config.brush_radius) * m_brush->normal);
    line_renderer.add_line(color, c_brush_line_width, m_brush->center, color, c_brush_line_width, tip);
}

auto Foliage_paint_tool::entry_parameter_rows(Foliage_palette_entry& entry, Foliage_palette_entry_rows& rows) -> bool
{
    if (!rows.problem.empty()) {
        kit_message_line(rows.problem);
        return false;
    }
    const Kit_shape_definition* const shape = rows.values.get_shape();
    if (shape == nullptr) {
        return false;
    }
    const Kit_lengths lengths{};
    Kit_row_edit      edit = Kit_row_edit::none;
    // FD7.2: the variants of an entry that derives values take their seeds from the entry, not from its seed value.
    const bool derives = is_foliage_seed_derived(rows.values);
    for (std::size_t index = 0, end = shape->parameters.size(); index < end; ++index) {
        const bool variants_seed = derives && (shape->parameters[index].name == "seed");
        ImGui::BeginDisabled(variants_seed);
        edit = merge(edit, kit_parameter_row(rows.values, index, Kit_row_label::beside, lengths));
        ImGui::EndDisabled();
    }
    bool written = false;
    if ((edit == Kit_row_edit::changed) || (edit == Kit_row_edit::committed)) {
        rows.message = rows.values.validate();
        if (rows.message.empty()) {
            entry.parameters = rows.values.to_canonical_text();
            written = true;
        } else if (edit == Kit_row_edit::committed) {
            // Refused values: the rows show the entry's values again, with the message.
            const std::string message = rows.message;
            read_entry_values(m_level_kits.get_library(), entry, rows);
            rows.message = message;
        }
    } else if (edit == Kit_row_edit::cancelled) {
        read_entry_values(m_level_kits.get_library(), entry, rows);
        rows.message.clear();
    }
    kit_message_line(rows.message);
    return written;
}

void Foliage_paint_tool::palette_imgui(const std::shared_ptr<Scene_root>& scene_root)
{
    const Kit_shape_library& library = m_level_kits.get_library();
    Kit_thumbnails& thumbnails = m_level_kits.get_thumbnails();
    // The thumbnails show at the lengths a window followed; the first follow takes the hovered scene's.
    if (!thumbnails.get_lengths().has_value() && scene_root) {
        thumbnails.follow_scene(scene_root);
    }
    const bool  show_thumbnails = thumbnails.get_lengths().has_value();
    const float thumbnail_size  = 2.0f * ImGui::GetTextLineHeightWithSpacing();

    bool                       changed = false;
    std::optional<std::size_t> remove_index;
    std::optional<std::size_t> move_up_index; // the entry that swaps with the one before it
    for (std::size_t i = 0, end = m_palette.size(); i < end; ++i) {
        Foliage_palette_entry&            entry = m_palette[i];
        const Kit_shape_definition* const shape = library.find(entry.shape);
        ImGui::PushID(static_cast<int>(i));
        ImGui::Separator();
        if ((shape != nullptr) && show_thumbnails) {
            static_cast<void>(thumbnails.draw(*shape, thumbnail_size));
            ImGui::SameLine();
        }
        ImGui::TextUnformatted((shape != nullptr) ? shape->display_name.c_str() : entry.shape.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            remove_index = i;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(i == 0);
        if (ImGui::SmallButton("Move Up")) {
            move_up_index = i;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled((i + 1) == end);
        if (ImGui::SmallButton("Move Down")) {
            move_up_index = i + 1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(entry.parameters.empty());
        if (ImGui::SmallButton("Reset")) {
            entry.parameters.clear();
            changed = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", "Return the entry's parameters to the shape's defaults.");
        }
        int weight = static_cast<int>(std::min(entry.weight, 1000u));
        if (ImGui::DragInt("Weight", &weight, 1.0f, 0, 1000)) {
            entry.weight = static_cast<std::uint32_t>(std::clamp(weight, 0, 1000));
            changed = true;
        }
        if (ImGui::DragFloat("Radius", &entry.radius, 0.01f, 0.0f, 100.0f, "%.2f m")) {
            entry.radius = std::clamp(entry.radius, 0.0f, 100.0f);
            changed = true;
        }
        if (ImGui::DragFloatRange2("Yaw", &entry.yaw_min_degrees, &entry.yaw_max_degrees, 1.0f, 0.0f, 360.0f, "%.0f deg")) {
            changed = true;
        }
        if (ImGui::SliderInt("Variants", &entry.variants, c_foliage_min_variants, c_foliage_max_variants)) {
            entry.variants = std::clamp(entry.variants, c_foliage_min_variants, c_foliage_max_variants);
            changed = true;
        }
        ERHE_VERIFY(i < m_entry_rows.size()); // parse_palette() reads one per entry
        if (entry_parameter_rows(entry, m_entry_rows[i])) {
            changed = true;
        }
        if (shape != nullptr) {
            std::string chosen;
            const std::optional<std::size_t> slot = slot_material_combos(
                library, *shape,
                [&entry, shape, &library](const std::size_t slot_index) -> const std::string& {
                    if ((slot_index < entry.materials.size()) && !entry.materials[slot_index].empty()) {
                        return entry.materials[slot_index];
                    }
                    const Kit_slot_palette* const palette = library.find_slot_palette(shape->material_slots[slot_index]);
                    static const std::string no_material{};
                    return (palette != nullptr) ? palette->default_material : no_material;
                },
                chosen
            );
            if (slot.has_value()) {
                if (entry.materials.size() <= slot.value()) {
                    entry.materials.resize(slot.value() + 1);
                }
                entry.materials[slot.value()] = chosen;
                changed = true;
            }
        }
        ImGui::PopID();
    }
    if (remove_index.has_value()) {
        m_palette.erase(m_palette.begin() + static_cast<std::ptrdiff_t>(remove_index.value()));
        changed = true;
    } else if (move_up_index.has_value()) {
        // The order is the weighted pick's and the saved order.
        std::swap(m_palette[move_up_index.value() - 1], m_palette[move_up_index.value()]);
        changed = true;
    }
    ImGui::Separator();
    const bool add_open = ImGui::BeginCombo("Add Entry", "Choose a foliage shape");
    if (!add_open) {
        erhe::imgui::set_item_debug_label("Add Entry");
    }
    if (add_open) {
        for (const Kit_shape_definition* shape : library.get_foliage_shapes()) {
            if (!ImGui::Selectable(shape->display_name.c_str(), false)) {
                continue;
            }
            // F3.1: the shape's radius; 8 variants for a shape that derives values from a seed, else 1.
            Foliage_palette_entry entry{};
            entry.shape    = shape->id;
            entry.radius   = shape->foliage_radius.value_or(1.0f);
            entry.variants = c_foliage_max_variants;
            const Kit_lengths          lengths{};
            const Kit_parameter_values defaults = make_default_values(*shape, lengths.module, lengths.wall_height);
            entry.variants = (get_foliage_variant_count(entry, defaults) > 1) ? 8 : 1;
            m_palette.push_back(std::move(entry));
            changed = true;
        }
        ImGui::EndCombo();
    }
    if (changed) {
        const std::vector<Foliage_palette_entry> entries = m_palette;
        set_palette(entries);
    }
}

void Foliage_paint_tool::tool_properties(erhe::imgui::Imgui_window&)
{
    ERHE_PROFILE_FUNCTION();

    float radius = m_config.brush_radius;
    if (ImGui::SliderFloat("Brush Radius", &radius, c_foliage_min_brush_radius, c_foliage_max_brush_radius, "%.2f m", ImGuiSliderFlags_Logarithmic)) {
        set_brush_radius(radius);
    }
    float paint_density = m_config.paint_density;
    if (ImGui::SliderFloat("Paint Density", &paint_density, c_foliage_min_paint_density, c_foliage_max_paint_density, "%.2f / m2", ImGuiSliderFlags_Logarithmic)) {
        set_paint_density(paint_density);
    }
    float remove_density = m_config.remove_density;
    if (ImGui::SliderFloat("Remove Density", &remove_density, 0.0f, 1.0f, "%.2f")) {
        set_remove_density(remove_density);
    }
    bool align = m_config.align_to_surface;
    if (ImGui::Checkbox("Align To Surface", &align)) {
        set_up_axis(align ? Foliage_up::surface_normal : Foliage_up::world);
    }
    bool only_palette_shapes = m_config.remove_only_palette_shapes;
    if (ImGui::Checkbox("Remove Only Palette Shapes", &only_palette_shapes)) {
        set_remove_filter(only_palette_shapes ? Foliage_remove_filter::palette_shapes : Foliage_remove_filter::all_shapes);
    }
    int seed = m_config.seed;
    if (ImGui::InputInt("Seed", &seed)) {
        set_seed(seed);
    }

    ImGui::SeparatorText("Palette");
    Scene_view* const scene_view = get_last_hover_scene_view();
    palette_imgui((scene_view != nullptr) ? scene_view->get_scene_root() : std::shared_ptr<Scene_root>{});
    if (!m_paint_refusal.empty()) {
        ImGui::TextUnformatted("Painting is disabled:");
        kit_message_line(m_paint_refusal);
    }

    ImGui::SeparatorText("Last Stroke");
    if (m_last_stroke.outcome == Foliage_stroke_outcome::none) {
        ImGui::TextUnformatted("No stroke yet.");
    } else if (m_last_stroke.outcome == Foliage_stroke_outcome::refused) {
        ImGui::TextWrapped("Did not begin: %s", m_last_stroke.refusal.c_str());
    } else {
        const Foliage_stroke_result& result = m_last_stroke.result;
        ImGui::Text(
            "%s %s, seed %d: %zu stamps, %zu attempts",
            (m_last_stroke.kind == Foliage_stroke_kind::paint) ? "Paint" : "Remove", c_str(m_last_stroke.outcome), m_last_stroke.seed,
            result.stamps, result.attempts
        );
        ImGui::Text(
            "%zu placed, %zu removed, %zu refused by spacing, %zu without a surface, %zu inside an instance",
            result.placed, result.removed, result.refused_by_spacing, result.without_surface, result.on_sealed_support
        );
    }
}

} // namespace editor
