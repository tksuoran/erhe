#pragma once

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <memory>
#include <mutex>

namespace editor
{

class Mesh_memory;
class Operation_stack;
class Selection_tool;
class Tool;

class Operations
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"Operations"};
    static constexpr std::string_view c_title{"Operations"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Operations ();
    ~Operations() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Window
    void imgui() override;

    // Public API
    // TODO XXX FIXME Move to erhe::application::View
    [[nodiscard]] auto get_active_tool() const -> Tool*;
    void register_active_tool(Tool* tool);

private:
    [[nodiscard]]auto count_selected_meshes() const -> size_t;

    // Component dependencies
    std::shared_ptr<Mesh_memory>     m_mesh_memory;
    std::shared_ptr<Operation_stack> m_operation_stack;
    std::shared_ptr<Selection_tool>  m_selection_tool;

    Tool*              m_current_active_tool{nullptr};
    std::mutex         m_mutex;
    std::vector<Tool*> m_active_tools;
};

} // namespace editor
