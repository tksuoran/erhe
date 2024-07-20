#pragma once

#include "erhe_geometry/types.hpp"
#include "erhe_scene/node_attachment.hpp"

#include <memory>

namespace erhe::scene {
    class Node;
}

namespace editor {

class Brush;

class Brush_placement : public erhe::Item<erhe::Item_base, erhe::scene::Node_attachment, Brush_placement, erhe::Item_kind::not_clonable>{
public:
    Brush_placement(const std::shared_ptr<Brush>& brush, erhe::geometry::Polygon_id polygon, erhe::geometry::Corner_id corner);
    Brush_placement(const Brush_placement&);
    Brush_placement& operator=(const Brush_placement&);
    ~Brush_placement() noexcept override;

    Brush_placement();

    static constexpr std::string_view static_type_name{"Brush_placement"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;

    // TODO Consider if Brush_placement is clonable or not
    auto clone() const -> std::shared_ptr<erhe::Item_base> override;

    // Implements Node_attachment
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

    // Public API
    [[nodiscard]] auto get_brush  () const -> std::shared_ptr<Brush>;
    [[nodiscard]] auto get_polygon() const -> erhe::geometry::Polygon_id;
    [[nodiscard]] auto get_corner () const -> erhe::geometry::Corner_id;

private:
    std::shared_ptr<Brush>     m_brush;
    erhe::geometry::Polygon_id m_polygon;
    erhe::geometry::Corner_id  m_corner;
};

auto is_brush_placement(const erhe::Item_base* item) -> bool;
auto is_brush_placement(const std::shared_ptr<erhe::Item_base>& item) -> bool;

auto get_brush_placement(const erhe::scene::Node* node) -> std::shared_ptr<Brush_placement>;

} // namespace editor
