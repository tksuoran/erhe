#include "erhe_scene/layout.hpp"
#include "erhe_scene/layout_item.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_primitive/primitive.hpp"

#include <glm/gtc/quaternion.hpp>

#include <array>
#include <memory>

namespace erhe::scene {

auto axis_index(const Axis_direction direction) -> int
{
    switch (direction) {
        case Axis_direction::pos_x:
        case Axis_direction::neg_x: return 0;
        case Axis_direction::pos_y:
        case Axis_direction::neg_y: return 1;
        case Axis_direction::pos_z:
        case Axis_direction::neg_z: return 2;
        default:                    return 0;
    }
}

auto axis_sign(const Axis_direction direction) -> float
{
    switch (direction) {
        case Axis_direction::pos_x:
        case Axis_direction::pos_y:
        case Axis_direction::pos_z: return  1.0f;
        case Axis_direction::neg_x:
        case Axis_direction::neg_y:
        case Axis_direction::neg_z: return -1.0f;
        default:                    return  1.0f;
    }
}

auto axis_vector(const Axis_direction direction) -> glm::vec3
{
    glm::vec3 result{0.0f, 0.0f, 0.0f};
    result[axis_index(direction)] = axis_sign(direction);
    return result;
}

namespace {

// An Aabb is empty (carries no extent) when its minimum exceeds its maximum on
// any axis. The default-constructed Aabb (min = FLT_MAX, max = -FLT_MAX) is empty.
// Note: erhe::math::Aabb::is_valid() uses '||' and is unreliable here, so this
// feature uses its own explicit emptiness test.
[[nodiscard]] auto is_empty(const erhe::math::Aabb& aabb) -> bool
{
    return (aabb.min.x > aabb.max.x) ||
           (aabb.min.y > aabb.max.y) ||
           (aabb.min.z > aabb.max.z);
}

[[nodiscard]] auto node_own_local_aabb(const Node& node) -> erhe::math::Aabb
{
    erhe::math::Aabb aabb{};
    const std::shared_ptr<Mesh> mesh = get_attachment<Mesh>(&node);
    if (mesh) {
        for (const Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
            if (!mesh_primitive.primitive) {
                continue;
            }
            const erhe::math::Aabb local = mesh_primitive.primitive->get_bounding_box();
            if (!is_empty(local)) {
                aabb.include(local);
            }
        }
    }
    return aabb;
}

// Map a child's content box (in child-local space) into a target cell (in
// layout-local space) honoring per-axis alignment and margins. The layout owns
// translation and scale only; rotation is set to identity so the result is a
// clean TRS (no shear from non-uniform stretch combined with rotation).
[[nodiscard]] auto compute_child_placement(
    const erhe::math::Aabb&                cell,
    const erhe::math::Aabb&                content,
    const std::array<Layout_alignment, 3>& alignment,
    const glm::vec3                         margin_min,
    const glm::vec3                         margin_max
) -> Trs_transform
{
    erhe::math::Aabb c = content;
    if (is_empty(c)) {
        c.min = glm::vec3{0.0f, 0.0f, 0.0f};
        c.max = glm::vec3{0.0f, 0.0f, 0.0f};
    }

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
        const float c_lo   = c.min[axis];
        const float c_hi   = c.max[axis];
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

// Measure a direct child's content box in the child's own local space. A child
// that is itself a layout contributes its declared volume rather than its
// geometry (matches intent and breaks the recursion cycle).
[[nodiscard]] auto measure_child_content(const Node& child) -> erhe::math::Aabb
{
    const std::shared_ptr<Layout> child_layout = get_attachment<Layout>(&child);
    if (child_layout) {
        return child_layout->volume;
    }
    return compute_content_local_aabb(child);
}

} // anonymous namespace

auto compute_content_local_aabb(const Node& node) -> erhe::math::Aabb
{
    erhe::math::Aabb aabb = node_own_local_aabb(node);

    for (const std::shared_ptr<erhe::Hierarchy>& child_item : node.get_children()) {
        const std::shared_ptr<Node> child = std::dynamic_pointer_cast<Node>(child_item);
        if (!child) {
            continue;
        }
        const erhe::math::Aabb child_box = measure_child_content(*child);
        if (!is_empty(child_box)) {
            const glm::mat4 child_from_node = child->parent_from_node(); // child-local -> node-local
            aabb.include(child_box.transformed_by(child_from_node));
        }
    }
    return aabb;
}

Layout::Layout(const Layout&) = default;
Layout::~Layout() noexcept    = default;

Layout::Layout(const std::string_view name)
    : Item{name}
{
}

Layout::Layout(const Layout& src, erhe::for_clone)
    : Item             {src, erhe::for_clone{}}
    , type             {src.type             }
    , volume           {src.volume           }
    , primary          {src.primary          }
    , secondary        {src.secondary        }
    , tertiary         {src.tertiary         }
    , gap              {src.gap              }
    , grid_track_count {src.grid_track_count }
    , grid_track_extent{src.grid_track_extent}
{
}

auto Layout::clone_attachment() const -> std::shared_ptr<Node_attachment>
{
    return std::make_shared<Layout>(*this, erhe::for_clone{});
}

void Layout::update()
{
    Node* const layout_node = get_node();
    if (layout_node == nullptr) {
        return;
    }

    switch (type) {
        case Type::stack: {
            layout_stack(*layout_node);
            break;
        }
        case Type::grid: {
            // TODO Step 2: layout_grid(*layout_node);
            break;
        }
        case Type::flow: {
            // TODO Step 3: layout_flow(*layout_node);
            break;
        }
        default: {
            break;
        }
    }
}

void Layout::layout_stack(Node& layout_node)
{
    const int   primary_axis = axis_index(primary);
    const float primary_sign = axis_sign(primary);
    const float primary_gap  = gap[primary_axis];

    float cursor = (primary_sign > 0.0f) ? volume.min[primary_axis] : volume.max[primary_axis];

    for (const std::shared_ptr<erhe::Hierarchy>& child_item : layout_node.get_children()) {
        const std::shared_ptr<Node> child = std::dynamic_pointer_cast<Node>(child_item);
        if (!child) {
            continue;
        }

        erhe::math::Aabb content = measure_child_content(*child);
        if (is_empty(content)) {
            content.min = glm::vec3{0.0f, 0.0f, 0.0f};
            content.max = glm::vec3{0.0f, 0.0f, 0.0f};
        }
        const float extent = content.max[primary_axis] - content.min[primary_axis];

        // Cell keeps the full volume extent on the two cross axes; the primary
        // axis is the slice this child consumes (sized to the child's own extent).
        // A Layout_item margin on the primary axis therefore shifts the child
        // within its slice but does not reserve extra space between neighbours;
        // inter-child spacing on the primary axis is controlled by 'gap'.
        erhe::math::Aabb cell = volume;
        if (primary_sign > 0.0f) {
            cell.min[primary_axis] = cursor;
            cell.max[primary_axis] = cursor + extent;
            cursor = cell.max[primary_axis] + primary_gap;
        } else {
            cell.max[primary_axis] = cursor;
            cell.min[primary_axis] = cursor - extent;
            cursor = cell.min[primary_axis] - primary_gap;
        }

        // Resolve per-child parameters (defaults when no Layout_item is present).
        std::array<Layout_alignment, 3> alignment{
            Layout_alignment::negative,
            Layout_alignment::negative,
            Layout_alignment::negative
        };
        glm::vec3 margin_min{0.0f, 0.0f, 0.0f};
        glm::vec3 margin_max{0.0f, 0.0f, 0.0f};
        const std::shared_ptr<Layout_item> item = get_attachment<Layout_item>(child.get());
        if (item) {
            alignment  = item->alignment;
            margin_min = item->margin_min;
            margin_max = item->margin_max;
        }

        const Trs_transform placement = compute_child_placement(cell, content, alignment, margin_min, margin_max);
        child->set_parent_from_node(placement);
    }
}

} // namespace erhe::scene
