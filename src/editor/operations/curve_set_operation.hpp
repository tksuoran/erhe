#pragma once

#include "operations/operation.hpp"

#include "erhe_math/curve_path.hpp"

#include <memory>
#include <string>
#include <vector>

namespace erhe::scene { class Curve; }

namespace editor {

// Replaces a curve's whole path (points, handles, kinds, closure): `after` on execute, `before` on undo. The first
// execute records the follow-ups of the change (the curve laid out again when it is a locked path layout,
// Level_kits::append_curve_follow_ups) and runs them; redo runs them again, undo undoes them first.
class Curve_set_operation : public Operation
{
public:
    Curve_set_operation(
        const std::shared_ptr<erhe::scene::Curve>& curve,
        erhe::math::Curve_path                     before,
        erhe::math::Curve_path                     after,
        std::string                                what
    );
    ~Curve_set_operation() noexcept override;

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;
    void collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const override;

private:
    std::shared_ptr<erhe::scene::Curve>     m_curve;
    erhe::math::Curve_path                  m_before;
    erhe::math::Curve_path                  m_after;
    std::vector<std::shared_ptr<Operation>> m_follow_ups;
    bool                                    m_follow_ups_recorded{false};
};

} // namespace editor
