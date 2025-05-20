#pragma once

namespace ImGG { namespace internal {

class HoverChecker {
public:
    void update();
    void force_consider_hovered();
    auto is_item_hovered() const -> bool;

private:
    int _frame_since_not_hovered = 0;
};

}} // namespace ImGG::internal
