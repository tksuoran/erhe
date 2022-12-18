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

namespace erhe::application
{
    class Line_renderer_set;
}

namespace editor
{

class Editor_message_bus;
class Editor_scenes;
class Headset_view;
class Node_tree_window;
class Scene_root;
class Selection_tool;
class Viewport_config;
class Viewport_windows;

class Selection_tool_delete_command
    : public erhe::application::Command
{
public:
    explicit Selection_tool_delete_command(Selection_tool& selection_tool)
        : Command         {"Selection_tool.delete"}
        , m_selection_tool{selection_tool}
    {
    }

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Selection_tool& m_selection_tool;
};

class Selection_tool_select_command
    : public erhe::application::Command
{
public:
    explicit Selection_tool_select_command(Selection_tool& selection_tool)
        : Command         {"Selection_tool.select"}
        , m_selection_tool{selection_tool}
    {
    }

    void try_ready(erhe::application::Command_context& context) override;
    auto try_call (erhe::application::Command_context& context) -> bool override;

private:
    Selection_tool& m_selection_tool;
};

class Range_selection
{
public:
    explicit Range_selection(Selection_tool& selection_tool);

    void set_terminator(const std::shared_ptr<erhe::scene::Item>& item);
    void entry         (const std::shared_ptr<erhe::scene::Item>& item, bool attachments_expanded);
    void begin         ();
    void end           (bool attachments_expanded);
    void reset         ();

private:
    Selection_tool&                                 m_selection_tool;
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
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Tool
    [[nodiscard]] auto tool_priority() const -> int   override { return c_priority; }
    [[nodiscard]] auto description  () -> const char* override;
    void on_inactived() override;

    // Implements Imgui_window
    void imgui() override;

    // Public API
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

    auto delete_selection() -> bool;

private:
    void send_selection_change_message() const;
    void toggle_mesh_selection(
        const std::shared_ptr<erhe::scene::Mesh>& mesh,
        bool                                      was_selected,
        bool                                      clear_others
    );

    Selection_tool_select_command m_select_command;
    Selection_tool_delete_command m_delete_command;

    // Component dependencies
    std::shared_ptr<erhe::application::Line_renderer_set> m_line_renderer_set;
    std::shared_ptr<Editor_message_bus> m_editor_message_bus;
    std::shared_ptr<Editor_scenes>      m_editor_scenes;
    std::shared_ptr<Headset_view>       m_headset_view;
    std::shared_ptr<Node_tree_window>   m_node_tree_window;
    std::shared_ptr<Viewport_windows>   m_viewport_windows;
    std::shared_ptr<Viewport_config>    m_viewport_config;

    std::vector<std::shared_ptr<erhe::scene::Item>> m_selection;
    Range_selection                                 m_range_selection;

    std::shared_ptr<erhe::scene::Mesh> m_hover_mesh;
    bool                               m_hover_content{false};
    bool                               m_hover_tool   {false};
};

} // namespace editor
