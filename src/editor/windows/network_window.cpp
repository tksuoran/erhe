#include "windows/network_window.hpp"

#include "editor_context.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_net/client.hpp"
#include "erhe_net/server.hpp"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace editor
{

Network_window::Network_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context,
    Time&                        time
)
    : Update_time_base         {time}
    , erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Network", "network"}
    , m_context                {editor_context}
{
    auto ini = erhe::configuration::get_ini("erhe.ini", "network");
    ini->get("upstream_address",   m_upstream_address);
    ini->get("upstream_port",      m_upstream_port);
    ini->get("downstream_address", m_downstream_address);
    ini->get("downstream_port",    m_downstream_port);

    m_client.set_receive_handler(
        [this](const uint8_t* data, const std::size_t length) {
            m_upstream_messages.push_back("received " + std::string{reinterpret_cast<const char*>(data), length});
        }
    );

    m_server.set_receive_handler(
        [this](const uint8_t* data, const std::size_t length) {
            m_downstream_messages.push_back("received " + std::string{reinterpret_cast<const char*>(data), length});
        }
    );

    set_developer();
}

void Network_window::update_once_per_frame(const Time_context&)
{
    update_network();
}

void Network_window::update_network()
{
    if (m_server.get_state() != erhe::net::Socket::State::CLOSED) {
        m_server.poll(0);
    }
    if (m_client.get_state() != erhe::net::Socket::State::CLOSED) {
        m_client.poll(0);
    }
}

void Network_window::imgui()
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

} // namespace editor

