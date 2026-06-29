#include "erhe_scene/layout_item.hpp"

namespace erhe::scene {

Layout_item::Layout_item(const Layout_item&) = default;
Layout_item::~Layout_item() noexcept         = default;

Layout_item::Layout_item(const std::string_view name)
    : Item{name}
{
}

Layout_item::Layout_item(const Layout_item& src, erhe::for_clone)
    : Item          {src, erhe::for_clone{}}
    , alignment     {src.alignment     }
    , margin_min    {src.margin_min    }
    , margin_max    {src.margin_max    }
    , grid_cell_auto{src.grid_cell_auto}
    , grid_cell     {src.grid_cell     }
    , grid_span     {src.grid_span     }
{
}

} // namespace erhe::scene
