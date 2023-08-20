#pragma once

#include "erhe_net/socket.hpp"

namespace erhe::net
{

class Server
{
public:
    Server();
    ~Server();
    Server(Server&) = delete;
    auto operator=(const Server&) = delete;
    Server(Server&& other) noexcept;
    auto operator=(Server&& other) noexcept -> Server&;

    auto broadcast          (const std::string& message) -> bool;
    void set_receive_handler(Receive_handler receive_handler);
    void disconnect         ();
    auto listen             (const char* address, int port) -> bool;
    auto poll               (int timeout_ms) -> bool;
    auto get_state          () const -> Socket::State;
    auto get_client_count   () const -> std::size_t;

private:
    Socket              m_listen_socket;
    Receive_handler     m_receive_handler;
    std::vector<Socket> m_clients;
};

}
