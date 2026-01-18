#pragma once

#include "operations/operation.hpp"

#include <memory>

namespace erhe { class Hierarchy; }

namespace editor {

class Item_parent_change_operation : public Operation
{
public:
    Item_parent_change_operation();
    Item_parent_change_operation(
        const std::shared_ptr<erhe::Hierarchy>& parent,
        const std::shared_ptr<erhe::Hierarchy>& child,
        const std::shared_ptr<erhe::Hierarchy>& place_before,
        const std::shared_ptr<erhe::Hierarchy>& place_after
    );

    // Implements Operation
    void execute (App_context& context)  override;
    void undo    (App_context& context)  override;

private:
    std::shared_ptr<erhe::Hierarchy> m_child              {};
    std::shared_ptr<erhe::Hierarchy> m_parent_before      {};
    std::size_t                      m_parent_before_index{0};
    std::shared_ptr<erhe::Hierarchy> m_parent_after       {};
    std::size_t                      m_parent_after_index {0};
    std::shared_ptr<erhe::Hierarchy> m_place_before       {};
    std::shared_ptr<erhe::Hierarchy> m_place_after        {};
};

}
