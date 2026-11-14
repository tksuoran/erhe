#pragma once

#include "erhe_scene/gprim.hpp"

#include "erhe_math/curve_path.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace erhe::scene {

// What changed when a curve's users are told (Scene_host::on_curve_changed).
enum class Curve_change : unsigned int
{
    shape = 0, // the path: points, handles, kinds or closure
    transform  // the prim's world transform
};

// A curve prim (doc/editor/curves.md, USD `UsdGeomBasisCurves` level): one
// erhe::math::Curve_path in the prim's own frame, with its baked table.
class Curve
    : public erhe::Item<
        Item_base,
        Gprim,
        Curve,
        erhe::Item_kind::clone_using_custom_clone_constructor
    >
{
public:
    Curve();
    Curve(const Curve&) = delete;
    Curve& operator=(const Curve&) = delete;
    explicit Curve(std::string_view name);
    Curve(const Curve& src, for_clone);
    ~Curve() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Curve"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t
    {
        return Gprim::get_static_type() | erhe::Item_type::curve;
    }

    // Overrides Typed: the class fixes the token.
    [[nodiscard]] auto get_class_type_name() const -> std::string_view override { return "Curve"; }

    // Overrides Xformable: a move of the prim is a change of the curve in world space.
    void handle_transform_update(uint64_t serial) override;

    // Replaces the path, rebuilds the table and the draw points, and tells the scene host.
    void set_path(const erhe::math::Curve_path& path);

    [[nodiscard]] auto get_path       () const -> const erhe::math::Curve_path&;
    [[nodiscard]] auto get_table      () const -> const erhe::math::Curve_path_table&;
    [[nodiscard]] auto get_draw_points() const -> std::span<const glm::vec3>; // polyline in the prim's frame
    [[nodiscard]] auto get_revision   () const -> uint64_t;                     // counts set_path() calls

    static constexpr std::size_t draw_points_per_cubic_span = 24;

private:
    void rebuild();
    void notify_host(Curve_change change);

    erhe::math::Curve_path       m_path;
    erhe::math::Curve_path_table m_table;
    std::vector<glm::vec3>       m_draw_points;
    uint64_t                     m_revision{0};
};

// The path a new curve gets: two smooth points one unit apart along +X, arcing towards -Z.
[[nodiscard]] auto make_default_curve_path() -> erhe::math::Curve_path;

[[nodiscard]] auto c_str(erhe::math::Curve_point_kind kind) -> const char*;
[[nodiscard]] auto curve_point_kind_from_string(std::string_view text, erhe::math::Curve_point_kind& out_kind) -> bool;

} // namespace erhe::scene
