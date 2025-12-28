#include "erhe_net/server.hpp"
#include "erhe_net/net_log.hpp"
#include "erhe_net/select_sockets.hpp"

#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace erhe::net {

Server::Server() = default;

Server::~Server() noexcept
{
    log_server->trace("Server destructor");
}

Server::Server(Server&& other) noexcept
    : m_listen_socket  {std::move(other.m_listen_socket)}
    , m_receive_handler{std::move(other.m_receive_handler)}
    , m_clients        {std::move(other.m_clients)}
{
    log_server->trace("Server move constructor");
}

auto Server::operator=(Server&& other) noexcept -> Server&
{
    log_server->trace("Server move assignment");
    m_listen_socket   = std::move(other.m_listen_socket);
    m_receive_handler = std::move(other.m_receive_handler);
    m_clients         = std::move(other.m_clients);
    return *this;
}

auto Server::listen(const char* address, const int port) -> bool
{
    return m_listen_socket.bind(address, port);
}

auto Server::poll(const int timeout_ms) -> bool
{
    if (m_listen_socket.get_state() == Socket::State::CLOSED) {
        return true; // NOP
    }

    Select_sockets select_sockets;

    // Collect fds for select
    m_listen_socket.pre_select(select_sockets);
    for (auto& client : m_clients) {
        client.pre_select(select_sockets);
    }

    // Call select() to find out if there is work to do
    const int select_res = select_sockets.select(timeout_ms);
    if (select_res == SOCKET_ERROR) {
        log_net->trace("server select() returned error {}", get_net_last_error_message());
        return false; // TODO
    }

    // Perform send and receive for client sockets, collect closed sockets
    for (auto& client : m_clients) {
        client.post_select_send_recv(select_sockets);
    }

    // Remove closed sockets
    m_clients.erase(
        std::remove_if(
            m_clients.begin(),
            m_clients.end(),
            [](Socket& client) {
                return client.get_state() == Socket::State::CLOSED;
            }
        ),
        m_clients.end()
    );

    // Check for new clients
    auto new_socket = m_listen_socket.post_select_listen(select_sockets);
    if (new_socket.has_value()) {
        log_net->info("new client is connecting to server");
        new_socket.value().set_receive_handler(m_receive_handler);
        m_clients.push_back(std::move(new_socket.value()));
    }

    return true;
}

auto Server::broadcast(const std::string& message) -> bool
{
    std::size_t error_count = 0;
    for (auto& client : m_clients) {
        if (!client.send(message.data(), static_cast<int>(message.length()))) {
            ++error_count;
        }
    }
    return error_count == 0;
}

void Server::set_receive_handler(Receive_handler receive_handler)
{
    m_receive_handler = receive_handler;
}

void Server::disconnect()
{
    m_listen_socket.close();
    m_clients.clear();
}

auto Server::get_state() const -> Socket::State
{
    return m_listen_socket.get_state();
}

auto Server::get_client_count() const -> std::size_t
{
    return m_clients.size();
}

} // namespace erhe::net
