#include "operations/live_edit.hpp"

#include "erhe_verify/verify.hpp"

namespace editor {

Live_edit_token::Live_edit_token(const std::uint64_t serial)
    : m_serial{serial}
{
}

Live_edit_token::Live_edit_token(Live_edit_token&& other) noexcept
    : m_serial{other.m_serial}
{
    other.m_serial = 0;
}

auto Live_edit_token::operator=(Live_edit_token&& other) noexcept -> Live_edit_token&
{
    if (this != &other) {
        m_serial       = other.m_serial;
        other.m_serial = 0;
    }
    return *this;
}

auto Live_edit_token::get_serial() const -> std::uint64_t
{
    return m_serial;
}

auto Live_edit::begin(std::string reason, std::function<void()> on_abandoned) -> Live_edit_token
{
    ERHE_VERIFY(!is_open());
    ERHE_VERIFY(!reason.empty());
    m_reason       = std::move(reason);
    m_on_abandoned = std::move(on_abandoned);
    m_serial       = m_next_serial++;
    m_operations.open();
    return Live_edit_token{m_serial};
}

auto Live_edit::is_owner(const Live_edit_token& token) const -> bool
{
    return is_open() && (token.get_serial() != 0) && (token.get_serial() == m_serial);
}

auto Live_edit::is_open() const -> bool
{
    return m_operations.is_open();
}

auto Live_edit::get_reason() const -> const char*
{
    return is_open() ? m_reason.c_str() : nullptr;
}

auto Live_edit::get_size() const -> std::size_t
{
    return m_operations.get_size();
}

void Live_edit::add(const std::shared_ptr<Operation>& operation)
{
    m_operations.add(operation);
}

void Live_edit::close()
{
    m_serial = 0;
    m_reason.clear();
    m_on_abandoned = {};
}

auto Live_edit::end() -> std::shared_ptr<Operation>
{
    std::shared_ptr<Operation> entry = m_operations.close();
    close();
    return entry;
}

void Live_edit::cancel(App_context& context)
{
    m_operations.undo_after(context, 0);
    static_cast<void>(m_operations.close());
    close();
}

auto Live_edit::abandon(App_context& context) -> std::function<void()>
{
    std::function<void()> on_abandoned = std::move(m_on_abandoned);
    cancel(context);
    return on_abandoned;
}

void Live_edit::collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const
{
    m_operations.collect_item_references(out_items);
}

auto get_operation_admission(const Operation_request request, const Save_hold save_hold, const Live_edit& live_edit) -> Operation_admission
{
    if (live_edit.is_open()) {
        return (request == Operation_request::execute_now) ? Operation_admission::refuse : Operation_admission::hold;
    }
    return (save_hold == Save_hold::held) ? Operation_admission::hold : Operation_admission::execute;
}

} // namespace editor
