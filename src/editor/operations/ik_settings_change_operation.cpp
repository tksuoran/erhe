#include "operations/ik_settings_change_operation.hpp"

#include <fmt/format.h>

namespace editor {

Ik_settings_change_operation::Ik_settings_change_operation(
    const std::shared_ptr<Ik_settings>& ik_settings,
    const Ik_settings_data&             before,
    const Ik_settings_data&             after
)
    : m_ik_settings{ik_settings}
    , m_before{before}
    , m_after{after}
{
    set_description(fmt::format("IK settings change {}", m_ik_settings->get_name()));
}

Ik_settings_change_operation::~Ik_settings_change_operation() noexcept = default;

void Ik_settings_change_operation::execute(App_context&)
{
    m_ik_settings->data = m_after;
}

void Ik_settings_change_operation::undo(App_context&)
{
    m_ik_settings->data = m_before;
}

}
