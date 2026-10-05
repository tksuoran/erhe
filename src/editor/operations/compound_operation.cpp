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

    const bool first_execute = !m_executed;
    m_executed = true;
    for (std::size_t i = 0, end = m_parameters.operations.size(); i < end; ++i) {
        Operation& operation = *m_parameters.operations[i].get();
        operation.execute(context);
        if (!first_execute || (m_parameters.child_error != Compound_child_error::roll_back) || !operation.has_error()) {
            continue;
        }
        // A child in error after its first execute changed nothing
        // (Operation_stack's all-or-nothing rule); undoing the children
        // before it leaves the whole compound unapplied.
        for (std::size_t j = i; j > 0; --j) {
            m_parameters.operations[j - 1]->undo(context);
        }
        set_error(operation.get_error());
        log_operations->trace("Op Execute Rolled Back {}: {}", describe(), get_error());
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
