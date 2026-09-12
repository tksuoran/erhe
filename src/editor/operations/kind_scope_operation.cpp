#include "operations/kind_scope_operation.hpp"

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/scope.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace editor {

Kind_scope_operation::Kind_scope_operation(const Parameters& parameters)
    : m_scope {parameters.scope}
    , m_parent{parameters.parent}
{
    ERHE_VERIFY(m_scope);
    ERHE_VERIFY(m_parent);
    set_description(fmt::format("[{}] Kind_scope '{}'", get_serial(), m_scope->get_name()));
}

void Kind_scope_operation::execute(App_context& context)
{
    static_cast<void>(context);
    if (m_scope->get_parent().lock()) {
        return; // already standing - a later operation's undo left it in the tree
    }
    m_scope->set_parent(m_parent);
}

void Kind_scope_operation::undo(App_context& context)
{
    static_cast<void>(context);
    if (!m_scope->get_children().empty()) {
        return; // resources a later operation placed here are not ours to take away
    }
    m_scope->set_parent(std::shared_ptr<erhe::Hierarchy>{});
}

}
