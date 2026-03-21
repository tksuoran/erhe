#include "brushes/brush_placement.hpp"

#include <glm/glm.hpp>

namespace editor {

auto Brush_placement::get_brush() const -> std::shared_ptr<Brush>
{
    return m_brush;
}

auto Brush_placement::get_facet() const -> GEO::index_t
{
    return m_facet;
}

auto Brush_placement::get_corner() const -> GEO::index_t
{
    return m_corner;
}

void Brush_placement::set_corner(const GEO::index_t corner)
{
    m_corner = corner;
}

Brush_placement::Brush_placement(
    const std::shared_ptr<Brush>& brush,
    const GEO::index_t            facet,
    const GEO::index_t            corner
)
    : Item    {"brush placement"}
    , m_brush {brush}
    , m_facet {facet}
    , m_corner{corner}
{
}

Brush_placement::Brush_placement()
    : Item    {"brush placement"}
    , m_brush {}
    , m_facet {GEO::NO_INDEX}
    , m_corner{GEO::NO_INDEX}
{
}

Brush_placement::Brush_placement(const Brush_placement&) = default;
Brush_placement& Brush_placement::operator=(const Brush_placement&) = default;
Brush_placement::~Brush_placement() noexcept = default;

auto Brush_placement::clone() const -> std::shared_ptr<erhe::Item_base>
{
    // It doesn't make sense copy Brush_placement - does it?
    return std::shared_ptr<erhe::Item_base>{};
}

}
