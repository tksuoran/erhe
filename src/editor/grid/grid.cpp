#include "grid/grid.hpp"

#include "app_context.hpp"
#include "app_rendering.hpp"
#include "app_settings.hpp"
#include "editor_settings_store.hpp"
#include "config/generated/grid_config.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "items.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_settings_resolve.hpp"
#include "scene/scene_view.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_scene/node.hpp"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <fmt/format.h>

#include <glm/gtx/matrix_operation.hpp>

#include <algorithm>
#include <cmath>

namespace editor {

namespace {

using erhe::property::Dependency_object;
using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;

constexpr erhe::property::Enum_entry c_grid_plane_type_entries[] = {
    {"XZ-Plane Y+", static_cast<int32_t>(Grid_plane_type::XZ)},
    {"XY-Plane Z+", static_cast<int32_t>(Grid_plane_type::XY)},
    {"YZ-Plane X+", static_cast<int32_t>(Grid_plane_type::YZ)},
    {"Node",        static_cast<int32_t>(Grid_plane_type::Node)},
};

auto is_free_plane(const Dependency_object& object) -> bool
{
    return static_cast<const Grid&>(object).get_value(Grid::plane_type_property) != Grid_plane_type::Node;
}

auto slider(const float min, const float max, const std::string_view label, const std::string_view tooltip = {}, const bool logarithmic = false) -> Property_ui
{
    return Property_ui{.min = min, .max = max, .presentation = Property_ui::Presentation::slider, .logarithmic = logarithmic, .tooltip = tooltip, .label = label};
}

auto color(const std::string_view label, const std::string_view group = {}) -> Property_ui
{
    return Property_ui{.presentation = Property_ui::Presentation::color, .group = group, .label = label};
}

constexpr std::string_view c_lines  = "Lines";
constexpr std::string_view c_labels = "Axis Labels";

} // anonymous namespace

const erhe::property::Enum_info c_grid_plane_type_enum_info{"Grid_plane_type", c_grid_plane_type_entries};

// (default and ui are parenthesized: macro arguments split at the commas
// inside braces)
// Entry-stored, inheriting (section 4.11): the members are a mirror
// refreshed by Grid::on_property_changed.
#define ERHE_GRID_PROPERTY(Type, member_name, default_expr, ui_expr)     Property<Type>::register_property(#member_name, Grid::property_owner_type(), Property_metadata{.default_value = default_expr, .inherits = true, .ui = ui_expr})

const Property<Grid_plane_type> Grid::plane_type_property = Property<Grid_plane_type>::register_property(
    "plane_type", Grid::property_owner_type(), c_grid_plane_type_enum_info,
    Property_metadata{.default_value = erhe::property::make_value(Grid_plane_type::XZ), .inherits = true, .ui = Property_ui{.label = "Plane"}}
);
const Property<glm::vec3> Grid::center_property = Property<glm::vec3>::register_property(
    "center", Grid::property_owner_type(),
    Property_metadata{.default_value = glm::vec3{0.0f}, .inherits = true, .ui = Property_ui{.step = 0.01f, .tooltip = "Grid origin in world space", .label = "Offset", .visible_when = is_free_plane}}
);
const Property<float> Grid::rotation_property = Property<float>::register_property(
    "rotation", Grid::property_owner_type(),
    Property_metadata{.default_value = 0.0f, .inherits = true, .ui = Property_ui{.min = -180.0f, .max = 180.0f, .step = 0.05f, .tooltip = "Degrees around the plane normal", .label = "Rotation", .visible_when = is_free_plane}}
);
const Property<bool>      Grid::intersect_enable_property    = ERHE_GRID_PROPERTY(bool,      intersect_enable,    (true), (Property_ui{.tooltip = "The grid takes part in hover and placement ray tests", .label = "Intersect Enable"}));
const Property<bool>      Grid::snap_enabled_property        = ERHE_GRID_PROPERTY(bool,      snap_enabled,        (true), (Property_ui{.label = "Snap Enable"}));
const Property<float>     Grid::cell_size_property           = ERHE_GRID_PROPERTY(float,     cell_size,           (1.0f), (slider(0.01f, 10.0f, "Cell Size", {}, true)));
const Property<int>       Grid::cell_div_property            = ERHE_GRID_PROPERTY(int,       cell_div,            (2), (slider(1.0f, 10.0f, "Cell Div", "Minor cells per major cell")));
const Property<int>       Grid::cell_count_property          = ERHE_GRID_PROPERTY(int,       cell_count,          (100), (Property_ui{.min = 1.0f, .max = 10000.0f, .tooltip = "Cells per axis that bound the ray intersection (snap) region; the rendered grid is infinite", .label = "Cell Count"}));
const Property<glm::vec4> Grid::level0_color_property        = ERHE_GRID_PROPERTY(glm::vec4, level0_color, (glm::vec4{0.0f, 0.0f, 0.01f, 1.0f}), (color("Level 0 Color", c_lines)));
const Property<glm::vec4> Grid::level1_color_property        = ERHE_GRID_PROPERTY(glm::vec4, level1_color, (glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}), (color("Level 1 Color", c_lines)));
const Property<glm::vec4> Grid::level2_color_property        = ERHE_GRID_PROPERTY(glm::vec4, level2_color, (glm::vec4{0.01f, 0.0f, 0.0f, 1.0f}), (color("Level 2 Color", c_lines)));
const Property<glm::vec4> Grid::level3_color_property        = ERHE_GRID_PROPERTY(glm::vec4, level3_color, (glm::vec4{0.0f, 0.01f, 0.0f, 1.0f}), (color("Level 3 Color", c_lines)));
const Property<float>     Grid::level0_width_property        = ERHE_GRID_PROPERTY(float, level0_width, (0.006f), (Property_ui{.min = 0.0f, .max = 0.5f, .presentation = Property_ui::Presentation::slider, .logarithmic = true, .group = c_lines, .tooltip = "Line width as a fraction of the level cell size", .label = "Level 0 Width"}));
const Property<float>     Grid::level1_width_property        = ERHE_GRID_PROPERTY(float, level1_width, (0.02f), (Property_ui{.min = 0.0f, .max = 0.5f, .presentation = Property_ui::Presentation::slider, .logarithmic = true, .group = c_lines, .label = "Level 1 Width"}));
const Property<float>     Grid::level2_width_property        = ERHE_GRID_PROPERTY(float, level2_width, (0.02f), (Property_ui{.min = 0.0f, .max = 0.5f, .presentation = Property_ui::Presentation::slider, .logarithmic = true, .group = c_lines, .label = "Level 2 Width"}));
const Property<float>     Grid::level3_width_property        = ERHE_GRID_PROPERTY(float, level3_width, (0.02f), (Property_ui{.min = 0.0f, .max = 0.5f, .presentation = Property_ui::Presentation::slider, .logarithmic = true, .group = c_lines, .label = "Level 3 Width"}));
const Property<bool>      Grid::label_enable_property        = ERHE_GRID_PROPERTY(bool,      label_enable,        (true), (Property_ui{.group = c_labels, .tooltip = "Axis coordinate labels in the grid shader", .label = "Enable"}));
const Property<float>     Grid::label_text_fraction_property = ERHE_GRID_PROPERTY(float,     label_text_fraction, (0.15f), (Property_ui{.min = 0.05f, .max = 0.5f, .presentation = Property_ui::Presentation::slider, .group = c_labels, .tooltip = "Text height as a fraction of the label spacing", .label = "Size"}));
const Property<float>     Grid::label_spacing_property       = ERHE_GRID_PROPERTY(float,     label_spacing,       (1.0f), (Property_ui{.min = 1.0f, .max = 100.0f, .presentation = Property_ui::Presentation::slider, .group = c_labels, .tooltip = "World units between labels", .label = "Spacing"}));
const Property<float>     Grid::label_fade_property          = ERHE_GRID_PROPERTY(float,     label_fade,          (4.0f), (Property_ui{.min = 0.5f, .max = 24.0f, .presentation = Property_ui::Presentation::slider, .group = c_labels, .tooltip = "Glyph size in pixels at which labels are fully visible; smaller keeps labels visible further away", .label = "Fade"}));
const Property<glm::vec4> Grid::label_color_property         = ERHE_GRID_PROPERTY(glm::vec4, label_color,         (glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}), (color("Color", c_labels)));

#undef ERHE_GRID_PROPERTY

void Grid::on_property_changed(const erhe::property::Property_changed_args& args)
{
    if (!erhe::property::is_owner_type_or_descendant(Grid::property_owner_type(), args.property.get_owner_type())) {
        return;
    }
    refresh_mirror();
    const erhe::property::Dependency_property* const property = &args.property;
    if ((property == plane_type_property.get_ptr()) || (property == center_property.get_ptr()) || (property == rotation_property.get_ptr())) {
        update();
    }
    touch_settings(); // D19: every Grid property change schedules the settings autosave
}

void Grid::refresh_mirror()
{
    m_plane_type          = get_value(plane_type_property);
    m_center              = get_value(center_property);
    m_rotation            = get_value(rotation_property);
    m_intersect_enable    = get_value(intersect_enable_property);
    m_snap_enabled        = get_value(snap_enabled_property);
    m_cell_size           = get_value(cell_size_property);
    m_cell_div            = get_value(cell_div_property);
    m_cell_count          = get_value(cell_count_property);
    m_level_colors[0]     = get_value(level0_color_property);
    m_level_colors[1]     = get_value(level1_color_property);
    m_level_colors[2]     = get_value(level2_color_property);
    m_level_colors[3]     = get_value(level3_color_property);
    m_level_widths[0]     = get_value(level0_width_property);
    m_level_widths[1]     = get_value(level1_width_property);
    m_level_widths[2]     = get_value(level2_width_property);
    m_level_widths[3]     = get_value(level3_width_property);
    m_label_enable        = get_value(label_enable_property);
    m_label_text_fraction = get_value(label_text_fraction_property);
    m_label_spacing       = get_value(label_spacing_property);
    m_label_fade          = get_value(label_fade_property);
    m_label_color         = get_value(label_color_property);
}

void Grid::touch_settings()
{
    if (m_settings_store != nullptr) {
        m_settings_store->touch();
    }
}

void Grid::handle_flag_bits_update(const uint64_t old_flag_bits, const uint64_t new_flag_bits)
{
    if (((old_flag_bits ^ new_flag_bits) & erhe::Item_flags::visible) != 0) {
        touch_settings();
    }
}

Grid::Grid()
{
    update();
}

// Clone constructor for node duplication. Copies all grid data while the base
// (Item / Node_attachment for_clone) gives the clone a fresh id and leaves it
// detached (m_node reset). Keep this in sync with the data members below.
Grid::Grid(const Grid& src, erhe::for_clone)
    : Item                 {src, erhe::for_clone{}}
    , m_plane_type         {src.m_plane_type         }
    , m_intersect_enable   {src.m_intersect_enable   }
    , m_snap_enabled       {src.m_snap_enabled       }
    , m_rotation           {src.m_rotation           }
    , m_center             {src.m_center             }
    , m_cell_size          {src.m_cell_size          }
    , m_cell_div           {src.m_cell_div           }
    , m_cell_count         {src.m_cell_count         }
    , m_label_enable       {src.m_label_enable       }
    , m_label_text_fraction{src.m_label_text_fraction}
    , m_label_spacing      {src.m_label_spacing      }
    , m_label_fade         {src.m_label_fade         }
    , m_level_colors       {src.m_level_colors       }
    , m_level_widths       {src.m_level_widths       }
    , m_label_color        {src.m_label_color        }
    , m_world_from_grid    {src.m_world_from_grid    }
    , m_grid_from_world    {src.m_grid_from_world    }
{
}

void Grid::read_config(const Grid_config& config)
{
    // The config is the grid's authored state: local values, written
    // through the store so the mirror and every observer follow.
    set_visible(config.visible);
    const Dependency_object::Change_batch batch{*this};
    set_value(snap_enabled_property,        config.snap_enabled);
    set_value(cell_size_property,           config.cell_size);
    set_value(cell_div_property,            config.cell_div);
    set_value(cell_count_property,          config.cell_count);
    set_value(label_enable_property,        config.label_enable);
    set_value(label_text_fraction_property, config.label_text_fraction);
    set_value(label_spacing_property,       config.label_spacing);
    set_value(label_fade_property,          config.label_fade);
    set_value(label_color_property,         config.label_color);
    set_value(level0_color_property,        config.level0_color);
    set_value(level1_color_property,        config.level1_color);
    set_value(level2_color_property,        config.level2_color);
    set_value(level3_color_property,        config.level3_color);
    set_value(level0_width_property,        config.level0_width);
    set_value(level1_width_property,        config.level1_width);
    set_value(level2_width_property,        config.level2_width);
    set_value(level3_width_property,        config.level3_width);
}

void Grid::write_config(Grid_config& config) const
{
    config.visible             = is_visible();
    config.snap_enabled        = m_snap_enabled;
    config.cell_size           = m_cell_size;
    config.cell_div            = m_cell_div;
    config.cell_count          = m_cell_count;
    config.label_enable        = m_label_enable;
    config.label_text_fraction = m_label_text_fraction;
    config.label_spacing       = m_label_spacing;
    config.label_fade          = m_label_fade;
    config.label_color         = m_label_color;
    config.level0_color        = m_level_colors[0];
    config.level1_color        = m_level_colors[1];
    config.level2_color        = m_level_colors[2];
    config.level3_color        = m_level_colors[3];
    config.level0_width        = m_level_widths[0];
    config.level1_width        = m_level_widths[1];
    config.level2_width        = m_level_widths[2];
    config.level3_width        = m_level_widths[3];
}

auto Grid::snap_world_position(const glm::vec3& position_in_world) const -> glm::vec3
{
    if (!m_snap_enabled) {
        return position_in_world;
    }
    const float     snap_size        = m_cell_size / static_cast<float>(std::max(1, m_cell_div));
    const glm::vec3 position_in_grid = glm::vec3{grid_from_world() * glm::vec4{position_in_world, 1.0}};
    const glm::vec3 snapped_position_in_grid{
        std::floor((position_in_grid.x + snap_size * 0.5) / snap_size) * snap_size,
        std::floor((position_in_grid.y + snap_size * 0.5) / snap_size) * snap_size,
        std::floor((position_in_grid.z + snap_size * 0.5) / snap_size) * snap_size
    };

    return glm::vec3{
        world_from_grid() * glm::vec4{snapped_position_in_grid, 1.0}
    };
}

auto Grid::snap_grid_position(const glm::vec3& position_in_grid) const -> glm::vec3
{
    if (!m_snap_enabled) {
        return position_in_grid;
    }

    const float     snap_size        = m_cell_size / static_cast<float>(std::max(1, m_cell_div));
    const glm::vec3 snapped_position_in_grid{
        std::floor((position_in_grid.x + snap_size * 0.5f) / snap_size) * snap_size,
        std::floor((position_in_grid.y + snap_size * 0.5f) / snap_size) * snap_size,
        std::floor((position_in_grid.z + snap_size * 0.5f) / snap_size) * snap_size
    };

    return snapped_position_in_grid;
}

auto Grid::world_from_grid() const -> glm::mat4
{
    if (m_plane_type == Grid_plane_type::Node) {
        const erhe::scene::Node* node = get_node();
        if (node != nullptr) {
            return node->world_from_node();
        }
    }
    return m_world_from_grid;
}

auto Grid::grid_from_world() const -> glm::mat4
{
    if (m_plane_type == Grid_plane_type::Node) {
        const erhe::scene::Node* node = get_node();
        if (node != nullptr) {
            return node->node_from_world();
        }
    }
    return m_grid_from_world;
}

void Grid::render(const Render_context& context)
{
    // Grid appearance / sizing looks up the Grid_config effective for this
    // viewport's scene (#239: per-scene override, else the editor-global default).
    // Falls back to this grid's own cached members when no editor settings are
    // available. The grid's placement (transform / plane) stays per-grid.
    const App_context& app_context = context.app_context;
    const Grid_config* cfg = nullptr;
    if (app_context.editor_settings != nullptr) {
        const std::shared_ptr<Scene_root> scene_root = context.scene_view.get_scene_root();
        cfg = (scene_root != nullptr)
            ? &get_effective_grid(*app_context.editor_settings, *scene_root)
            : &app_context.editor_settings->grid;
    }

    // Graphics_settings::grid_visible is the session-only capture override
    // (MCP set_graphics_settings); it hides the grid without touching the
    // stored Grid_config.
    const bool  session_grid  = (app_context.app_settings == nullptr) || app_context.app_settings->graphics.grid_visible;
    const bool  visible       = session_grid && ((cfg != nullptr) ? cfg->visible : is_visible());
    const bool  label_enable  = (cfg != nullptr) ? cfg->label_enable        : m_label_enable;
    const float label_text_fr = (cfg != nullptr) ? cfg->label_text_fraction : m_label_text_fraction;
    const float label_spacing = (cfg != nullptr) ? cfg->label_spacing       : m_label_spacing;
    const float label_fade    = (cfg != nullptr) ? cfg->label_fade          : m_label_fade;
    const glm::vec4 label_color = (cfg != nullptr) ? cfg->label_color       : m_label_color;
    const std::array<glm::vec4, 4> level_colors = (cfg != nullptr)
        ? std::array<glm::vec4, 4>{cfg->level0_color, cfg->level1_color, cfg->level2_color, cfg->level3_color}
        : m_level_colors;
    const std::array<float, 4> level_widths = (cfg != nullptr)
        ? std::array<float, 4>{cfg->level0_width, cfg->level1_width, cfg->level2_width, cfg->level3_width}
        : m_level_widths;
    const int   cell_div  = (cfg != nullptr) ? cfg->cell_div  : m_cell_div;
    const float cell_size = (cfg != nullptr) ? cfg->cell_size : m_cell_size;

    // TODO Handling visibility here may add latency, but I guess that is okay.
    //      If Gr\id overrided handle_flag_bits_update(), it could handle visibility
    //      changes more directly, but it would then need access to App_rendering.
    context.app_context.app_rendering->set_grid_visibility(visible);
    context.app_context.app_rendering->set_grid_label(
        glm::vec4{
            label_enable ? 1.0f : 0.0f,
            label_text_fr,
            std::max(1.0f, std::round(label_spacing)), // shader formats integer values only
            label_fade
        }
    );
    context.app_context.app_rendering->set_grid_colors(level_colors, label_color);
    context.app_context.app_rendering->set_grid_line_widths(
        glm::vec4{level_widths[0], level_widths[1], level_widths[2], level_widths[3]}
    );

    // Derive the 4 grid LOD level cell sizes from cell size and cell div,
    // preserving the old CPU grid semantics: level1 = cell size (major
    // lines), level2 = cell size / cell div (minor lines = snap step),
    // extended geometrically to level0 (super-major) and level3
    // (sub-minor). Defaults (size 1, div 10) reproduce the historical
    // shader level sizes {10, 1, 0.1, 0.01}.
    const float div  = static_cast<float>(std::max(1, cell_div));
    const float size = std::max(0.001f, cell_size);
    context.app_context.app_rendering->set_grid_sizes(
        glm::vec4{size * div, size, size / div, size / (div * div)}
    );
}

void Grid::update()
{
    if (m_plane_type != Grid_plane_type::Node) {
        const float     radians      = glm::radians(m_rotation);
        const glm::mat4 orientation  = get_plane_transform(m_plane_type);
        const glm::vec3 plane_normal = glm::vec3{0.0f, 1.0f, 0.0f};
        const glm::mat4 offset       = erhe::math::create_translation<float>(m_center);
        const glm::mat4 rotation     = erhe::math::create_rotation<float>(radians, plane_normal);
        m_world_from_grid = orientation * rotation * offset;
        m_grid_from_world = glm::inverse(m_world_from_grid); // orientation * inverse_rotation * inverse_offset;
    }
}

auto Grid::imgui(App_context& context) -> bool
{
    bool changed = false;

    changed |= ImGui::InputText("Name", &m_name);

    if (m_plane_type == Grid_plane_type::Node) {
        {
            erhe::scene::Node* host_node = get_node();
            if (host_node != nullptr) {
                const std::string label        = fmt::format("Node: {}", host_node->get_name());
                const std::string detach_label = fmt::format("Detach from {}", host_node->get_name());
                ImGui::TextUnformatted(label.c_str());
                if (ImGui::Button(detach_label.c_str())) {
                    host_node->detach(this);
                    changed = true;
                }
            }
        }
        const auto& host_node = get<erhe::scene::Node>(context.selection->get_selected_items());
        if (host_node) {
            const std::string label = fmt::format("Attach to {}", host_node->get_name());
            if (ImGui::Button(label.c_str())) {
                host_node->attach(
                    std::static_pointer_cast<Grid>(shared_from_this())
                );
                changed = true;
            }
        }
    }

    return changed;
}

auto Grid::normal_in_world() const -> glm::vec3
{
    const glm::mat4 world_from_grid_       = world_from_grid();
    const glm::mat4 normal_world_from_grid = glm::transpose(glm::adjugate(world_from_grid_));
    const glm::vec3 normal_in_world        = glm::vec3{normal_world_from_grid * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}};
    const glm::vec3 unit_normal_in_world   = glm::normalize(normal_in_world);
    return unit_normal_in_world;
}

auto Grid::tangent_in_world() const -> glm::vec3
{
    const glm::mat4 world_from_grid_      = world_from_grid();
    const glm::vec3 tangent_in_world      = glm::vec3{world_from_grid_ * glm::vec4{0.0f, 0.0f, 1.0f, 0.0f}};
    const glm::vec3 unit_tangent_in_world = glm::normalize(tangent_in_world);
    return unit_tangent_in_world;
}

auto Grid::bitangent_in_world() const -> glm::vec3
{
    const glm::mat4 world_from_grid_        = world_from_grid();
    const glm::vec3 bitangent_in_world      = glm::vec3{world_from_grid_ * glm::vec4{1.0f, 0.0f, 0.0f, 0.0f}};
    const glm::vec3 unit_bitangent_in_world = glm::normalize(bitangent_in_world);
    return unit_bitangent_in_world;
}

auto Grid::get_cell_size() const -> float
{
    return m_cell_size;
}

auto Grid::intersect_ray(const glm::vec3& ray_origin_in_world, const glm::vec3& ray_direction_in_world) -> std::optional<glm::vec3>
{
    if (!m_intersect_enable) {
        return {};
    }

    const glm::vec3 ray_origin_in_grid    = glm::vec3{grid_from_world() * glm::vec4{ray_origin_in_world,    1.0f}};
    const glm::vec3 ray_direction_in_grid = glm::vec3{grid_from_world() * glm::vec4{ray_direction_in_world, 0.0f}};
    const auto intersection = erhe::math::intersect_plane<float>(
        glm::vec3{0.0f, 1.0f, 0.0f},
        glm::vec3{0.0f, 0.0f, 0.0f},
        ray_origin_in_grid,
        ray_direction_in_grid
    );
    if (!intersection.has_value() || intersection.value() < 0.0f) {
        return {};
    }
    const glm::vec3 position_in_grid = ray_origin_in_grid + intersection.value() * ray_direction_in_grid;

    if (
        (position_in_grid.x < -m_cell_size * static_cast<float>(m_cell_count)) ||
        (position_in_grid.x >  m_cell_size * static_cast<float>(m_cell_count)) ||
        (position_in_grid.z < -m_cell_size * static_cast<float>(m_cell_count)) ||
        (position_in_grid.z >  m_cell_size * static_cast<float>(m_cell_count))
    ) {
        return {};
    }

    return glm::vec3{
        world_from_grid() * glm::vec4{position_in_grid, 1.0}
    };
}

}
