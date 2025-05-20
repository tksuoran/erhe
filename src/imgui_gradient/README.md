# imgui_gradient

[Dear ImGui](https://github.com/ocornut/imgui) extension that adds a *gradient* widget.

![Animation](https://user-images.githubusercontent.com/45451201/218996538-5f5f80d2-3cbe-4f5c-8c26-a107970bd97b.gif)

## Table of Contents

- [Compatibility](#compatibility)
- [Basic usage](#basic-usage)
- [Tutorial](#tutorial)
  - [Including](#including)
  - [Sampling the gradient](#sampling-the-gradient)
  - [Interpolation](#interpolation)
  - [Settings](#settings)
  - [Color randomization](#color-randomization)
- [For maintainers](#for-maintainers)
  - [Running the tests](#running-the-tests)


## Compatibility

This library is tested and compiles with C++11 on:
- [x] Windows
    - [x] Clang
    - [x] MSVC
- [x] Linux
    - [x] Clang
    - [x] GCC
- [x] MacOS
    - [x] Clang

## Basic usage

The whole state of the widget is stored in a `ImGG::GradientWidget` variable. You need to create and store one in order to have access to a gradient. You can then use the `widget()` method to render the ImGui widget:
```cpp
ImGG::GradientWidget gradient_widget{};
gradient_widget.widget("My Gradient");
```

In order to access the gradient data (without the GUI state), you can get the `ImGG::Gradient` with `gradient_widget.gradient()`.

## Tutorial

### Including

To add this library to your project, simply add these three lines to your *CMakeLists.txt* and replace `folder/containing/imgui` with the path to the folder containing the *imgui* headers:
```cmake
add_subdirectory(path/to/imgui_gradient)
target_include_directories(imgui_gradient SYSTEM PRIVATE folder/containing/imgui)
target_link_libraries(${PROJECT_NAME} PRIVATE imgui_gradient::imgui_gradient)
```

Then include it as:
```cpp
#include <imgui_gradient/imgui_gradient.hpp>
```

### Sampling the gradient

```cpp
const ColorRGBA color = widget.gradient().at({0.5f});
```

Note that you should use values between `0.f` and `1.f` when sampling the gradient. If you know you are gonna use values outside that range, specify a `WrapMode` to indicate how the values should be mapped back to the [0, 1] range:

```cpp
const ColorRGBA color = widget.gradient().at({0.5f, WrapMode::Clamp});
```

Our wrap modes mimic the OpenGL ones:

- `ImGG::WrapMode::Clamp`: If the value is bigger than 1, maps it to 1. If it smaller than 0, maps it to 0.
- `ImGG::WrapMode::Repeat`: Maps the number line to a bunch of copies of [0, 1] stuck together. Conceptually equivalent to adding or subtracting 1 to the position until it is in the [0, 1] range.
- `ImGG::WrapMode::MirrorRepeat`: Like `ImGG::WrapMode::Repeat` except that every other range is flipped.

To create a widget for the wrap mode, you can use:
```cpp
ImGG::wrap_mode_widget("Wrap Mode", &wrap_mode);
```

### Interpolation

Controls how the colors are interpolated between two marks.

- `ImGG::Interpolation::Linear`: Linear interpolation.
- `ImGG::Interpolation::Constant`: Constant color between two marks (uses the color of the mark on the right).

To create a widget that changes the interpolation mode, use:
```cpp
ImGG::interpolation_mode_widget("Interpolation Mode", &widget.gradient().interpolation_mode());
```

> NB: Our linear interpolation is done in the [Oklab](https://bottosson.github.io/posts/oklab/) color space, which gives more accurate results. We also use premultiplied alpha during the interpolation for the same reason.
> BUT the colors we output are all in sRGB space with straight alpha, for ease of use.

### Settings

The `ImGG::GradientWidget::widget()` method can also take settings that control its behaviour:
```cpp
ImGG::Settings settings{};

settings.gradient_width = 500.f;
settings.gradient_height = 40.f;
settings.horizontal_margin = 10.f;

/// Distance under the gradient bar to delete a mark by dragging it down.
/// This behaviour can also be disabled with the Flag::NoDragDowntoDelete.
settings.distance_to_delete_mark_by_dragging_down = 80.f;

settings.flags = ImGG::Flag::None;

settings.color_edit_flags = ImGuiColorEditFlags_None;

/// Controls how the new mark color is chosen.
/// If true, the new mark color will be a random color,
/// otherwise it will be the one that the gradient already has at the new mark position.
settings.should_use_a_random_color_for_the_new_marks = false;

gradient_widget.widget("My Gradient", settings);
```

Here are all the available flags:

- `ImGG::Flag::None`: All options are enabled.
- `ImGG::Flag::NoTooltip`: No tooltip when hovering a widget.
- `ImGG::Flag::NoResetButton`: No button to reset the gradient to its default value.
- `ImGG::Flag::NoLabel`: No name for the widget.
- `ImGG::Flag::NoAddButton`: No "+" button to add a mark.
- `ImGG::Flag::NoRemoveButton`: No "-" button to remove a mark.
- `ImGG::Flag::NoPositionSlider`: No slider widget to chose a precise position for the selected mark.
- `ImGG::Flag::NoColorEdit`: No color edit widget for the selected mark.
- `ImGG::Flag::NoDragDownToDelete`: Don't delete a mark when dragging it down.
- `ImGG::Flag::NoBorder`: No border around the gradient widget.
- `ImGG::Flag::NoAddAndRemoveButtons`: No "+" and "-" buttons.
- `ImGG::Flag::NoMarkOptions`: No widgets for the selected mark.

### Color randomization

`Settings::should_use_a_random_color_for_the_new_marks` allows you to randomize the colors of the marks.
To create a widget for that setting you can use:
```cpp
ImGG::random_mode_widget("Use a random color for the new marks", &settings.should_use_a_random_color_for_the_new_marks);
```

By default we use our own internal `std::default_random_engine`, but you can provide your own rng function that we will use instead. It can be any function that returns a number between `0.f` and `1.f`.

```cpp
const auto your_custom_rng = []() {
    static auto rng          = std::default_random_engine{std::random_device{}()};
    static auto distribution = std::uniform_real_distribution<float>{0.f, 1.f};
    return distribution(rng);
};
gradient_widget.widget("My Gradient", your_custom_rng, settings);
```

> *NB:* If all calls to `gradient_widget.widget(...)` provide a rng function, we will not create our `std::default_random_engine` at all.

## For maintainers

### Running the tests

Simply use "tests/CMakeLists.txt" to generate a project, then run it.<br/>
If you are using VSCode and the CMake extension, this project already contains a *.vscode/settings.json* that will use the right CMakeLists.txt automatically.
