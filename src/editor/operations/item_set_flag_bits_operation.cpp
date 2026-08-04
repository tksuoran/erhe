#include "operations/item_set_flag_bits_operation.hpp"
#include "editor_log.hpp"

#include "erhe_item/item.hpp"

#include <fmt/format.h>

namespace editor {

Item_set_flag_bits_operation::Item_set_flag_bits_operation(
    std::vector<std::shared_ptr<erhe::Item_base>>&& items,
    const uint64_t                                  flag_bits,
    const bool                                      enable,
    const char* const                               label
)
    : m_items    {std::move(items)}
    , m_flag_bits{flag_bits}
    , m_enable   {enable}
{
    set_description(fmt::format("[{}] {} ({} items)", get_serial(), label, m_items.size()));
}

void Item_set_flag_bits_operation::apply(const bool enable)
{
    for (const std::shared_ptr<erhe::Item_base>& item : m_items) {
        if (item) {
            item->set_flag_bits(m_flag_bits, enable);
        }
    }
}

void Item_set_flag_bits_operation::execute(App_context&)
{
    log_operations->trace("Op Execute {}", describe());
    apply(m_enable);
}

void Item_set_flag_bits_operation::undo(App_context&)
{
    log_operations->trace("Op Undo {}", describe());
    apply(!m_enable);
}

void Item_set_flag_bits_operation::collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const
{
    for (const std::shared_ptr<erhe::Item_base>& item : m_items) {
        if (item) {
            out_items.insert(item.get());
        }
    }
}

}
