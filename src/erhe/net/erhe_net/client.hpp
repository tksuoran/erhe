#pragma once

#include "erhe_net/socket.hpp"

namespace erhe::net {

class Client
{
public:
    Client();
    ~Client();
    Client(Client&) = delete;
    auto operator=(const Client&) = delete;
    Client(Client&& other) noexcept;
    auto operator=(Client&& other) noexcept -> Client&;

    auto connect            (const char* address, int port) -> bool;
    void disconnect         ();
    auto send               (const std::string& message) -> bool;
    void set_receive_handler(Receive_handler receive_handler);
    auto poll               (int timeout_ms) -> bool;
    auto get_state          () -> Socket::State;

private:
    Socket m_socket;
};

} // namespace erhe::net
