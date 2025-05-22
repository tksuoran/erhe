#include "Gradient.hpp"
#include "color_conversions.hpp"
#include "imgui_internal.hpp"

namespace ImGG {

void Gradient::sort_marks()
{
    _marks.sort([](const Mark& a, const Mark& b) { return a.position < b.position; });
}

auto Gradient::find(MarkId id) const -> const Mark*
{
    return id.find(*this);
}
auto Gradient::find(MarkId id) -> Mark*
{
    return id.find(*this);
}

auto Gradient::find_iterator(MarkId id) const -> std::list<Mark>::const_iterator
{
    return id.find_iterator(*this);
}

auto Gradient::find_iterator(MarkId id) -> std::list<Mark>::iterator
{
    return id.find_iterator(*this);
}

auto Gradient::is_empty() const -> bool
{
    return _marks.empty();
}

auto Gradient::add_mark(const Mark& mark) -> MarkId
{
    _marks.push_back(mark);
    const auto id = MarkId{_marks.back()};
    sort_marks();
    return id;
}

void Gradient::remove_mark(MarkId mark)
{
    const auto* const ptr = find(mark);
    if (ptr)
    {
        _marks.remove(*ptr);
    }
}

void Gradient::clear()
{
    _marks.clear();
}

void Gradient::set_mark_position(const MarkId mark, const RelativePosition position)
{
    auto* const ptr = find(mark);
    if (ptr)
    {
        ptr->position = position;
        sort_marks();
    }
}

void Gradient::set_mark_color(const MarkId mark, const ColorRGBA color)
{
    auto* const ptr = find(mark);
    if (ptr)
    {
        ptr->color = color;
    }
}

auto Gradient::interpolation_mode() const -> Interpolation
{
    return _interpolation_mode;
}

auto Gradient::interpolation_mode() -> Interpolation&
{
    return _interpolation_mode;
}

void Gradient::distribute_marks_evenly()
{
    if (_marks.empty())
        return;

    if (_marks.size() == 1)
    {
        _marks.begin()->position.set(0.5f);
        return;
    }

    const float f = 1.f / static_cast<float>(_marks.size() - (_interpolation_mode == Interpolation::Constant ? 0 : 1));

    float i = _interpolation_mode == Interpolation::Constant ? 1.f : 0.f;
    for (auto& mark : _marks)
    {
        mark.position.set(f * i);
        i += 1.f;
    }
}

auto Gradient::get_marks() const -> const std::list<Mark>&
{
    return _marks;
}

struct SurroundingMarks {
    SurroundingMarks(const Mark* lower, const Mark* upper) // We need to explicitly define the constructor in order to compile with MacOS Clang in C++ 11
        : lower{lower}
        , upper{upper}
    {}
    const Mark* lower{nullptr};
    const Mark* upper{nullptr};
};

/// Returns the marks positioned just before and after `position`, or nullptr if there is none.
static auto get_marks_surrounding(const RelativePosition position, const std::list<Mark>& marks) -> SurroundingMarks
{
    const Mark* lower{nullptr};
    const Mark* upper{nullptr};
    for (const Mark& mark : marks)
    {
        if (mark.position >= position && (!upper || mark.position < upper->position))
        {
            upper = &mark;
        }
        if (mark.position <= position && (!lower || mark.position > lower->position))
        {
            lower = &mark;
        }
    }
    return SurroundingMarks{lower, upper};
}

static auto interpolate(const Mark& lower, const Mark& upper, const RelativePosition position, Interpolation interpolation_mode) -> ColorRGBA
{
    switch (interpolation_mode)
    {
    case Interpolation::Linear:
    {
        const float mix_factor = (position.get() - lower.position.get())
                                 / (upper.position.get() - lower.position.get());
        // Do the interpolation in Lab space with premultiplied alpha because it looks much better.
        return internal::sRGB_Straight_from_Oklab_Premultiplied(ImLerp(
            internal::Oklab_Premultiplied_from_sRGB_Straight(lower.color),
            internal::Oklab_Premultiplied_from_sRGB_Straight(upper.color),
            mix_factor
        ));
    }

    case Interpolation::Constant:
    {
        return upper.color;
    }

    default:
        assert(false && "[ImGuiGradient::interpolate] Invalid enum value");
        return {-1.f, -1.f, -1.f, -1.f};
    }
}

auto Gradient::at(const RelativePosition position) const -> ColorRGBA
{
    const auto        surrounding_marks = get_marks_surrounding(position, _marks);
    const Mark* const lower{surrounding_marks.lower};
    const Mark* const upper{surrounding_marks.upper};

    if (!lower && !upper)
    {
        return ColorRGBA{0.f, 0.f, 0.f, 1.f};
    }
    else if (upper && !lower)
    {
        return upper->color;
    }
    else if (!upper && lower)
    {
        return lower->color;
    }
    else if (upper == lower)
    {
        return upper->color;
    }
    else
    {
        return interpolate(*lower, *upper, position, _interpolation_mode);
    }
}

} // namespace ImGG
