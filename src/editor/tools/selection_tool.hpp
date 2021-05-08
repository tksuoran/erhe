#pragma once

#include "tools/tool.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <memory>
#include <mutex>
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
    , public Window
{
public:
    static constexpr const char* c_name = "Selection_tool";
    Selection_tool() : erhe::components::Component(c_name) {}
    virtual ~Selection_tool() = default;

    // Implements Component
    void connect() override;

    // Implements Tool
    auto update(Pointer_context& pointer_context) -> bool override;
    void render(Render_context& render_context) override;
    auto state() const -> State override;
    void cancel_ready() override;
    auto description() -> const char* override { return c_name; }

    // Implements Window
    void window(Pointer_context& pointer_context) override;

    using Mesh_collection           = std::vector<std::shared_ptr<erhe::scene::Mesh>>;
    using On_mesh_selection_changed = std::function<void(const Mesh_collection&)>;

    class Subcription
    {
    public:
        Subcription(Selection_tool* tool, int handle)
            : m_tool  {tool}
            , m_handle{handle}
        {
        }

        ~Subcription()
        {
            if ((m_tool != nullptr) && (m_handle != 0))
            {
                m_tool->unsubscribe_mesh_selection_change_notification(m_handle);
            }
        }

        Subcription(const Subcription&) = delete;
        Subcription& operator=(const Subcription&) = delete;

        Subcription(Subcription&& other) noexcept
        {
            m_tool         = other.m_tool;
            m_handle       = other.m_handle;
            other.m_tool   = nullptr;
            other.m_handle = 0;
        }

        Subcription& operator=(Subcription&& other) noexcept
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
    Subcription subscribe_mesh_selection_change_notification(On_mesh_selection_changed callback);
    void unsubscribe_mesh_selection_change_notification(int handle);

    const Mesh_collection& selected_meshes() { return m_selected_meshes; }

private:
    void call_mesh_selection_change_subscriptions();
    void clear_mesh_selection();
    void toggle_mesh_selection(std::shared_ptr<erhe::scene::Mesh> mesh, bool clear_others);

    struct Subscription_entry
    {
        On_mesh_selection_changed callback;
        int                       handle;
    };

    State                           m_state{State::passive};
    int                             m_next_mesh_selection_change_subscription{1};
    std::mutex                      m_mutex;
    Mesh_collection                 m_selected_meshes;
    std::vector<Subscription_entry> m_mesh_selection_change_subscriptions;
    std::shared_ptr<Scene_manager>  m_scene_manager;

    std::shared_ptr<erhe::scene::Mesh> m_hover_mesh;
    glm::vec3                          m_hover_position{0.0f, 0.0f, 0.0f};
    bool                               m_hover_content {false};
    bool                               m_hover_tool    {false};
};

} // namespace editor
