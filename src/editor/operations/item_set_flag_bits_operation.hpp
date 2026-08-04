#pragma once

#include "operations/operation.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace erhe { class Item_base; }

namespace editor {

// Undoable bulk flag-bit edit: sets (or clears) the given Item_flags bits
// on every item in the list; undo applies the opposite. The caller
// pre-filters the list to items whose current state differs from the
// target, so both directions are exact inverses without per-item
// before-state bookkeeping (Hierarchy window "Enable/Disable Lightmap
// (Recursive)" entries build the list from the meshes under the clicked /
// selected nodes).
class Item_set_flag_bits_operation : public Operation
{
public:
    Item_set_flag_bits_operation(
        std::vector<std::shared_ptr<erhe::Item_base>>&& items,
        uint64_t                                        flag_bits,
        bool                                            enable,
        const char*                                     label
    );

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;
    // R5.4: the retained items (meshes) pin their materials through their
    // primitives for as long as this operation is recorded.
    void collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const override;

private:
    void apply(bool enable);

    std::vector<std::shared_ptr<erhe::Item_base>> m_items;
    uint64_t                                      m_flag_bits;
    bool                                          m_enable;
};

}
