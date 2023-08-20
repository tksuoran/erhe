#include "erhe_net/client.hpp"
#include "erhe_net/net_log.hpp"
#include "erhe_net/net_os.hpp"
#include "erhe_net/select_sockets.hpp"


namespace erhe::net
{

Client::Client() = default;

Client::~Client()
{
    log_client->trace("Client destructor");
}

Client::Client(Client&& other) noexcept
    : m_socket{std::move(other.m_socket)}
{
    log_client->trace("Client move constructor");
}

auto Client::operator=(Client&& other) noexcept -> Client&
{
    log_client->trace("Client move assignment");
    m_socket = std::move(other.m_socket);
    return *this;
}

auto Client::connect(const char* address, const int port) -> bool
{
    return m_socket.connect(address, port);
}

void Client::disconnect()
{
    log_socket->trace("Socket move assignment");
    m_socket.close();
}

auto Client::poll(const int timeout_ms) -> bool
{
    if (m_socket.get_state() == Socket::State::CLOSED) {
        return true; // NOP
    }
    Select_sockets select_sockets;

    m_socket.pre_select(select_sockets);

    // Call select() to find out if there is work to do
    const int select_res = select_sockets.select(timeout_ms);
    if (select_res == SOCKET_ERROR) {
        log_client->trace("client select() returned error {}", get_net_last_error_message());
        return false; // TODO
    }
    if (select_res == 0) {
        return true; // NOP
    }

    switch (m_socket.get_state()) {
        case Socket::State::CLIENT_CONNECTING: {
            m_socket.post_select_connect(select_sockets);
            break;
        }
        case Socket::State::CONNECTED: {
            m_socket.post_select_send_recv(select_sockets);
            break;
        }
        default: {
            //log_client->info("client socket is not connecting nor connected");
        }
    }

    return true;
}

auto Client::send(const std::string& message) -> bool
{
    return m_socket.send(
        message.data(),
        static_cast<int>(message.size())
    );
}

void Client::set_receive_handler(Receive_handler receive_handler)
{
    m_socket.set_receive_handler(receive_handler);
}

auto Client::get_state() -> Socket::State
{
    return m_socket.get_state();
}

}
