#include "erhe_scene/layout_system.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_scene/trs_transform.hpp"

#include "erhe_profile/profile.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>

namespace erhe::scene {

namespace {

[[nodiscard]] auto is_empty(const erhe::math::Aabb& aabb) -> bool
{
    return (aabb.min.x > aabb.max.x) ||
           (aabb.min.y > aabb.max.y) ||
           (aabb.min.z > aabb.max.z);
}

// The content box of a child, with an empty box collapsed to a point so every
// algorithm below works on a well-defined extent.
[[nodiscard]] auto measured_content(const Node& child) -> erhe::math::Aabb
{
    erhe::math::Aabb content = measure_child_content(child);
    if (is_empty(content)) {
        content.min = glm::vec3{0.0f, 0.0f, 0.0f};
        content.max = glm::vec3{0.0f, 0.0f, 0.0f};
    }
    return content;
}

// Map a child's content box (in child-local space) into a target cell (in
// layout-local space) honoring per-axis alignment and margins. The layout owns
// translation and scale only; rotation is set to identity so the result is a
// clean TRS (no shear from non-uniform stretch combined with rotation).
[[nodiscard]] auto compute_child_placement(
    const erhe::math::Aabb&                cell,
    const erhe::math::Aabb&                content,
    const std::array<Layout_alignment, 3>& alignment,
    const glm::vec3                        margin_min,
    const glm::vec3                        margin_max
) -> Trs_transform
{
    glm::vec3 translation{0.0f, 0.0f, 0.0f};
    glm::vec3 scale      {1.0f, 1.0f, 1.0f};

    for (int axis = 0; axis < 3; ++axis) {
        const float cell_lo = cell.min[axis];
        const float cell_hi = cell.max[axis];
        float       avail_lo = cell_lo + margin_min[axis];
        float       avail_hi = cell_hi - margin_max[axis];
        if (avail_hi < avail_lo) {
            const float mid = 0.5f * (cell_lo + cell_hi);
            avail_lo = mid;
            avail_hi = mid;
        }
        const float avail  = avail_hi - avail_lo;
        const float c_lo   = content.min[axis];
        const float c_hi   = content.max[axis];
        const float c_size = c_hi - c_lo;

        switch (alignment[axis]) {
            case Layout_alignment::stretch: {
                scale[axis]       = (c_size > 1.0e-6f) ? (avail / c_size) : 1.0f;
                translation[axis] = avail_lo - (scale[axis] * c_lo);
                break;
            }
            case Layout_alignment::positive: {
                scale[axis]       = 1.0f;
                translation[axis] = avail_hi - c_hi;
                break;
            }
            case Layout_alignment::negative:
            default: {
                scale[axis]       = 1.0f;
                translation[axis] = avail_lo - c_lo;
                break;
            }
        }
    }

    return Trs_transform{translation, glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, scale};
}

// Build the n+1 track boundary positions along one axis, from low to high,
// into a caller-owned buffer. When 'extents' has exactly 'count' entries they
// are honored as absolute track sizes accumulated from 'lo'; otherwise the
// range [lo, hi] is divided into 'count' equal tracks. The track count is
// clamped to at least 1.
void build_track_edges(
    const float               lo,
    const float               hi,
    const int                 count,
    const std::vector<float>& extents,
    std::vector<float>&       edges
)
{
    const int n = (count > 1) ? count : 1;
    edges.clear();
    edges.push_back(lo);
    if (extents.size() == static_cast<std::size_t>(n)) {
        float edge = lo;
        for (int k = 0; k < n; ++k) {
            edge += std::max(0.0f, extents[static_cast<std::size_t>(k)]); // a track cannot have negative size
            edges.push_back(edge);
        }
    } else {
        const float step = (hi - lo) / static_cast<float>(n);
        for (int k = 1; k <= n; ++k) {
            edges.push_back(lo + (step * static_cast<float>(k)));
        }
    }
}

// Per-child layout parameters read from the attached properties the child node
// carries (a child without local values uses the defaults). Shared by all
// layout algorithms.
class Resolved_item
{
public:
    std::array<Layout_alignment, 3> alignment{
        Layout_alignment::negative,
        Layout_alignment::negative,
        Layout_alignment::negative
    };
    glm::vec3  margin_min    {0.0f, 0.0f, 0.0f};
    glm::vec3  margin_max    {0.0f, 0.0f, 0.0f};
    bool       grid_cell_auto{true};
    glm::ivec3 grid_cell     {0, 0, 0};
    glm::ivec3 grid_span     {1, 1, 1};
};

[[nodiscard]] auto resolve_item(const Node& child) -> Resolved_item
{
    Resolved_item result;
    result.alignment[0]   = child.get_value(Layout::align_x_property);
    result.alignment[1]   = child.get_value(Layout::align_y_property);
    result.alignment[2]   = child.get_value(Layout::align_z_property);
    result.margin_min     = child.get_value(Layout::margin_min_property);
    result.margin_max     = child.get_value(Layout::margin_max_property);
    result.grid_cell_auto = child.get_value(Layout::grid_cell_auto_property);
    result.grid_cell      = child.get_value(Layout::grid_cell_property);
    result.grid_span      = child.get_value(Layout::grid_span_property);
    return result;
}

// Advance a signed cursor by 'size', returning the [lo, hi] interval consumed
// and moving the cursor to the far edge (in the axis-sign direction).
[[nodiscard]] auto advance(float& cursor, const float sign, const float size) -> std::pair<float, float>
{
    float lo = 0.0f;
    float hi = 0.0f;
    if (sign > 0.0f) {
        lo = cursor;
        hi = cursor + size;
        cursor = hi;
    } else {
        hi = cursor;
        lo = cursor - size;
        cursor = lo;
    }
    return std::pair<float, float>{lo, hi};
}

} // anonymous namespace

Layout_system::~Layout_system() noexcept = default;

void Layout_system::on_node_registered(Node& node)
{
    const std::optional<Layout_data> data = read_layout(node);
    if (!data.has_value()) {
        return;
    }
    m_records.insert_or_assign(&node, data.value());
}

void Layout_system::on_node_unregistered(Node& node)
{
    m_records.erase(&node);
}

void Layout_system::on_values_changed(Node& node, const erhe::property::Dependency_property&)
{
    // Every container value reaches here, so the record is the effective set
    // as of the last change and the per-frame solve reads it without touching
    // the property store.
    const std::optional<Layout_data> data = read_layout(node);
    if (!data.has_value()) {
        m_records.erase(&node);
        return;
    }
    m_records.insert_or_assign(&node, data.value());
}

void Layout_system::on_node_active_changed(Node&)
{
    // A layout arranges its children's transforms, which is authored
    // structure rather than something drawn: an inactive subtree keeps the
    // arrangement it has, so there is nothing to create or release here.
}

auto Layout_system::get_records() const -> const std::unordered_map<Node*, Layout_data>&
{
    return m_records;
}

auto Layout_system::find(const Node& node) const -> const Layout_data*
{
    const std::unordered_map<Node*, Layout_data>::const_iterator i = m_records.find(const_cast<Node*>(&node));
    return (i != m_records.end()) ? &i->second : nullptr;
}

void Layout_system::update()
{
    if (m_records.empty()) {
        return;
    }
    ERHE_PROFILE_FUNCTION();

    // Sort by hierarchy depth so a parent layout runs before any nested child
    // layout. Depth changes with reparenting, so the (small) list is sorted
    // every pass rather than cached.
    m_sorted.clear();
    for (const std::pair<Node* const, Layout_data>& record : m_records) {
        m_sorted.emplace_back(record.first->get_depth(), record.first);
    }
    std::stable_sort(
        m_sorted.begin(),
        m_sorted.end(),
        [](const std::pair<std::size_t, Node*>& lhs, const std::pair<std::size_t, Node*>& rhs) -> bool {
            return lhs.first < rhs.first;
        }
    );
    for (const std::pair<std::size_t, Node*>& entry : m_sorted) {
        const std::unordered_map<Node*, Layout_data>::const_iterator i = m_records.find(entry.second);
        if (i == m_records.end()) {
            continue;
        }
        apply(*entry.second, i->second);
    }
}

void Layout_system::apply(Node& layout_node, const Layout_data& data)
{
    switch (data.type) {
        case Layout_type::stack: {
            layout_stack(layout_node, data);
            break;
        }
        case Layout_type::grid: {
            layout_grid(layout_node, data);
            break;
        }
        case Layout_type::flow: {
            layout_flow(layout_node, data);
            break;
        }
        case Layout_type::none:
        default: {
            break;
        }
    }
}

void Layout_system::layout_stack(Node& layout_node, const Layout_data& data)
{
    const int   primary_axis = axis_index(data.primary);
    const float primary_sign = axis_sign(data.primary);
    const float primary_gap  = data.gap[primary_axis];

    float cursor = (primary_sign > 0.0f) ? data.volume.min[primary_axis] : data.volume.max[primary_axis];

    for (const std::shared_ptr<erhe::Hierarchy>& child_item : layout_node.get_children()) {
        const std::shared_ptr<Node> child = std::dynamic_pointer_cast<Node>(child_item);
        if (!child) {
            continue;
        }

        const erhe::math::Aabb content = measured_content(*child);
        const float extent = content.max[primary_axis] - content.min[primary_axis];

        // Cell keeps the full volume extent on the two cross axes; the primary
        // axis is the slice this child consumes (sized to the child's own extent).
        // A margin hint on the primary axis therefore shifts the child
        // within its slice but does not reserve extra space between neighbours;
        // inter-child spacing on the primary axis is controlled by 'gap'.
        erhe::math::Aabb cell = data.volume;
        if (primary_sign > 0.0f) {
            cell.min[primary_axis] = cursor;
            cell.max[primary_axis] = cursor + extent;
            cursor = cell.max[primary_axis] + primary_gap;
        } else {
            cell.max[primary_axis] = cursor;
            cell.min[primary_axis] = cursor - extent;
            cursor = cell.min[primary_axis] - primary_gap;
        }

        const Resolved_item params = resolve_item(*child);
        const Trs_transform placement = compute_child_placement(cell, content, params.alignment, params.margin_min, params.margin_max);
        child->set_parent_from_node(placement);
    }
}

void Layout_system::layout_grid(Node& layout_node, const Layout_data& data)
{
    for (int axis = 0; axis < 3; ++axis) {
        build_track_edges(
            data.volume.min[axis],
            data.volume.max[axis],
            data.grid_track_count[axis],
            data.grid_track_extent[static_cast<std::size_t>(axis)],
            m_track_edges[static_cast<std::size_t>(axis)]
        );
    }
    const std::vector<float>& edges_x = m_track_edges[0];
    const std::vector<float>& edges_y = m_track_edges[1];
    const std::vector<float>& edges_z = m_track_edges[2];
    const int track_count_x = static_cast<int>(edges_x.size()) - 1;
    const int track_count_y = static_cast<int>(edges_y.size()) - 1;
    const int track_count_z = static_cast<int>(edges_z.size()) - 1;
    const glm::ivec3 track_count{track_count_x, track_count_y, track_count_z};

    // Auto placement: children without an explicit cell flow into successive
    // cells in document order - primary axis fastest, wrapping into the
    // secondary axis, then into the tertiary axis (like the flow layout but on
    // cell indices). A negative axis direction walks that axis from the last
    // cell towards the first. Each auto child consumes its span along the
    // primary axis; a wrap advances by the largest secondary/tertiary span
    // seen in the completed row/sheet. Explicitly placed children do not move
    // the cursor and are not tracked for occupancy, so mixing explicit and
    // auto children can overlap.
    const int p_axis  = axis_index(data.primary);
    const int s_axis  = axis_index(data.secondary);
    const int t_axis  = axis_index(data.tertiary);
    glm::ivec3 logical_cursor{0, 0, 0}; // (primary, secondary, tertiary) positions, sign-independent
    int row_advance_s   = 1; // secondary advance when the current row wraps
    int sheet_advance_t = 1; // tertiary advance when the current sheet wraps

    for (const std::shared_ptr<erhe::Hierarchy>& child_item : layout_node.get_children()) {
        const std::shared_ptr<Node> child = std::dynamic_pointer_cast<Node>(child_item);
        if (!child) {
            continue;
        }

        const erhe::math::Aabb content = measured_content(*child);
        const Resolved_item   params   = resolve_item(*child);

        glm::ivec3 child_cell = params.grid_cell;
        if (params.grid_cell_auto) {
            const int span_p = std::clamp(params.grid_span[p_axis], 1, track_count[p_axis]);
            const int span_s = std::clamp(params.grid_span[s_axis], 1, track_count[s_axis]);
            const int span_t = std::clamp(params.grid_span[t_axis], 1, track_count[t_axis]);
            if ((logical_cursor[0] + span_p) > track_count[p_axis]) {
                logical_cursor[0]  = 0;
                logical_cursor[1] += row_advance_s;
                row_advance_s      = 1;
            }
            if ((logical_cursor[1] + span_s) > track_count[s_axis]) {
                logical_cursor[1]  = 0;
                logical_cursor[2] += sheet_advance_t;
                sheet_advance_t    = 1;
            }
            if ((logical_cursor[2] + span_t) > track_count[t_axis]) {
                logical_cursor[2] = 0; // grid full: wrap around (overlaps from the start)
            }
            // Convert sign-independent logical positions into cell indices:
            // a positive direction counts up from the first cell, a negative
            // direction counts down from the last cell (span occupies the
            // cells below the converted start index).
            const auto to_cell = [](const Axis_direction direction, const int logical, const int span, const int count) -> int {
                return (axis_sign(direction) > 0.0f) ? logical : (count - logical - span);
            };
            child_cell[p_axis] = to_cell(data.primary,   logical_cursor[0], span_p, track_count[p_axis]);
            child_cell[s_axis] = to_cell(data.secondary, logical_cursor[1], span_s, track_count[s_axis]);
            child_cell[t_axis] = to_cell(data.tertiary,  logical_cursor[2], span_t, track_count[t_axis]);
            row_advance_s      = std::max(row_advance_s,   span_s);
            sheet_advance_t    = std::max(sheet_advance_t, span_t);
            logical_cursor[0] += span_p;
        }

        // Clamp the cell index and span into the available track range.
        const int cx = std::clamp(child_cell.x, 0, track_count_x - 1);
        const int cy = std::clamp(child_cell.y, 0, track_count_y - 1);
        const int cz = std::clamp(child_cell.z, 0, track_count_z - 1);
        const int sx = std::clamp(params.grid_span.x, 1, track_count_x - cx);
        const int sy = std::clamp(params.grid_span.y, 1, track_count_y - cy);
        const int sz = std::clamp(params.grid_span.z, 1, track_count_z - cz);

        erhe::math::Aabb cell;
        cell.min = glm::vec3{
            edges_x[static_cast<std::size_t>(cx)],
            edges_y[static_cast<std::size_t>(cy)],
            edges_z[static_cast<std::size_t>(cz)]
        };
        cell.max = glm::vec3{
            edges_x[static_cast<std::size_t>(cx + sx)],
            edges_y[static_cast<std::size_t>(cy + sy)],
            edges_z[static_cast<std::size_t>(cz + sz)]
        };

        const Trs_transform placement = compute_child_placement(cell, content, params.alignment, params.margin_min, params.margin_max);
        child->set_parent_from_node(placement);
    }
}

void Layout_system::layout_flow(Node& layout_node, const Layout_data& data)
{
    // primary / secondary / tertiary should select three distinct axes. The cell
    // is seeded from the full volume so a misconfiguration (duplicate axis) leaves
    // the unset axis at full extent rather than producing an invalid cell.
    const int   primary_axis   = axis_index(data.primary);
    const int   secondary_axis = axis_index(data.secondary);
    const int   tertiary_axis  = axis_index(data.tertiary);
    const float primary_sign   = axis_sign(data.primary);
    const float secondary_sign = axis_sign(data.secondary);
    const float tertiary_sign  = axis_sign(data.tertiary);
    const float primary_gap    = data.gap[primary_axis];
    const float secondary_gap  = data.gap[secondary_axis];
    const float tertiary_gap   = data.gap[tertiary_axis];
    const float cap_primary    = data.volume.max[primary_axis]   - data.volume.min[primary_axis];
    const float cap_secondary  = data.volume.max[secondary_axis] - data.volume.min[secondary_axis];
    const float epsilon        = 1.0e-4f;

    // Collect direct Node children with their measured content boxes.
    m_flow_children.clear();
    m_flow_contents.clear();
    for (const std::shared_ptr<erhe::Hierarchy>& child_item : layout_node.get_children()) {
        const std::shared_ptr<Node> child = std::dynamic_pointer_cast<Node>(child_item);
        if (!child) {
            continue;
        }
        m_flow_children.push_back(child);
        m_flow_contents.push_back(measured_content(*child));
    }

    // Pass 1a: group children into lines along the primary axis (wrap on cap_primary).
    m_flow_lines.clear();
    m_flow_members.clear();
    Flow_line current_line;
    current_line.first_member = 0;
    for (std::size_t i = 0; i < m_flow_children.size(); ++i) {
        const float primary_len   = m_flow_contents[i].max[primary_axis]   - m_flow_contents[i].min[primary_axis];
        const float secondary_len = m_flow_contents[i].max[secondary_axis] - m_flow_contents[i].min[secondary_axis];
        const float tertiary_len  = m_flow_contents[i].max[tertiary_axis]  - m_flow_contents[i].min[tertiary_axis];
        float add_primary = ((current_line.member_count == 0) ? 0.0f : primary_gap) + primary_len;
        if ((current_line.member_count != 0) && ((current_line.used_p + add_primary) > (cap_primary + epsilon))) {
            m_flow_lines.push_back(current_line);
            current_line = Flow_line{};
            current_line.first_member = m_flow_members.size();
            add_primary = primary_len;
        }
        current_line.used_p  += add_primary;
        current_line.cross_s  = std::max(current_line.cross_s, secondary_len);
        current_line.cross_t  = std::max(current_line.cross_t, tertiary_len);
        m_flow_members.push_back(i);
        ++current_line.member_count;
    }
    if (current_line.member_count != 0) {
        m_flow_lines.push_back(current_line);
    }

    // Pass 1b: group lines into sheets along the secondary axis (wrap on cap_secondary).
    m_flow_sheets.clear();
    m_flow_sheet_lines.clear();
    Flow_sheet current_sheet;
    current_sheet.first_line = 0;
    for (std::size_t line_index = 0; line_index < m_flow_lines.size(); ++line_index) {
        const float line_cross_s = m_flow_lines[line_index].cross_s;
        float add_secondary = ((current_sheet.line_count == 0) ? 0.0f : secondary_gap) + line_cross_s;
        if ((current_sheet.line_count != 0) && ((current_sheet.used_s + add_secondary) > (cap_secondary + epsilon))) {
            m_flow_sheets.push_back(current_sheet);
            current_sheet = Flow_sheet{};
            current_sheet.first_line = m_flow_sheet_lines.size();
            add_secondary = line_cross_s;
        }
        current_sheet.used_s  += add_secondary;
        current_sheet.cross_t  = std::max(current_sheet.cross_t, m_flow_lines[line_index].cross_t);
        m_flow_sheet_lines.push_back(line_index);
        ++current_sheet.line_count;
    }
    if (current_sheet.line_count != 0) {
        m_flow_sheets.push_back(current_sheet);
    }

    // Pass 2: assign each child a cell and place it. Sheets stack along the
    // tertiary axis (no further wrapping; overflow past the volume is allowed).
    float tertiary_cursor = (tertiary_sign > 0.0f) ? data.volume.min[tertiary_axis] : data.volume.max[tertiary_axis];
    for (const Flow_sheet& sheet : m_flow_sheets) {
        const std::pair<float, float> tertiary_interval = advance(tertiary_cursor, tertiary_sign, sheet.cross_t);
        float secondary_cursor = (secondary_sign > 0.0f) ? data.volume.min[secondary_axis] : data.volume.max[secondary_axis];
        for (std::size_t s = 0; s < sheet.line_count; ++s) {
            const Flow_line& line = m_flow_lines[m_flow_sheet_lines[sheet.first_line + s]];
            const std::pair<float, float> secondary_interval = advance(secondary_cursor, secondary_sign, line.cross_s);
            float primary_cursor = (primary_sign > 0.0f) ? data.volume.min[primary_axis] : data.volume.max[primary_axis];
            for (std::size_t m = 0; m < line.member_count; ++m) {
                const std::size_t i = m_flow_members[line.first_member + m];
                const float primary_len = m_flow_contents[i].max[primary_axis] - m_flow_contents[i].min[primary_axis];
                const std::pair<float, float> primary_interval = advance(primary_cursor, primary_sign, primary_len);

                erhe::math::Aabb cell = data.volume;
                cell.min[primary_axis]   = primary_interval.first;   cell.max[primary_axis]   = primary_interval.second;
                cell.min[secondary_axis] = secondary_interval.first; cell.max[secondary_axis] = secondary_interval.second;
                cell.min[tertiary_axis]  = tertiary_interval.first;  cell.max[tertiary_axis]  = tertiary_interval.second;

                const Resolved_item params = resolve_item(*m_flow_children[i]);
                const Trs_transform placement = compute_child_placement(cell, m_flow_contents[i], params.alignment, params.margin_min, params.margin_max);
                m_flow_children[i]->set_parent_from_node(placement);

                primary_cursor += (primary_sign > 0.0f) ? primary_gap : -primary_gap;
            }
            secondary_cursor += (secondary_sign > 0.0f) ? secondary_gap : -secondary_gap;
        }
        tertiary_cursor += (tertiary_sign > 0.0f) ? tertiary_gap : -tertiary_gap;
    }
}

} // namespace erhe::scene
