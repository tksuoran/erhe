#pragma once

#include "operations/operation.hpp"

#include "erhe_item/hierarchy.hpp"

#include <memory>

namespace editor {

class Content_library;

// Moves a resource prim or a folder scope under another scope of the same
// library (doc/content-library-folders.md D3). Records the prim's parent and
// index before and after; execute and undo are one set_parent each, under the
// library mutex, so the detach and the attach happen inside one call and the
// move is never announced as a removal.
class Content_library_move_operation : public Operation
{
public:
    Content_library_move_operation(
        std::shared_ptr<Content_library>       content_library,
        std::shared_ptr<erhe::Hierarchy>       prim,
        std::shared_ptr<erhe::Hierarchy>       new_parent,
        std::size_t                            new_index
    );

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    std::shared_ptr<Content_library> m_content_library;
    std::shared_ptr<erhe::Hierarchy> m_node;
    std::shared_ptr<erhe::Hierarchy> m_before_parent;
    std::size_t                      m_before_index;
    std::shared_ptr<erhe::Hierarchy> m_after_parent;
    std::size_t                      m_after_index;
};

}
