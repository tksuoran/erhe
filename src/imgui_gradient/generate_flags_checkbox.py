# ---HOW TO---
# Modify `all_flags()` and `includes()`, then run this script.
# ------------


def all_flags():
    return {
        "ImGG::Flag::NoTooltip":                  ["isNoTooltip"],
        "ImGG::Flag::NoResetButton":              ["isNoResetButton"],
        "ImGG::Flag::NoLabel":                    ["isNoLabel"],
        "ImGG::Flag::NoAddButton":                ["isNoAddButton"],
        "ImGG::Flag::NoRemoveButton":             ["isNoRemoveButton"],
        "ImGG::Flag::NoPositionSlider":           ["isNoPositionSlider"],
        "ImGG::Flag::NoColorEdit":                ["isNoColorEdit"],
        "ImGG::Flag::NoDragDownToDelete":         ["isNoDragDownToDelete"],
        "ImGG::Flag::NoBorder":                   ["isNoBorder"],
        "ImGG::Flag::NoAddAndRemoveButtons":      ["isNoAddAndRemoveButtons"],
        "ImGG::Flag::NoMarkOptions":              ["isNoMarkOptions"],
    }


def checkboxes_for_all_flags():
    out = f"""
#include <imgui/imgui.h>
#include "../src/Flags.hpp"

auto checkboxes_for_all_flags() -> ImGG::Flags
{{
ImGG::Flags options = ImGG::Flag::None;

"""
    for key, values in all_flags().items():
        for value in values:
            out += f"""
static auto {value} = false;
ImGui::Checkbox("{key}", &{value});
if ({value})
{{
    options|={key};
}}
"""
    out += f"""
    return options;
}}"""
    return out


if __name__ == '__main__':
    from tooling.generate_files import generate
    generate(
        folder="generated",
        files=[
            checkboxes_for_all_flags,
        ],
    )
