#pragma once

#include "erhe_net/ring_buffer.hpp"
#include "erhe_net/net_os.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace erhe::net
{

using Receive_handler = std::function<void(const uint8_t* data, std::size_t length)>;

class Select_sockets;

class Socket
{
public:
    enum class State : int {
        CLOSED            = 0,
        SERVER_LISTENING  = 1,
        CLIENT_CONNECTING = 2,
        CONNECTED         = 3
    };

    Socket();
    Socket(
        const SOCKET&      socket,
        const sockaddr_in& address_in
    );
    ~Socket();
    Socket(const Socket&) = delete;
    void operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    auto operator=(Socket&& other) noexcept -> Socket&;

    auto get_state           () const -> State                 { return m_state; }
    void set_receive_handler (Receive_handler receive_handler) { m_receive_handler = receive_handler; }
    auto get_socket          () const -> SOCKET                { return m_socket; }
    auto get_sockaddr_in     () const -> const sockaddr_in&    { return m_address_in; }
    auto get_address_string  () const -> const std::string&    { return m_address; }
    auto get_send_buffer_size() const -> size_t                { return m_send_buffer ? m_send_buffer->size() : 0; }
    auto send                (const char* data, int length) -> bool;
    auto send_pending        () -> bool;
    auto recv                () -> bool;
    auto get_receive_buffer  () -> Ring_buffer* { return m_receive_buffer.get(); }
    void close               ();
    auto has_pending_writes  () -> bool         { return m_send_buffer ? !m_send_buffer->empty() : false; }

    void pre_select           (Select_sockets& select_sockets);
    auto post_select_send_recv(Select_sockets& select_sockets) -> bool;
    auto post_select_connect  (Select_sockets& select_sockets) -> bool;
    auto post_select_listen   (Select_sockets& select_sockets) -> std::optional<Socket>;

    auto connect(const char* address, int port) -> bool; // for client
    auto bind   (const char* address, int port) -> bool; // for server

private:
    auto connect              () -> bool;
    void set_state            (State state);
    void on_state_changed     (State old_state, State new_state);
    auto receive_packet_length() -> uint32_t;

    SOCKET                       m_socket    {INVALID_SOCKET};
    sockaddr_in                  m_address_in{};
    addrinfo*                    m_addr_info {nullptr};
    std::string                  m_address;
    State                        m_state     {State::CLOSED};
    std::unique_ptr<Ring_buffer> m_send_buffer;
    std::unique_ptr<Ring_buffer> m_receive_buffer;
    Receive_handler              m_receive_handler;
};

auto c_str(const Socket::State state) -> const char*;

}
