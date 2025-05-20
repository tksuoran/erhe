#pragma once

namespace ImGG {

using Flags = int;

namespace Flag {

enum ImGuiGradientFlag {
    None                  = 0,                              // All options are enabled
    NoTooltip             = 1 << 1,                         // No tooltip when hovering a widget
    NoResetButton         = 1 << 2,                         // No button to reset the gradient to its default value.
    NoLabel               = 1 << 3,                         // No name for the widget
    NoAddButton           = 1 << 5,                         // No "+" button to add a mark
    NoRemoveButton        = 1 << 6,                         // No "-" button to remove a mark
    NoPositionSlider      = 1 << 7,                         // No slider widget to chose a precise position for the selected mark
    NoColorEdit           = 1 << 8,                         // No color edit widget for the selected mark
    NoDragDownToDelete    = 1 << 11,                        // Don't delete a mark when dragging it down
    NoBorder              = 1 << 12,                        // No border around the gradient widget
    NoAddAndRemoveButtons = NoAddButton | NoRemoveButton,   // No "+" and "-" buttons
    NoMarkOptions         = NoColorEdit | NoPositionSlider, // No widgets for the selected mark
};

} // namespace Flag
} // namespace ImGG
