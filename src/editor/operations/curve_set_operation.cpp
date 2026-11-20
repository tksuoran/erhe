#include "operations/curve_set_operation.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "level/level_kits.hpp"

#include "erhe_scene/curve.hpp"

#include <fmt/format.h>

namespace editor {

Curve_set_operation::Curve_set_operation(
    const std::shared_ptr<erhe::scene::Curve>& curve,
    erhe::math::Curve_path                     before,
    erhe::math::Curve_path                     after,
    std::string                                what
)
    : m_curve {curve}
    , m_before{std::move(before)}
    , m_after {std::move(after)}
{
    set_description(fmt::format("Curve '{}' {}", curve ? curve->get_name() : std::string{}, what));
}

Curve_set_operation::~Curve_set_operation() noexcept = default;

void Curve_set_operation::execute(App_context& context)
{
    log_operations->trace("Op Execute {}", describe());
    if (!m_curve) {
        return;
    }
    m_curve->set_path(m_after);
    if (!m_follow_ups_recorded) {
        m_follow_ups_recorded = true;
        if (context.level_kits != nullptr) {
            context.level_kits->append_curve_follow_ups(*m_curve, m_before, m_follow_ups);
        }
    }
    const Operation* const refused = execute_unless_refused(context, m_follow_ups);
    if (refused != nullptr) {
        // A follow-up refused the change (a floor fit, K0.3): the path goes back and the edit is refused with it.
        m_follow_ups.clear();
        m_curve->set_path(m_before);
        set_refused(refused->get_error());
    }
}

void Curve_set_operation::undo(App_context& context)
{
    log_operations->trace("Op Undo {}", describe());
    if (!m_curve) {
        return;
    }
    for (auto i = m_follow_ups.rbegin(), end = m_follow_ups.rend(); i != end; ++i) {
        (*i)->undo(context);
    }
    m_curve->set_path(m_before);
}

void Curve_set_operation::collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const
{
    if (m_curve) {
        out_items.insert(m_curve.get());
    }
    for (const std::shared_ptr<Operation>& follow_up : m_follow_ups) {
        follow_up->collect_item_references(out_items);
    }
}

} // namespace editor
