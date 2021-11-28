#pragma once

#include "tools/tool.hpp"
#include "windows/imgui_window.hpp"

#include "erhe/scene/node.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace erhe::scene {
    class Mesh;
}

namespace editor
{

class Scene_manager;

class Selection_tool
    : public erhe::components::Component
    , public Tool
{
public:
    static constexpr std::string_view c_name{"Selection_tool"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Selection_tool();
    ~Selection_tool() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Tool
    auto update      (Pointer_context& pointer_context) -> bool override;
    void render      (const Render_context& render_context)     override;
    auto state       () const -> State                          override;
    void cancel_ready()                                         override;
    auto description () -> const char*                          override;

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

        ~Subcription()
        {
            if ((m_tool != nullptr) && (m_handle != 0))
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

    auto subscribe_selection_change_notification(On_selection_changed callback) -> Subcription;

    void unsubscribe_selection_change_notification(int handle);

    auto selection() const -> const Selection&
    {
        return m_selection;
    }
    auto set_selection(const Selection& selection)
    {
        m_selection = selection;
        call_selection_change_subscriptions();
    }
    auto clear_selection      () -> bool;
    auto is_in_selection      (const std::shared_ptr<erhe::scene::Node>& item) const -> bool;
    auto add_to_selection     (const std::shared_ptr<erhe::scene::Node>& item) -> bool;
    auto remove_from_selection(const std::shared_ptr<erhe::scene::Node>& item) -> bool;

private:
    void call_selection_change_subscriptions();
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

    State                           m_state{State::Passive};
    int                             m_next_selection_change_subscription{1};
    Selection                       m_selection;
    std::vector<Subscription_entry> m_selection_change_subscriptions;

    Scene_manager*                     m_scene_manager{nullptr};

    std::shared_ptr<erhe::scene::Mesh> m_hover_mesh;
    glm::vec3                          m_hover_position{0.0f, 0.0f, 0.0f};
    bool                               m_hover_content {false};
    bool                               m_hover_tool    {false};
};

} // namespace editor
