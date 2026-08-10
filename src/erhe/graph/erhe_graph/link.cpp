#include "erhe_graph/graph.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"

namespace erhe::graph {

Link::Link()
    : m_id{make_graph_id()}
{
}

Link::Link(Link&& old) noexcept = default;

Link& Link::operator=(Link&& old) noexcept = default;

Link::Link(Pin* source, Pin* sink)
    : m_id    {make_graph_id()}
    , m_source{source}
    , m_sink  {sink}
{
}

Link::~Link() noexcept = default;

auto Link::get_id() const -> int
{
    return m_id;
}

auto Link::get_source() const -> Pin* {
    return m_source;
}

auto Link::get_sink() const -> Pin*
{
    return m_sink;
}

auto Link::is_connected() const -> bool
{
    return (m_source != nullptr) && (m_sink != nullptr);
}

void Link::disconnect()
{
    m_source->remove_link(this);
    m_sink  ->remove_link(this);
    m_source = nullptr;
    m_sink   = nullptr;
}

auto operator==(const Link_mid_point& lhs, const Link_mid_point& rhs) -> bool
{
    return
        (lhs.position_x    == rhs.position_x   ) &&
        (lhs.position_y    == rhs.position_y   ) &&
        (lhs.mode          == rhs.mode         ) &&
        (lhs.tangent_in_x  == rhs.tangent_in_x ) &&
        (lhs.tangent_in_y  == rhs.tangent_in_y ) &&
        (lhs.tangent_out_x == rhs.tangent_out_x) &&
        (lhs.tangent_out_y == rhs.tangent_out_y);
}

auto operator==(const Link_curve_params& lhs, const Link_curve_params& rhs) -> bool
{
    return
        (lhs.tension    == rhs.tension   ) &&
        (lhs.continuity == rhs.continuity) &&
        (lhs.bias       == rhs.bias      );
}

auto Link::get_mid_points() const -> const std::vector<Link_mid_point>&
{
    return m_mid_points;
}

auto Link::get_curve_params() const -> const Link_curve_params&
{
    return m_curve_params;
}

auto Link::has_routing() const -> bool
{
    return
        !m_mid_points.empty() ||
        (m_curve_params.tension    != 0.0f) ||
        (m_curve_params.continuity != 0.0f) ||
        (m_curve_params.bias       != 0.0f);
}

void Link::set_mid_points(std::vector<Link_mid_point> mid_points)
{
    m_mid_points = std::move(mid_points);
}

void Link::set_curve_params(const Link_curve_params& curve_params)
{
    m_curve_params = curve_params;
}

} // namespace erhe::graph