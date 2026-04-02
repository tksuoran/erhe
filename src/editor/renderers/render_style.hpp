#pragma once

#include "config/generated/render_style_data.hpp"
#include "erhe_renderer/enums.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"

#include "erhe_primitive/enums.hpp"
#include "erhe_item/item.hpp"

#include <string_view>

auto is_primitive_mode_enabled(const Render_style_data& style, erhe::primitive::Primitive_mode primitive_mode) -> bool;
auto get_primitive_settings   (const Render_style_data& style, erhe::primitive::Primitive_mode primitive_mode) -> erhe::scene_renderer::Primitive_interface_settings;

namespace editor {

class Render_style : public erhe::Item<erhe::Item_base, erhe::Item_base, Render_style, erhe::Item_kind::not_clonable>
{
public:
    Render_style();
    ~Render_style() noexcept override;

    explicit Render_style(std::string_view name);

    static constexpr std::string_view static_type_name{"Render_style"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::node; }

    Render_style_data data;
};

}

// bool  polygon_offset_enable{false};
// float polygon_offset_factor{1.0000f};
// float polygon_offset_units {1.0000f};
// float polygon_offset_clamp {0.0001f};
