#include "operations/compound_operation.hpp"

#include "editor_log.hpp"

#include <sstream>

namespace editor {

Compound_operation::Compound_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
    std::stringstream ss;
    ss << fmt::format("[{}] Compound ", get_serial());
    bool first = true;
    for (auto& operation : m_parameters.operations) {
        if (first) {
            first = false;
        } else {
            ss << ", ";
        }
        ss << operation->describe();
    }
    set_description(ss.str());
}

Compound_operation::~Compound_operation() noexcept
{
}

void Compound_operation::execute(App_context& context)
{
    log_operations->trace("Op Execute Begin {}", describe());

    // A refused child undid itself and the earlier ones are undone, so the entry leaves nothing behind.
    std::optional<std::string> refusal = execute_unless_refused(context, m_parameters.operations);
    if (refusal.has_value()) {
        set_refused(std::move(refusal.value()));
        log_operations->info("Op Execute {} refused: {}", describe(), get_error());
        return;
    }

    log_operations->trace("Op Execute End {}", describe());
}

void Compound_operation::undo(App_context& context)
{
    log_operations->trace("Op Undo Begin {}", describe());

    for (auto i = rbegin(m_parameters.operations), end = rend(m_parameters.operations); i < end; ++i) {
        auto& operation = *i;
        operation->undo(context);
    }

    log_operations->trace("Op Undo End {}", describe());
}

void Compound_operation::collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const
{
    for (const std::shared_ptr<Operation>& operation : m_parameters.operations) {
        operation->collect_item_references(out_items);
    }
}

}
