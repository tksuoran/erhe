#include "scene/brush_placement.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_bit/bit_helpers.hpp"

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

auto Brush_placement::get_static_type() -> uint64_t
{
    return erhe::Item_type::node_attachment | erhe::Item_type::brush_placement;
}

auto Brush_placement::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Brush_placement::get_type_name() const -> std::string_view
{
    return static_type_name;
}

auto is_brush_placement(const erhe::Item_base* const item) -> bool
{
    if (item == nullptr) {
        return false;
    }
    return erhe::bit::test_all_rhs_bits_set(item->get_type(), erhe::Item_type::brush_placement);
}

auto is_brush_placement(const std::shared_ptr<erhe::Item_base>& item) -> bool
{
    return is_brush_placement(item.get());
}

auto as_brush_placement(erhe::Item_base* item) -> Brush_placement*
{
    if (item == nullptr) {
        return nullptr;
    }
    if (!erhe::bit::test_all_rhs_bits_set(item->get_type(), erhe::Item_type::frame_controller)) {
        return nullptr;
    }
    return static_cast<Brush_placement*>(item);
}

auto get_brush_placement(const erhe::scene::Node* node) -> std::shared_ptr<Brush_placement>
{
    for (const auto& attachment : node->get_attachments()) {
        auto frame_controller = std::dynamic_pointer_cast<Brush_placement>(attachment);
        if (frame_controller) {
            return frame_controller;
        }
    }
    return {};
}

}
