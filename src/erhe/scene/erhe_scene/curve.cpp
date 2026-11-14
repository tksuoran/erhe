#include "erhe_scene/curve.hpp"
#include "erhe_scene/scene_host.hpp"

namespace erhe::scene {

Curve::Curve()
{
    rebuild();
}

Curve::~Curve() noexcept = default;

Curve::Curve(const std::string_view name)
    : Item{name}
{
    rebuild();
}

// See Xform: the transform level's copy is not a clone.
Curve::Curve(const Curve& src, for_clone)
    : Item{src, erhe::for_clone{}}
    , m_path{src.m_path}
{
    rebuild();
}

void Curve::handle_transform_update(const uint64_t serial)
{
    Gprim::handle_transform_update(serial);
    notify_host(Curve_change::transform);
}

void Curve::set_path(const erhe::math::Curve_path& path)
{
    m_path = path;
    ++m_revision;
    rebuild();
    notify_host(Curve_change::shape);
}

auto Curve::get_path() const -> const erhe::math::Curve_path&
{
    return m_path;
}

auto Curve::get_table() const -> const erhe::math::Curve_path_table&
{
    return m_table;
}

auto Curve::get_draw_points() const -> std::span<const glm::vec3>
{
    return m_draw_points;
}

auto Curve::get_revision() const -> uint64_t
{
    return m_revision;
}

void Curve::rebuild()
{
    m_table.build(m_path);
    m_draw_points.clear();
    const std::size_t span_count = m_table.get_span_count();
    if (span_count == 0) {
        if (!m_path.points.empty()) {
            m_draw_points.push_back(m_path.points.front().position);
        }
        return;
    }
    m_draw_points.push_back(m_table.get_span(0).control[0]);
    for (std::size_t span = 0; span < span_count; ++span) {
        const erhe::math::Curve_span& curve_span = m_table.get_span(span);
        if (curve_span.shape == erhe::math::Curve_span_shape::line) {
            m_draw_points.push_back(curve_span.control[3]);
            continue;
        }
        for (std::size_t i = 1; i <= draw_points_per_cubic_span; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(draw_points_per_cubic_span);
            m_draw_points.push_back(erhe::math::get_span_position(curve_span, t));
        }
    }
}

void Curve::notify_host(const Curve_change change)
{
    Scene_host* const scene_host = static_cast<Scene_host*>(get_item_host());
    if (scene_host == nullptr) {
        return;
    }
    const std::shared_ptr<Curve> shared_this = std::static_pointer_cast<Curve>(weak_from_this().lock());
    if (shared_this) {
        scene_host->on_curve_changed(shared_this, change);
    }
}

auto make_default_curve_path() -> erhe::math::Curve_path
{
    erhe::math::Curve_path path{};
    path.closure = erhe::math::Curve_closure::open;
    path.points.push_back(
        erhe::math::Curve_point{
            .position   = glm::vec3{0.0f, 0.0f, 0.0f},
            .in_handle  = glm::vec3{-0.25f, 0.0f, 0.25f},
            .out_handle = glm::vec3{0.25f, 0.0f, -0.25f},
            .kind       = erhe::math::Curve_point_kind::smooth
        }
    );
    path.points.push_back(
        erhe::math::Curve_point{
            .position   = glm::vec3{1.0f, 0.0f, 0.0f},
            .in_handle  = glm::vec3{-0.25f, 0.0f, -0.25f},
            .out_handle = glm::vec3{0.25f, 0.0f, 0.25f},
            .kind       = erhe::math::Curve_point_kind::smooth
        }
    );
    return path;
}

auto c_str(const erhe::math::Curve_point_kind kind) -> const char*
{
    switch (kind) {
        case erhe::math::Curve_point_kind::corner: return "corner";
        case erhe::math::Curve_point_kind::smooth: return "smooth";
        case erhe::math::Curve_point_kind::free:   return "free";
        default:                                   return "?";
    }
}

auto curve_point_kind_from_string(const std::string_view text, erhe::math::Curve_point_kind& out_kind) -> bool
{
    if (text == "corner") { out_kind = erhe::math::Curve_point_kind::corner; return true; }
    if (text == "smooth") { out_kind = erhe::math::Curve_point_kind::smooth; return true; }
    if (text == "free")   { out_kind = erhe::math::Curve_point_kind::free;   return true; }
    return false;
}

} // namespace erhe::scene
