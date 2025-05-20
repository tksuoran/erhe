// Inspired by https://gist.github.com/galloscript/8a5d179e432e062550972afcd1ecf112

#pragma once

#include <functional>
#include "Gradient.hpp"
#include "HoverChecker.hpp"
#include "MarkId.hpp"
#include "Settings.hpp"

namespace ImGG {

/// A function that returns a random number between 0.f and 1.f whenever it is called.
using RandomNumberGenerator = std::function<float()>;

namespace internal {
struct draw_gradient_marks_Result {
    bool hitbox_is_hovered{false};
    bool selected_mark_changed{false};
};
} // namespace internal

class GradientWidget {
public:
    GradientWidget() = default;
    explicit GradientWidget(const std::list<Mark>& marks)
        : _gradient{marks}
    {}

    GradientWidget(const GradientWidget&);

    GradientWidget& operator=(const GradientWidget& widget)
    {
        *this = GradientWidget{widget}; // Construct and then move-assign
        return *this;
    }

    GradientWidget(GradientWidget&&) noexcept   = default;
    GradientWidget& operator=(GradientWidget&&) = default;

    friend auto operator==(const GradientWidget& a, const GradientWidget& b) -> bool { return a.gradient() == b.gradient(); }

    auto gradient() const -> const Gradient& { return _gradient; }
    auto gradient() -> Gradient& { return _gradient; }

    auto widget(
        const char*     label,
        const Settings& settings = {}
    ) -> bool;

    /// If `_should_use_a_random_color_for_the_new_marks` is true when adding a new mark,
    /// `rng` is used to generate the random color of the new mark. It can be any function that returns a number between 0.f and 1.f.
    /// There is an overload that doesn't need `rng` and uses a `std::default_random_engine` internally.
    auto widget(
        const char*           label,
        RandomNumberGenerator rng,
        const Settings&       settings = {}
    ) -> bool;

private:
    void add_mark_with_chosen_mode(RelativePosition relative_pos, RandomNumberGenerator rng, bool add_a_random_color);

    auto draw_gradient_marks(
        MarkId&         mark_to_delete,
        ImVec2          gradient_bar_pos,
        ImVec2          size,
        Settings const& settings
    ) -> internal::draw_gradient_marks_Result;

    auto mouse_dragging_interactions(
        ImVec2          gradient_bar_position,
        ImVec2          gradient_size,
        const Settings& settings
    ) -> bool;

private:
    Gradient _gradient{};
    MarkId   _selected_mark{};
    MarkId   _dragged_mark{};
    MarkId   _mark_to_hide{};

    internal::HoverChecker _hover_checker{};
};

} // namespace ImGG