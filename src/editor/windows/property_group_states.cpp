#include "windows/property_group_states.hpp"

#include "editor_settings_store.hpp"

#include <algorithm>

namespace editor {

Property_group_states::Property_group_states(Editor_settings_store& store)
    : m_store {store}
    , m_groups{store.get_user_state().property_groups}
{
    // App-lifetime like the store's owner, so the callback is never
    // unregistered.
    m_store.register_collect_callback(
        [this](Editor_settings_config&, User_state_config& user_state) {
            user_state.property_groups = m_groups;
        }
    );
}

auto Property_group_states::find(const std::string_view group) -> Property_group_state*
{
    for (Property_group_state& state : m_groups) {
        if (state.name == group) {
            return &state;
        }
    }
    return nullptr;
}

auto Property_group_states::find_or_add(const std::string_view group) -> Property_group_state&
{
    Property_group_state* const found = find(group);
    if (found != nullptr) {
        return *found;
    }
    m_groups.push_back(Property_group_state{.name = std::string{group}, .open = false});
    m_store.touch();
    return m_groups.back();
}

auto Property_group_states::is_open(const std::string_view group) -> bool
{
    return find_or_add(group).open;
}

void Property_group_states::set_open(const std::string_view group, const bool open)
{
    Property_group_state& state = find_or_add(group);
    if (state.open != open) {
        state.open = open;
        m_store.touch();
    }
}

void Property_group_states::order(std::vector<std::string_view>& groups)
{
    for (const std::string_view group : groups) {
        static_cast<void>(find_or_add(group)); // appends the groups seen for the first time, in incoming order
    }
    m_order_scratch.clear();
    for (const Property_group_state& state : m_groups) {
        const std::vector<std::string_view>::const_iterator i = std::find(groups.begin(), groups.end(), std::string_view{state.name});
        if (i != groups.end()) {
            m_order_scratch.push_back(*i);
        }
    }
    groups.assign(m_order_scratch.begin(), m_order_scratch.end());
}

void Property_group_states::move(const std::string_view group, const std::string_view target, const Group_drop_side side)
{
    if (group == target) {
        return;
    }
    Property_group_state* const source_state = find(group);
    if ((source_state == nullptr) || (find(target) == nullptr)) {
        return;
    }
    const Property_group_state moved = *source_state;
    m_groups.erase(m_groups.begin() + (source_state - m_groups.data()));
    std::vector<Property_group_state>::iterator target_i = m_groups.begin();
    while (target_i->name != target) {
        ++target_i;
    }
    if (side == Group_drop_side::after) {
        ++target_i;
    }
    m_groups.insert(target_i, moved);
    m_store.touch();
}

}
