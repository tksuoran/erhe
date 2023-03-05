#include "erhe/net/client.hpp"
#include "erhe/net/net_log.hpp"
#include "erhe/net/net_os.hpp"
#include "erhe/net/select_sockets.hpp"


namespace erhe::net
{

auto Client::connect(const char* address, const int port) -> bool
{
    return m_socket.connect(address, port);
}

void Client::disconnect()
{
    m_socket.close();
}

auto Client::poll(const int timeout_ms) -> bool
{
    Select_sockets select_sockets;

    m_socket.pre_select(select_sockets);

    // Call select() to find out if there is work to do
    const int select_res = select_sockets.select(timeout_ms);
    if (select_res == SOCKET_ERROR) {
        const int  error_code = get_net_last_error();
        const auto message    = get_net_error_string(error_code);
        log_client->trace("client select() returned error {} - {}", error_code, message);
        return false; // TODO
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
