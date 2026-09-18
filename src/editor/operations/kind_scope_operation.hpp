#pragma once

#include "operations/operation.hpp"

#include <memory>

namespace erhe {
    class Hierarchy;
    class Scope;
}

namespace editor {

// Brings a content library's kind `Scope` into the tree (doc/erhe/usd_compatibility_design.md
// C5 / U4: resources sit under a lazily made scope named for their kind).
// The scope a resource insert needed is part of the operation that needed it,
// so the same undo that takes the resource out takes the scope it created out
// with it - a compound places this step BEFORE the resource insert, so undo
// runs it last, when the resource is already gone.
//
// Undo removes the scope only when the scope is childless at that moment: a
// later operation may have placed other resources under it, and those are not
// this operation's to take away. The scope then stays standing, and this
// operation's own redo finds it in the tree and does nothing.
class Kind_scope_operation : public Operation
{
public:
    class Parameters
    {
    public:
        std::shared_ptr<erhe::Scope>     scope;
        std::shared_ptr<erhe::Hierarchy> parent;
    };

    explicit Kind_scope_operation(const Parameters& parameters);

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    std::shared_ptr<erhe::Scope>     m_scope;
    std::shared_ptr<erhe::Hierarchy> m_parent;
};

}
