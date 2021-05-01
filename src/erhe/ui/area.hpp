#ifndef area_hpp_erhe_ui
#define area_hpp_erhe_ui

#include "erhe/ui/rectangle.hpp"
#include "erhe/ui/style.hpp"
#include <glm/glm.hpp>
#include <gsl/pointers>
#include <string>
#include <vector>

namespace erhe::ui
{

struct ui_context
{
    glm::vec2 mouse;
    bool      mouse_buttons[3]; // 0 left 1 right 2 middle
};

class Gui_renderer;

class Area
{
public:
    using Collection = std::vector<Area*>;

    enum class Orientation : unsigned int
    {
        horizontal,
        vertical
    };

    enum class Flow_mode : unsigned int
    {
        normal,
        extend_horizontal,
        extend_vertical
    };

    enum class Flow_direction : unsigned int
    {
        increasing,
        decreasing,
        none
    };

    enum class Order : unsigned int
    {
        self_first, // Draw self before children
        post_self,  // Draw self last, after children
        separate    // Separate DrawSelf() call
    };

    explicit Area(Gui_renderer& renderer) noexcept;

    Area(Gui_renderer&               renderer,
         gsl::not_null<const Style*> style) noexcept;

    Area(Gui_renderer&               renderer,
         gsl::not_null<const Style*> style,
         Flow_direction              layout_x_order,
         Flow_direction              layout_y_order) noexcept;

    virtual ~Area() = default;

    auto remove(gsl::not_null<Area*> area)
    -> gsl::not_null<Area*>;

    auto get_hit(glm::vec2 hit_position) noexcept
    -> Area*;

    void draw(ui_context& context) noexcept;

    // Do not make this virtual.
    // Derived classes should override begin_size() instead
    auto do_size(glm::vec2 free_size_reference) noexcept
    -> glm::vec2;

    // Do not make this virtual.
    // Derived classes should override begin_place() instead
    auto do_place(const Rectangle& reference_location, glm::vec2 grow_direction) noexcept
    -> glm::vec2;

    virtual auto add(gsl::not_null<Area*> area)
    -> gsl::not_null<Area*>;

    virtual void draw_self(ui_context& context) noexcept;

    // Layout
    virtual void begin_size(glm::vec2 free_size_reference) noexcept;

    virtual void call_size(gsl::not_null<Area*> area) noexcept;

    virtual void end_size() noexcept;

    virtual void begin_place(Rectangle reference, glm::vec2 container_grow_direction) noexcept;

    virtual void call_place(gsl::not_null<Area*> area) noexcept;

    virtual void end_place() noexcept;

    std::string    name;
    Gui_renderer&  renderer;
    Area*          parent{nullptr};
    Area*          link  {nullptr};
    gsl::not_null<const Style*> style;
    bool           hidden                   {false};
    bool           clip_to_reference        {true};
    glm::vec2      offset_pixels            {0.0f};
    glm::vec2      offset_self_size_relative{0.0f};
    glm::vec2      offset_free_size_relative{0.0f};
    glm::vec2      fill_base_pixels         {0.0f};
    glm::vec2      fill_free_size_relative  {0.0f};
    glm::vec2      size                     {0.0f};
    Rectangle      rect;
    Rectangle      in_rect;
    Order          draw_ordering {Order::self_first};
    Order          event_ordering{Order::post_self};
    Flow_mode      layout_style  {Flow_mode::normal};
    Flow_direction layout_x_order{Flow_direction::none};
    Flow_direction layout_y_order{Flow_direction::none};
    Collection     children;

protected:
    void update_in_rect()
    {
        in_rect = rect.shrink(style->padding);
    }

    inline void assert_size_valid()
    {
        assert(std::isfinite(size.x));
        assert(std::isfinite(size.y));
    }

private:
    bool       m_in_draw{false};
    Collection m_add_list;
    Collection m_remove_list;
};

} // namespace erhe::ui

#endif
