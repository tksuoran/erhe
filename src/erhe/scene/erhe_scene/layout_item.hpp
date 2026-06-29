#pragma once

#include "erhe_scene/node_attachment.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

namespace erhe::scene {

// Alignment of a child within its layout cell, per axis.
//   negative : pin the child to the cell's minimum face
//   positive : pin the child to the cell's maximum face
//   stretch  : scale the child to fill the cell on this axis
enum class Layout_alignment : unsigned int {
    negative = 0,
    positive,
    stretch
};

// Per-child layout parameters. A child node without a Layout_item attachment
// is laid out using the default-constructed values below, so "no item" and
// "default item" behave identically.
class Layout_item : public erhe::Item<Item_base, Node_attachment, Layout_item, erhe::Item_kind::clone_using_custom_clone_constructor>
{
public:
    static constexpr const char* c_alignment_strings[] = {
        "Negative",
        "Positive",
        "Stretch"
    };

    Layout_item(const Layout_item&);
    Layout_item& operator=(const Layout_item&) = delete;
    ~Layout_item() noexcept override;

    explicit Layout_item(std::string_view name);
    Layout_item(const Layout_item& src, erhe::for_clone);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Layout_item"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t
    {
        return erhe::Item_type::node_attachment | erhe::Item_type::layout_item;
    }

    // Parameters (per local axis x = 0, y = 1, z = 2)
    std::array<Layout_alignment, 3> alignment{
        Layout_alignment::negative,
        Layout_alignment::negative,
        Layout_alignment::negative
    };
    glm::vec3  margin_min{0.0f, 0.0f, 0.0f}; // inset at the cell minimum face
    glm::vec3  margin_max{0.0f, 0.0f, 0.0f}; // inset at the cell maximum face
    bool       grid_cell_auto{true};         // grid: true = auto-placed into the next free cell; false = use grid_cell
    glm::ivec3 grid_cell {0, 0, 0};          // grid: explicit cell index (i, j, k); used when grid_cell_auto is false
    glm::ivec3 grid_span {1, 1, 1};          // grid: cells spanned per axis (>= 1); honored for auto placement too
};

} // namespace erhe::scene
