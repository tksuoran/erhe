#pragma once

#include "editor_message.hpp"
#include "tools/tool.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/message_bus/message_bus.hpp"
#include "erhe/scene/node.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace erhe::scene
{
    class Mesh;
    class Scene;
}

namespace editor
{

class Selection_tool_delete_command
    : public erhe::application::Command
{
public:
    Selection_tool_delete_command();
    auto try_call() -> bool override;
};

class Selection_tool_select_command
    : public erhe::application::Command
{
public:
    Selection_tool_select_command();
    void try_ready() override;
    auto try_call () -> bool override;
};

class Selection_tool_select_toggle_command
    : public erhe::application::Command
{
public:
    Selection_tool_select_toggle_command();
    void try_ready() override;
    auto try_call () -> bool override;
};

class Range_selection
{
public:
    Range_selection();

    void set_terminator(const std::shared_ptr<erhe::scene::Item>& item);
    void entry         (const std::shared_ptr<erhe::scene::Item>& item, bool attachments_expanded);
    void begin         ();
    void end           (bool attachments_expanded);
    void reset         ();

private:
    std::shared_ptr<erhe::scene::Item>              m_primary_terminator;
    std::shared_ptr<erhe::scene::Item>              m_secondary_terminator;
    bool                                            m_edited{false};
    std::vector<std::shared_ptr<erhe::scene::Item>> m_entries;
};

class Selection_tool
    : public erhe::application::Imgui_window
    , public erhe::components::Component
    , public Tool
{
public:
    static constexpr int              c_priority {3};
    static constexpr std::string_view c_type_name{"Selection_tool"};
    static constexpr std::string_view c_title    {"Selection tool"};
    static constexpr uint32_t         c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Selection_tool ();
    ~Selection_tool() noexcept override;

    // Implements Component
    auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    // Implements Tool
    void handle_priority_update(int old_priority, int new_priority) override;

    // Implements Imgui_window
    void imgui() override;

    // Public API
    void viewport_toolbar(bool& hovered);
    [[nodiscard]] auto selection               () const -> const std::vector<std::shared_ptr<erhe::scene::Item>>&;
    [[nodiscard]] auto is_in_selection         (const std::shared_ptr<erhe::scene::Item>& item) const -> bool;
    [[nodiscard]] auto range_selection         () -> Range_selection&;
    [[nodiscard]] auto get_first_selected_node () -> std::shared_ptr<erhe::scene::Node>;
    [[nodiscard]] auto get_first_selected_scene() -> std::shared_ptr<erhe::scene::Scene>;
    void set_selection                   (const std::vector<std::shared_ptr<erhe::scene::Item>>& selection);
    auto add_to_selection                (const std::shared_ptr<erhe::scene::Item>& item) -> bool;
    auto clear_selection                 () -> bool;
    auto remove_from_selection           (const std::shared_ptr<erhe::scene::Item>& item) -> bool;
    void update_selection_from_scene_item(const std::shared_ptr<erhe::scene::Item>& item, const bool added);
    void sanity_check                    ();

    // Commands
    auto on_select_try_ready() -> bool;
    auto on_select          () -> bool;
    auto on_select_toggle   () -> bool;

    auto delete_selection() -> bool;

private:
    void send_selection_change_message() const;
    void toggle_mesh_selection(
        const std::shared_ptr<erhe::scene::Mesh>& mesh,
        bool                                      was_selected,
        bool                                      clear_others
    );

    Selection_tool_select_command        m_select_command;
    Selection_tool_select_toggle_command m_select_toggle_command;
    Selection_tool_delete_command        m_delete_command;

    std::vector<std::shared_ptr<erhe::scene::Item>> m_selection;
    Range_selection                                 m_range_selection;
    std::shared_ptr<erhe::scene::Mesh>              m_hover_mesh;
    bool                                            m_hover_content{false};
    bool                                            m_hover_tool   {false};
};

extern Selection_tool* g_selection_tool;

} // namespace editor
