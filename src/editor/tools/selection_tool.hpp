#pragma once

#include "tools/tool.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/windows/imgui_window.hpp"
#include "erhe/components/components.hpp"
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

class Editor_scenes;
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

    void set_terminator(const std::shared_ptr<erhe::scene::Node>& node);
    void entry         (const std::shared_ptr<erhe::scene::Node>& node);
    void begin         ();
    void end           ();
    void reset         ();

private:
    Selection_tool&                                 m_selection_tool;
    std::shared_ptr<erhe::scene::Node>              m_primary_terminator;
    std::shared_ptr<erhe::scene::Node>              m_secondary_terminator;
    bool                                            m_edited{false};
    std::vector<std::shared_ptr<erhe::scene::Node>> m_entries;
};

class Selection_tool
    : public erhe::components::Component
    , public Tool
{
public:
    using Selection            = std::vector<std::shared_ptr<erhe::scene::Node>>;
    using On_selection_changed = std::function<void(const Selection&)>;

    class Subcription final
    {
    public:
        Subcription(Selection_tool* tool, const int handle)
            : m_tool  {tool}
            , m_handle{handle}
        {
        }

        ~Subcription() noexcept
        {
            if (
                (m_tool != nullptr) &&
                (m_handle != 0)
            )
            {
                m_tool->unsubscribe_selection_change_notification(m_handle);
            }
        }

        Subcription   (const Subcription&) = delete;
        void operator=(const Subcription&) = delete;

        Subcription(Subcription&& other) noexcept
        {
            m_tool         = other.m_tool;
            m_handle       = other.m_handle;
            other.m_tool   = nullptr;
            other.m_handle = 0;
        }

        auto operator=(Subcription&& other) noexcept -> Subcription&
        {
            m_tool         = other.m_tool;
            m_handle       = other.m_handle;
            other.m_tool   = nullptr;
            other.m_handle = 0;
            return *this;
        }

    private:
        Selection_tool* m_tool  {nullptr};
        int             m_handle{0};
    };

    static constexpr int              c_priority{3};
    static constexpr std::string_view c_label   {"Selection_tool"};
    static constexpr std::string_view c_title   {"Selection tool"};
    static constexpr uint32_t         hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Selection_tool ();
    ~Selection_tool() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Tool
    [[nodiscard]] auto tool_priority() const -> int   override { return c_priority; }
    [[nodiscard]] auto description  () -> const char* override;

    // Public API
    [[nodiscard]] auto subscribe_selection_change_notification(On_selection_changed callback) -> Subcription;
    [[nodiscard]] auto selection                              () const -> const Selection&;
    [[nodiscard]] auto is_in_selection                        (const std::shared_ptr<erhe::scene::Node>& item) const -> bool;
    [[nodiscard]] auto range_selection                        () -> Range_selection&;
    void unsubscribe_selection_change_notification(int handle);
    void set_selection             (const Selection& selection);
    auto add_to_selection          (const std::shared_ptr<erhe::scene::Node>& item) -> bool;
    auto clear_selection           () -> bool;
    auto remove_from_selection     (const std::shared_ptr<erhe::scene::Node>& item) -> bool;
    void update_selection_from_node(const std::shared_ptr<erhe::scene::Node>& node, const bool added);
    void sanity_check              ();

    // Commands
    auto mouse_select_try_ready() -> bool;
    auto on_mouse_select       () -> bool;

    auto delete_selection() -> bool;

private:
    void call_selection_change_subscriptions() const;
    void toggle_selection(
        const std::shared_ptr<erhe::scene::Node>& item,
        const bool clear_others
    );

    class Subscription_entry
    {
    public:
        On_selection_changed callback;
        int                  handle;
    };

    Selection_tool_select_command m_select_command;
    Selection_tool_delete_command m_delete_command;

    Range_selection m_range_selection;

    // Component dependencies
    std::shared_ptr<erhe::application::Line_renderer_set> m_line_renderer_set;
    std::shared_ptr<Editor_scenes>     m_editor_scenes;
    std::shared_ptr<Viewport_windows>  m_viewport_windows;
    std::shared_ptr<Viewport_config>   m_viewport_config;

    int                                m_next_selection_change_subscription{1};
    Selection                          m_selection;
    std::vector<Subscription_entry>    m_selection_change_subscriptions;

    std::shared_ptr<erhe::scene::Mesh> m_hover_mesh;
    bool                               m_hover_content{false};
    bool                               m_hover_tool   {false};
};

} // namespace editor
