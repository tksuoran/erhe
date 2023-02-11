#pragma once

#include "erhe/components/components.hpp"

#include <memory>

namespace editor
{

class IOperation;
class Operation_stack;

class Operation_stack_impl;

class Operation_stack
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"Operation_stack"};
    static constexpr std::string_view c_title{"Operation Stack"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Operation_stack ();
    ~Operation_stack() noexcept override;

    // Implements Component
    auto get_type_hash              () const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    // Public API
    [[nodiscard]] auto can_undo() const -> bool;
    [[nodiscard]] auto can_redo() const -> bool;
    void push(const std::shared_ptr<IOperation>& operation);
    void undo();
    void redo();

private:
    std::unique_ptr<Operation_stack_impl> m_impl;
};

extern Operation_stack* g_operation_stack;

} // namespace editor
