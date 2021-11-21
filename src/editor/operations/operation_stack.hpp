#pragma once

#include "windows/imgui_window.hpp"

#include "erhe/components/component.hpp"

#include <memory>

namespace editor
{

class IOperation;

class Operation_stack
    : public erhe::components::Component
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name {"Operation_stack"};
    static constexpr std::string_view c_title{"Operations"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Operation_stack ();
    ~Operation_stack() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void initialize_component() override;

    void push    (std::shared_ptr<IOperation> operation);
    void undo    ();
    void redo    ();
    auto can_undo() const -> bool
    {
        return !m_executed.empty();
    }
    auto can_redo() const -> bool
    {
        return !m_undone.empty();
    }

    // Implements Window
    void imgui(Pointer_context& pointer_context) override;

private:
    void imgui(
        const char*                                     stack_label,
        const std::vector<std::shared_ptr<IOperation>>& operations
    );

    std::vector<std::shared_ptr<IOperation>> m_executed;
    std::vector<std::shared_ptr<IOperation>> m_undone;
};

}
