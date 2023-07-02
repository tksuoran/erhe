#pragma once

#include "time.hpp"
#include "erhe/imgui/imgui_window.hpp"
#include "erhe/net/client.hpp"
#include "erhe/net/server.hpp"

#include <memory>
#include <mutex>
#include <vector>

namespace erhe::imgui {
    class Imgui_windows;
}

namespace editor
{

class Editor_context;

class Network_window
    : public erhe::imgui::Imgui_window
    , public Update_once_per_frame
{
public:
    Network_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context,
        Time&                        time
    );

    // Implements Imgui_window
    void imgui() override;

    // Implements Update_once_per_frame
    void update_once_per_frame(const Time_context&) override;

private:
    void update_network();

    Editor_context&          m_context;
    erhe::net::Client        m_client;
    erhe::net::Server        m_server;

    // Network client
    std::string              m_upstream_address;
    int                      m_upstream_port{0};
    std::vector<std::string> m_upstream_messages;

    // Network server
    std::string              m_downstream_address;
    int                      m_downstream_port{0};
    std::vector<std::string> m_downstream_messages;
};

} // namespace editor

