#pragma once

#include <vector>

namespace erhe::graph {

class Pin;
class Link;

// One routing mid point of a link's wire. Presentation state, but stored on
// the link (the model) so every editor canvas shows the same wire shape and
// it serializes with the graph. Positions are canvas units; mode 0 = auto
// (tangents computed by the renderer), 1 = mirrored, 2 = aligned, 3 = free;
// tangents are offsets from the position (translation invariant).
class Link_mid_point
{
public:
    float position_x   {0.0f};
    float position_y   {0.0f};
    int   mode         {0};
    float tangent_in_x {0.0f};
    float tangent_in_y {0.0f};
    float tangent_out_x{0.0f};
    float tangent_out_y{0.0f};
};

[[nodiscard]] auto operator==(const Link_mid_point& lhs, const Link_mid_point& rhs) -> bool;

// Kochanek-Bartels style wire shape parameters, each in [-1, 1]; all zero is
// the standard routing. Stored on the link for the same reason as the mid
// points.
class Link_curve_params
{
public:
    float tension   {0.0f};
    float continuity{0.0f};
    float bias      {0.0f};
};

[[nodiscard]] auto operator==(const Link_curve_params& lhs, const Link_curve_params& rhs) -> bool;

class Link
{
public:
    Link();
    Link(Link&& old) noexcept;
    Link(const Link& other) = delete;
    Link& operator=(Link&& old) noexcept;
    Link& operator=(const Link& other) = delete;
    Link(Pin* source, Pin* sink);
    virtual ~Link() noexcept;

    [[nodiscard]] auto get_id      () const -> int;
    [[nodiscard]] auto get_source  () const -> Pin*;
    [[nodiscard]] auto get_sink    () const -> Pin*;
    [[nodiscard]] auto is_connected() const -> bool;
    void disconnect();

    // Wire routing (see Link_mid_point / Link_curve_params above).
    [[nodiscard]] auto get_mid_points  () const -> const std::vector<Link_mid_point>&;
    [[nodiscard]] auto get_curve_params() const -> const Link_curve_params&;
    [[nodiscard]] auto has_routing     () const -> bool;
    void set_mid_points  (std::vector<Link_mid_point> mid_points);
    void set_curve_params(const Link_curve_params& curve_params);

private:
    int  m_id;
    Pin* m_source{nullptr};
    Pin* m_sink  {nullptr};
    std::vector<Link_mid_point> m_mid_points;
    Link_curve_params           m_curve_params;
};

} // namespace erhe::graph
