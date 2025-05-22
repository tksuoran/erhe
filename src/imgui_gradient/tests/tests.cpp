#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <imgui_gradient/imgui_gradient.hpp>
#include <quick_imgui/quick_imgui.hpp>
#include <random>
#include "../generated/checkboxes_for_all_flags.inl"
#include "../src/Utils.hpp" // to test wrap mode functions
#include "../src/maybe_disabled.hpp"

auto main(int argc, char* argv[]) -> int
{
    const int  exit_code              = doctest::Context{}.run(); // Run all unit tests
    const bool should_run_imgui_tests = argc < 2 || strcmp(argv[1], "-nogpu") != 0;
    if (
        should_run_imgui_tests
        && exit_code == 0 // Only open the window if the tests passed; this makes it easier to notice when some tests fail
    )
    {
        auto gradient  = ImGG::GradientWidget{};
        auto gradient2 = ImGG::GradientWidget{};
        quick_imgui::loop("imgui_gradient tests", [&]() {
            ImGui::ShowDemoWindow();
            ImGui::Begin("Framerate");
            ImGui::Text("%.3f FPS", ImGui::GetIO().Framerate);
            ImGui::End();
            ImGui::Begin("Flags");
            const auto flags = checkboxes_for_all_flags();
            ImGui::End();
            ImGui::Begin("Options");
            static auto custom_generator = false;
            ImGui::Checkbox("Use our custom generator", &custom_generator);
            ImGui::End();
            ImGui::Begin("Settings");
            static ImGG::Settings settings{};
            ImGui::PushItemWidth(100.f);
            ImGui::DragFloat("Gradient width", &settings.gradient_width);
            ImGui::DragFloat("Gradient height", &settings.gradient_height);
            ImGui::DragFloat("Horizontal margin", &settings.horizontal_margin);
            ImGui::DragFloat("Distance to delete mark by dragging down", &settings.distance_to_delete_mark_by_dragging_down);
            ImGui::End();
            ImGui::Begin("Programmatic Actions");
            ImGG::maybe_disabled(gradient.gradient().is_empty(), "Gradient is empty", [&]() {
                if (ImGui::Button("Remove a mark"))
                {
                    gradient.gradient().remove_mark(ImGG::MarkId{gradient.gradient().get_marks().front()});
                }
            });
            if (ImGui::Button("Add a mark"))
            {
                gradient.gradient().add_mark(ImGG::Mark{ImGG::RelativePosition{0.5f}, ImVec4{0.f, 0.f, 0.f, 1.f}});
            };
            static auto position{0.f};
            if (ImGui::Button("Set mark position") && !gradient.gradient().is_empty())
            {
                gradient.gradient().set_mark_position(
                    ImGG::MarkId{gradient.gradient().get_marks().front()},
                    ImGG::RelativePosition{position}
                );
            };
            ImGui::SameLine();
            ImGui::DragFloat("##260", &position, .0001f, /* speed */
                             0.f, 1.f,                   /* min and max */
                             "%.4f" /* precision */);
            static auto color = ImVec4{0.f, 0.f, 0.f, 1.f};
            if (ImGui::Button("Set mark color") && !gradient.gradient().is_empty())
            {
                gradient.gradient().set_mark_color(ImGG::MarkId{gradient.gradient().get_marks().front()}, color);
            };
            ImGui::SameLine();
            ImGui::ColorEdit4(
                "##colorpicker1",
                reinterpret_cast<float*>(&color),
                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs
            );
            static ImGG::WrapMode wrap_mode{};
            ImGG::wrap_mode_widget("Position Mode", &wrap_mode);
            static ImGG::Interpolation interpolation_mode{};
            if (ImGG::interpolation_mode_widget("Interpolation Mode", &interpolation_mode))
            {
                gradient.gradient().interpolation_mode()  = interpolation_mode;
                gradient2.gradient().interpolation_mode() = interpolation_mode;
            }
            ImGG::random_mode_widget("Randomize new marks' color", &settings.should_use_a_random_color_for_the_new_marks);

            if (ImGui::Button("Equalize"))
                gradient.gradient().distribute_marks_evenly();

            ImGui::End();
            ImGui::Begin("imgui_gradient tests");
            settings.flags            = flags;
            settings.color_edit_flags = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR;
            if (custom_generator)
            {
                const auto custom_rng = []() {
                    static auto rng          = std::default_random_engine{std::random_device{}()};
                    static auto distribution = std::uniform_real_distribution<float>{0.f, 1.f};
                    return distribution(rng);
                };
                gradient.widget(
                    "Gradient",
                    custom_rng,
                    settings
                );
                gradient2.widget(
                    "Gradient2",
                    custom_rng,
                    settings
                );
            }
            else
            {
                gradient.widget(
                    "Gradient",
                    settings
                );
                gradient2.widget(
                    "Gradient2",
                    settings
                );
            }
            ImGui::End();
        });
    }
    return exit_code;
}

// Check out doctest's documentation: https://github.com/doctest/doctest/blob/master/doc/markdown/tutorial.md

TEST_CASE("Distribute marks evenly")
{
    ImGG::Gradient gradient;
    auto const&    marks = gradient.get_marks();

    for (int i = 0; i < 2; ++i)
    {
        // Test with both interpolation modes
        gradient.interpolation_mode() = i == 0 ? ImGG::Interpolation::Linear : ImGG::Interpolation::Constant;
        gradient.clear();
        // ---

        // With 0 marks
        gradient.distribute_marks_evenly(); // Test that this doesn't crash.

        // With 1 mark
        gradient.add_mark({ImGG::RelativePosition{0.3f}, ImGG::ColorRGBA{}});
        gradient.distribute_marks_evenly();
        CHECK(marks.begin()->position.get() == doctest::Approx{0.5f});

        // With 2 marks
        gradient.add_mark({ImGG::RelativePosition{0.8f}, ImGG::ColorRGBA{}});
        gradient.distribute_marks_evenly();
        if (gradient.interpolation_mode() == ImGG::Interpolation::Linear)
        {
            CHECK(marks.begin()->position.get() == doctest::Approx{0.f});
            CHECK(std::next(marks.begin())->position.get() == doctest::Approx{1.f});
        }
        else
        {
            CHECK(marks.begin()->position.get() == doctest::Approx{0.5f});
            CHECK(std::next(marks.begin())->position.get() == doctest::Approx{1.f});
        }
    }
}

TEST_CASE("Evaluating at 0 and 1")
{
    // Gradient from black to white
    ImGG::Gradient gradient{{
        ImGG::Mark{ImGG::RelativePosition{0.f}, ImGG::ColorRGBA{0.f, 0.f, 0.f, 1.f}},
        ImGG::Mark{ImGG::RelativePosition{1.f}, ImGG::ColorRGBA{1.f, 1.f, 1.f, 1.f}},
    }};

    CHECK(doctest::Approx(gradient.at({0.f, ImGG::WrapMode::Clamp}).x) == 0.f);
    CHECK(doctest::Approx(gradient.at({1.f, ImGG::WrapMode::Clamp}).x) == 1.f);
    CHECK(doctest::Approx(gradient.at({0.f, ImGG::WrapMode::Repeat}).x) == 0.f);
    CHECK(doctest::Approx(gradient.at({1.f, ImGG::WrapMode::Repeat}).x) == 0.f);
    CHECK(doctest::Approx(gradient.at({0.f, ImGG::WrapMode::MirrorRepeat}).x) == 0.f);
    CHECK(doctest::Approx(gradient.at({1.f, ImGG::WrapMode::MirrorRepeat}).x) == 1.f);

    CHECK(doctest::Approx(gradient.at({-0.000001f, ImGG::WrapMode::Clamp}).x) == 0.f);
    CHECK(doctest::Approx(gradient.at({0.000001f, ImGG::WrapMode::Clamp}).x) == 0.f);
    CHECK(doctest::Approx(gradient.at({1.0000001f, ImGG::WrapMode::Clamp}).x) == 1.f);
    CHECK(doctest::Approx(gradient.at({0.9999999f, ImGG::WrapMode::Clamp}).x) == 1.f);

    CHECK(doctest::Approx(gradient.at({-0.000001f, ImGG::WrapMode::Repeat}).x) == 1.f);
    CHECK(doctest::Approx(gradient.at({0.000001f, ImGG::WrapMode::Repeat}).x) == 0.f);
    CHECK(doctest::Approx(gradient.at({1.0000001f, ImGG::WrapMode::Repeat}).x) == 0.f);
    CHECK(doctest::Approx(gradient.at({0.9999999f, ImGG::WrapMode::Repeat}).x) == 1.f);

    CHECK(doctest::Approx(gradient.at({-0.000001f, ImGG::WrapMode::MirrorRepeat}).x) == 0.f);
    CHECK(doctest::Approx(gradient.at({0.000001f, ImGG::WrapMode::MirrorRepeat}).x) == 0.f);
    CHECK(doctest::Approx(gradient.at({1.0000001f, ImGG::WrapMode::MirrorRepeat}).x) == 1.f);
    CHECK(doctest::Approx(gradient.at({0.9999999f, ImGG::WrapMode::MirrorRepeat}).x) == 1.f);
}

TEST_CASE("Interpolation modes")
{
    // Gradient from black to white
    ImGG::Gradient gradient{{
        ImGG::Mark{ImGG::RelativePosition{0.f}, ImGG::ColorRGBA{0.f, 0.f, 0.f, 1.f}},
        ImGG::Mark{ImGG::RelativePosition{1.f}, ImGG::ColorRGBA{1.f, 1.f, 1.f, 1.f}},
    }};

    gradient.interpolation_mode() = ImGG::Interpolation::Linear;
    CHECK(doctest::Approx(gradient.at(ImGG::RelativePosition{0.25f}).x) == 0.131499f);
    CHECK(doctest::Approx(gradient.at(ImGG::RelativePosition{0.75f}).x) == 0.681341f);
    gradient.interpolation_mode() = ImGG::Interpolation::Constant;
    CHECK(doctest::Approx(gradient.at(ImGG::RelativePosition{0.25f}).x) == 1.f);
    CHECK(doctest::Approx(gradient.at(ImGG::RelativePosition{0.75f}).x) == 1.f);
}

TEST_CASE("Wrap modes")
{
    SUBCASE("clamp_position() when position in the range [0,1]")
    {
        const float position   = 0.3f;
        const auto  repeat_pos = ImGG::internal::clamp_position(position);

        CHECK(doctest::Approx(repeat_pos.get()) == 0.3f);
    }
    SUBCASE("clamp_position() when position in the range [-1,0]")
    {
        const float position  = -0.4f;
        const auto  clamp_pos = ImGG::internal::clamp_position(position);

        CHECK(doctest::Approx(clamp_pos.get()) == 0.f);
    }
    SUBCASE("clamp_position() when position is < -1")
    {
        const float position  = -1.4f;
        const auto  clamp_pos = ImGG::internal::clamp_position(position);

        CHECK(doctest::Approx(clamp_pos.get()) == 0.f);
    }
    SUBCASE("clamp_position() when position > 1")
    {
        const float position  = 1.9f;
        const auto  clamp_pos = ImGG::internal::clamp_position(position);

        CHECK(doctest::Approx(clamp_pos.get()) == 1.f);
    }
    SUBCASE("clamp_position() when position = 1")
    {
        const float position  = 1.f;
        const auto  clamp_pos = ImGG::internal::clamp_position(position);

        CHECK(doctest::Approx(clamp_pos.get()) == 1.f);
    }
    SUBCASE("clamp_position() when position = 0")
    {
        const float position  = 0.f;
        const auto  clamp_pos = ImGG::internal::clamp_position(position);

        CHECK(doctest::Approx(clamp_pos.get()) == 0.f);
    }

    SUBCASE("repeat_position() when position in the range [0,1]")
    {
        const float position   = 0.2f;
        const auto  repeat_pos = ImGG::internal::repeat_position(position);

        CHECK(doctest::Approx(repeat_pos.get()) == 0.2f);
    }
    SUBCASE("repeat_position() when position in the range [-1,0]")
    {
        const float position   = -0.3f;
        const auto  repeat_pos = ImGG::internal::repeat_position(position);

        CHECK(doctest::Approx(repeat_pos.get()) == 0.7f);
    }
    SUBCASE("repeat_position() when position is < -1")
    {
        const float position   = -1.4f;
        const auto  repeat_pos = ImGG::internal::repeat_position(position);

        CHECK(doctest::Approx(repeat_pos.get()) == 0.6f);
    }
    SUBCASE("repeat_position() when position > 1")
    {
        const float position   = 1.8f;
        const auto  repeat_pos = ImGG::internal::repeat_position(position);

        CHECK(doctest::Approx(repeat_pos.get()) == 0.8f);
    }
    SUBCASE("repeat_position() when position just before 1")
    {
        const float position   = .99f;
        const auto  repeat_pos = ImGG::internal::repeat_position(position);

        CHECK(doctest::Approx(repeat_pos.get()) == .99f);
    }
    SUBCASE("repeat_position() when position just after 1")
    {
        const float position   = 1.01f;
        const auto  repeat_pos = ImGG::internal::repeat_position(position);

        CHECK(doctest::Approx(repeat_pos.get()) == .01f);
    }
    SUBCASE("repeat_position() when position just before 0")
    {
        const float position   = -0.01f;
        const auto  repeat_pos = ImGG::internal::repeat_position(position);

        CHECK(doctest::Approx(repeat_pos.get()) == 0.99f);
    }
    SUBCASE("repeat_position() when position just after 0")
    {
        const float position   = 0.01f;
        const auto  repeat_pos = ImGG::internal::repeat_position(position);

        CHECK(doctest::Approx(repeat_pos.get()) == 0.01f);
    }

    SUBCASE("mirror_repeat_position() when position in the range [0,1]")
    {
        const float position          = 0.4f;
        const auto  mirror_repeat_pos = ImGG::internal::mirror_repeat_position(position);

        CHECK(doctest::Approx(mirror_repeat_pos.get()) == 0.4f);
    }
    SUBCASE("mirror_repeat_position() when position in the range [-1,0]")
    {
        const float position          = -0.2f;
        const auto  mirror_repeat_pos = ImGG::internal::mirror_repeat_position(position);

        CHECK(doctest::Approx(mirror_repeat_pos.get()) == 0.2f);
    }
    SUBCASE("mirror_repeat_position() when position is negative and < -1")
    {
        const float position          = -1.6f;
        const auto  mirror_repeat_pos = ImGG::internal::mirror_repeat_position(position);

        CHECK(doctest::Approx(mirror_repeat_pos.get()) == 0.4f);
    }
    SUBCASE("mirror_repeat_position() when position > 1")
    {
        const float position          = 1.8f;
        const auto  mirror_repeat_pos = ImGG::internal::mirror_repeat_position(position);

        CHECK(doctest::Approx(mirror_repeat_pos.get()) == 0.2f);
    }
    SUBCASE("mirror_repeat_position() when position = 1")
    {
        const float position          = 1.f;
        const auto  mirror_repeat_pos = ImGG::internal::mirror_repeat_position(position);

        CHECK(doctest::Approx(mirror_repeat_pos.get()) == 1.f);
    }
    SUBCASE("mirror_repeat_position() when position = 0")
    {
        const float position          = 0.f;
        const auto  mirror_repeat_pos = ImGG::internal::mirror_repeat_position(position);

        CHECK(doctest::Approx(mirror_repeat_pos.get()) == 0.f);
    }

    SUBCASE("test intermediate functions : modulo(x,mod) when x = 0")
    {
        const float x          = 0.f;
        const float mod        = 2.f;
        const float modulo_res = ImGG::internal::modulo(x, mod);

        CHECK(doctest::Approx(modulo_res) == 0.f);
    }
    SUBCASE("test intermediate functions : modulo(x, mod) when x = 1")
    {
        const float x          = 1.f;
        const float mod        = 2.f;
        const float modulo_res = ImGG::internal::modulo(x, mod);

        CHECK(doctest::Approx(modulo_res) == 1.f);
    }
}
