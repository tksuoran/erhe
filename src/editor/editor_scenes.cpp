#include "editor_scenes.hpp"
#include "scene/scene_root.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/net/client.hpp"
#include "erhe/net/server.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/verify.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

namespace editor
{

IEditor_scenes::~IEditor_scenes() noexcept = default;

class Editor_scenes_impl
    : public IEditor_scenes
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_title{"Network"};

    Editor_scenes_impl()
        : erhe::application::Imgui_window{c_title}
    {
        ERHE_VERIFY(g_editor_scenes == nullptr);

        erhe::application::g_imgui_windows->register_imgui_window(this, "network");

        auto ini = erhe::application::get_ini("erhe.ini", "network");
        ini->get("upstream_address",   m_upstream_address);
        ini->get("upstream_port",      m_upstream_port);
        ini->get("downstream_address", m_downstream_address);
        ini->get("downstream_port",    m_downstream_port);

        m_client.set_receive_handler(
            [this](const uint8_t* data, const std::size_t length)
            {
                m_upstream_messages.push_back("received " + std::string{reinterpret_cast<const char*>(data), length});
            }
        );

        m_server.set_receive_handler(
            [this](const uint8_t* data, const std::size_t length)
            {
                m_downstream_messages.push_back("received " + std::string{reinterpret_cast<const char*>(data), length});
            }
        );

        g_editor_scenes = this;
    }
    ~Editor_scenes_impl() noexcept override
    {
        ERHE_VERIFY(g_editor_scenes == this);
        g_editor_scenes = nullptr;
    }

    // Implements Window
    void imgui() override;

    void register_scene_root   (const std::shared_ptr<Scene_root>& scene_root) override;
    void update_node_transforms() override;
    void update_network        () override;
    void sanity_check          () override;

    [[nodiscard]] auto get_scene_roots() -> const std::vector<std::shared_ptr<Scene_root>>& override;
    [[nodiscard]] auto scene_combo(
        const char*                  label,
        std::shared_ptr<Scene_root>& in_out_selected_entry,
        const bool                   empty_option
    ) const -> bool override;

private:
    std::mutex                               m_mutex;
    erhe::net::Client                        m_client;
    erhe::net::Server                        m_server;
    std::vector<std::shared_ptr<Scene_root>> m_scene_roots;

    // Network client
    std::string              m_upstream_address;
    int                      m_upstream_port{0};
    std::vector<std::string> m_upstream_messages;

    // Network server
    std::string              m_downstream_address;
    int                      m_downstream_port{0};
    std::vector<std::string> m_downstream_messages;
};

IEditor_scenes* g_editor_scenes{nullptr};

Editor_scenes::Editor_scenes()
    : Component{c_type_name}
{
}

Editor_scenes::~Editor_scenes() noexcept
{
    ERHE_VERIFY(g_editor_scenes == nullptr);
}

void Editor_scenes::deinitialize_component()
{
    m_impl.reset();
}

void Editor_scenes::initialize_component()
{
    m_impl = std::make_unique<Editor_scenes_impl>();
}

void Editor_scenes_impl::register_scene_root(
    const std::shared_ptr<Scene_root>& scene_root
)
{
    std::lock_guard<std::mutex> lock{m_mutex};

    m_scene_roots.push_back(scene_root);
}

void Editor_scenes_impl::update_node_transforms()
{
    for (const auto& scene_root : m_scene_roots) {
        scene_root->scene().update_node_transforms();
    }
}

void Editor_scenes_impl::update_network()
{
    if (m_server.get_state() != erhe::net::Socket::State::CLOSED) {
        m_server.poll(0);
    }
    if (m_client.get_state() != erhe::net::Socket::State::CLOSED) {
        m_client.poll(0);
    }
}

void Editor_scenes_impl::imgui()
{
    using namespace erhe::net;

    ImGui::PushID("Network");
    {
        ImGui::PushID("Client");
        ImGui::Text     ("Client");
        ImGui::InputText("Upstream Address", &m_upstream_address);
        ImGui::DragInt  ("Upstream Port",    &m_upstream_port, 0.1f, 1, 65535);
        ImGui::Text     ("State: %s", c_str(m_client.get_state()));
        const bool is_closed    = m_client.get_state() == Socket::State::CLOSED;
        const bool is_connected = m_client.get_state() == Socket::State::CONNECTED;
        if (is_closed) {
            if (ImGui::Button("Connect")) {
                m_client.connect(m_upstream_address.c_str(), m_upstream_port);
            }
        } else {
            if (is_connected) {
                std::string message;
                if (ImGui::InputText("Say", &message, ImGuiInputTextFlags_EnterReturnsTrue)) {
                    m_upstream_messages.push_back("sent " + message);
                    m_client.send(message);
                }
            }
            if (ImGui::Button("Disconnect")) {
                m_client.disconnect();
            }
            for (const auto& message : m_upstream_messages) {
                ImGui::TextUnformatted(message.c_str());
            }
        }
        ImGui::PopID();
    }

    {
        ImGui::PushID("Server");
        ImGui::Text     ("Server");
        ImGui::InputText("Downstream Address", &m_downstream_address);
        ImGui::DragInt  ("Downstream Port",    &m_downstream_port, 0.1f, 1, 65535);
        ImGui::Text     ("State: %s",         c_str(m_server.get_state()));
        ImGui::Text     ("Clients: %d",       static_cast<int>(m_server.get_client_count()));
        const bool is_stopped  = m_server.get_state() == Socket::State::CLOSED;
        const bool has_clients = m_server.get_client_count() > 0;
        if (is_stopped) {
            if (ImGui::Button("Start")) {
                m_server.listen(m_downstream_address.c_str(), m_downstream_port);
            }
        } else {
            if (has_clients) {
                std::string message;
                if (ImGui::InputText("Say", &message, ImGuiInputTextFlags_EnterReturnsTrue)) {
                    m_downstream_messages.push_back("sent " + message);
                    m_server.broadcast(message);
                }
            }
            if (ImGui::Button("Stop")) {
                m_server.disconnect();
            }
            for (const auto& message : m_downstream_messages) {
                ImGui::TextUnformatted(message.c_str());
            }
        }
        ImGui::PopID();
    }
    ImGui::PopID();
}

[[nodiscard]] auto Editor_scenes_impl::get_scene_roots() -> const std::vector<std::shared_ptr<Scene_root>>&
{
    return m_scene_roots;
}

void Editor_scenes_impl::sanity_check()
{
#if !defined(NDEBUG)
    for (const auto& scene_root : m_scene_roots) {
        scene_root->sanity_check();
    }
#endif
}

auto Editor_scenes_impl::scene_combo(
    const char*                  label,
    std::shared_ptr<Scene_root>& in_out_selected_entry,
    const bool                   empty_option
) const -> bool
{
    int selection_index = 0;
    int index = 0;
    std::vector<const char*> names;
    std::vector<std::shared_ptr<Scene_root>> entries;
    const bool empty_entry = empty_option || (!in_out_selected_entry);
    if (empty_entry) {
        names.push_back("(none)");
        entries.push_back({});
        ++index;
    }
    for (const auto& entry : m_scene_roots) {
        names.push_back(entry->get_name().c_str());
        entries.push_back(entry);
        if (in_out_selected_entry == entry) {
            selection_index = index;
        }
        ++index;
    }

    // TODO Move to begin / end combo
    const bool selection_changed =
        ImGui::Combo(
            label,
            &selection_index,
            names.data(),
            static_cast<int>(names.size())
        ) &&
        (in_out_selected_entry != entries.at(selection_index));
    if (selection_changed) {
        in_out_selected_entry = entries.at(selection_index);
    }
    return selection_changed;
}

} // namespace hextiles

