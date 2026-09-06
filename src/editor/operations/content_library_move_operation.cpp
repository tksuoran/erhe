#include "operations/content_library_move_operation.hpp"

#include "content_library/content_library.hpp"

#include "erhe_profile/profile.hpp"

#include <fmt/format.h>

#include <mutex>

namespace editor {

Content_library_move_operation::Content_library_move_operation(
    std::shared_ptr<Content_library> content_library,
    std::shared_ptr<erhe::Hierarchy> prim,
    std::shared_ptr<erhe::Hierarchy> new_parent,
    const std::size_t                new_index
)
    : m_content_library{std::move(content_library)}
    , m_node           {std::move(prim)}
    , m_before_parent  {m_node->get_parent().lock()}
    , m_before_index   {m_node->get_index_in_parent()}
    , m_after_parent   {std::move(new_parent)}
    , m_after_index    {new_index}
{
    set_description(
        fmt::format(
            "[{}] Content_library_move '{}' -> '{}'",
            get_serial(),
            m_node->get_name(),
            m_after_parent ? m_after_parent->get_name() : "(none)"
        )
    );
}

void Content_library_move_operation::execute(App_context&)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_content_library->mutex};
    m_node->set_parent(m_after_parent, m_after_index);
}

void Content_library_move_operation::undo(App_context&)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_content_library->mutex};
    m_node->set_parent(m_before_parent, m_before_index);
}

}
