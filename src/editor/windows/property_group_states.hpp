#pragma once

#include "config/generated/property_group_state.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace editor {

class Editor_settings_store;

// Which side of a group a dragged group is dropped on.
enum class Group_drop_side : unsigned int {
    before = 0,
    after  = 1
};

// The fold state and order of the property groups (Property_ui::group) that
// every Properties window shares - one state per group name, whatever window
// or item shows it - persisted in user_state.json (User_state_config::
// property_groups) through the settings store's collect callback. A group
// the state does not name yet is closed and is appended to the order the
// first time it is drawn, so the order always names every group seen.
class Property_group_states
{
public:
    explicit Property_group_states(Editor_settings_store& store);

    [[nodiscard]] auto is_open (std::string_view group) -> bool;
    void               set_open(std::string_view group, bool open);

    // Reorders `groups` into the persisted order: the groups the state
    // names first, in that order, then the rest in their incoming order,
    // which is the order they are appended to the state in.
    void order(std::vector<std::string_view>& groups);

    // Moves `group` directly before or after `target` in the order.
    void move(std::string_view group, std::string_view target, Group_drop_side side);

private:
    [[nodiscard]] auto find       (std::string_view group) -> Property_group_state*;
    [[nodiscard]] auto find_or_add(std::string_view group) -> Property_group_state&;

    Editor_settings_store&            m_store;
    std::vector<Property_group_state> m_groups;
    std::vector<std::string_view>     m_order_scratch; // order(): capacity kept
};

}
