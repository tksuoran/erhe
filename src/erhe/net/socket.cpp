#include "erhe/net/socket.hpp"
#include "erhe/net/net_os.hpp"
#include "erhe/net/net_log.hpp"
#include "erhe/net/select_sockets.hpp"
#include "erhe/toolkit/verify.hpp"

#include <fmt/format.h>


namespace erhe::net
{

//                                            E  r  h  e
constexpr uint32_t erhe_header_magic_u32 = 0x45'72'68'65u;
constexpr int      u32_byte_count        = sizeof(uint32_t);
constexpr int      header_byte_count     = 2 * u32_byte_count;

class Packet_header
{
public:
    Packet_header();
    explicit Packet_header(uint32_t length);

    uint32_t magic {0};
    uint32_t length{0};
};

auto c_str(const Socket::State state) -> const char*
{
    switch (state) {
        case Socket::State::CLOSED           : return "CLOSED";
        case Socket::State::SERVER_LISTENING : return "SERVER_LISTENING";
        case Socket::State::CLIENT_CONNECTING: return "CLIENT_CONNECTING";
        case Socket::State::CONNECTED        : return "CONNECTED";
        default:                               return "?";
    };
}

Packet_header::Packet_header() = default;

Packet_header::Packet_header(const uint32_t length)
    : magic {erhe_header_magic_u32}
    , length{length}
{
}

Socket::Socket()
{
}

Socket::Socket(
    const SOCKET&      socket,
    const sockaddr_in& address_in
)
    : m_socket    {socket}
    , m_address_in{address_in}
{
    set_connected();
    char ipv4_address_string[INET_ADDRSTRLEN]{0};
    PCSTR res = inet_ntop(
        AF_INET,
        (const void*)(&address_in.sin_addr),
        ipv4_address_string,
        INET_ADDRSTRLEN
    );
    if (res == nullptr) {
        log_net->warn("inet_ntop() failed to get address");
        return;
    }
    m_address = std::string{ipv4_address_string};
    log_net->info("socket address: {}", m_address);
}

Socket::~Socket()
{
    close();
}

void Socket::close()
{
    m_state = State::CLOSED;
    if (m_addr_info != nullptr) {
        freeaddrinfo(m_addr_info);
        m_addr_info = nullptr;
    }
    m_send_buffer.reset();
    m_receive_buffer.reset();
    if (m_socket != INVALID_SOCKET) {
        log_net->info("Closing socket");
        shutdown(m_socket, SD_BOTH);
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
}

// Initiate client connecting to server
// Returns false in case of error, true in case of ok
bool Socket::connect(const char* const address, const int port)
{
    ERHE_VERIFY(m_state == State::CLOSED);

    const addrinfo hints{
        .ai_family   = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP
    };

    const std::string port_string = fmt::format("{}", port);
    if (m_addr_info != nullptr) {
        freeaddrinfo(m_addr_info);
    }
    m_addr_info = nullptr;
    const int getaddrinfo_res = getaddrinfo(address, port_string.c_str(), &hints, &m_addr_info);
    if (getaddrinfo_res != 0) {
        const int  error_code = WSAGetLastError();
        const auto message    = get_net_error_string(error_code);
        log_net->error("getaddrinfo('{}', '{}') failed with error {} - {}", address, port_string, error_code, message);
        return false;
    }
    if (m_addr_info == nullptr) {
        log_net->error("getaddrinfo('{}', '{}') returned nullptr", address, port_string);
        return false;
    }

    // Create a SOCKET for connecting to server
    m_socket = socket(hints.ai_family, hints.ai_socktype, hints.ai_protocol);
    if (m_socket == INVALID_SOCKET) {
        const int  error_code = WSAGetLastError();
        const auto message    = get_net_error_string(error_code);
        log_net->error("socket() failed with error {} - {}", error_code, message);
        return false;
    }

    // Set socket to non-blocking mode
    const bool non_block_ok = set_socket_blocking_enabled(m_socket, false);
    if (!non_block_ok) {
        const int  error_code = WSAGetLastError();
        const auto message    = get_net_error_string(error_code);
        log_net->error("ioctlsocket() failed with error {} - {}", error_code, message);
        return false;
    }

    log_net->info("Connecting to {} port {}", address, port);
    m_state = State::CLIENT_CONNECTING;
    return true;
}

// Initiate server to start listening
// Returns false in case of error, true in case of ok
auto Socket::bind(const char* const address, const int port) -> bool
{
    ERHE_VERIFY(m_state == State::CLOSED);

    addrinfo hints{
        .ai_flags    = AI_PASSIVE, // for server
        .ai_family   = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP
    };

    const std::string port_string = fmt::format("{}", port);
    if (m_addr_info != nullptr)
    {
        freeaddrinfo(m_addr_info);
    }
    m_addr_info = nullptr;
    const int getaddrinfo_res = getaddrinfo(address, port_string.c_str(), &hints, &m_addr_info);
    if (getaddrinfo_res != 0) {
        const int  error_code = WSAGetLastError();
        const auto message    = get_net_error_string(error_code);
        log_net->error("getaddrinfo('{}', '{}') failed with error {} - {}", address, port_string, error_code, message);
        return false;
    }

    // setsockopt() options https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-setsockopt

    m_socket = socket(m_addr_info->ai_family, m_addr_info->ai_socktype, m_addr_info->ai_protocol);
    if (m_socket == INVALID_SOCKET) {
        const int  error_code = WSAGetLastError();
        const auto message    = get_net_error_string(error_code);
        log_net->error("socket() failed with error {} - {}", error_code, message);
        return false;
    }

    const int enable = 1;
    const int reuse_res = setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&enable, sizeof(enable));
    if (reuse_res == SOCKET_ERROR) {
        const int  error_code = WSAGetLastError();
        const auto message    = get_net_error_string(error_code);
        log_net->error("setsockopt(SO_REUSEADDR) failed with error {} - {}", error_code, message);
        return false;
    }

    const int bind_res = ::bind(m_socket, m_addr_info->ai_addr, static_cast<int>(m_addr_info->ai_addrlen));
    if (bind_res == SOCKET_ERROR) {
        const int  error_code = WSAGetLastError();
        const auto message    = get_net_error_string(error_code);
        log_net->error("::bind() failed with error {} - {}", error_code, message);
        return false;
    }

    const bool non_block_ok = set_socket_blocking_enabled(m_socket, false);
    if (!non_block_ok) {
        const int  error_code = WSAGetLastError();
        const auto message    = get_net_error_string(error_code);
        log_net->error("ioctlsocket() failed with error {} - {}", error_code, message);
        return false;
    }

    const int backlog = 8;
    const int listen_res = listen(m_socket, backlog);
    if (listen_res == SOCKET_ERROR) {
        const int  error_code = WSAGetLastError();
        const auto message    = get_net_error_string(error_code);
        log_net->error("listen() failed with error {} - {}", error_code, message);
        return false;
    }

    m_send_buffer    = std::make_unique<Ring_buffer>(4 * 1024 * 1024);
    m_receive_buffer = std::make_unique<Ring_buffer>(4 * 1024 * 1024);

    log_net->info("Listening at {} port {}", address, port);
    m_state = State::SERVER_LISTENING;
    return true;
}

// Attempts to send some or all of the data queued in send buffer.
// Returns true if no error, returns false in case of error.
auto Socket::send_pending() -> bool
{
    ERHE_VERIFY(m_state == State::CONNECTED);
    ERHE_VERIFY(m_send_buffer);

    // Loop (at most) twice, for the case where the ring buffer
    // wraps around, in which case two send() calls are needed.
    for (;;) {
        if (m_send_buffer->empty()) {
            return true;
        }

        std::size_t          can_send_byte_count_before_wrap{0};
        std::size_t          can_send_byte_count_after_wrap {0};
        const uint8_t* const read_pointer    = m_send_buffer->begin_consume(can_send_byte_count_before_wrap, can_send_byte_count_after_wrap);
        const int            send_result     = ::send(m_socket, reinterpret_cast<const char*>(read_pointer), static_cast<int>(can_send_byte_count_before_wrap), 0);
        const int            sent_byte_count = (std::max)(0, send_result);
        if (can_send_byte_count_after_wrap > 0) {
            log_net->info("send() before wrap {} bytes, after wrap {} bytes", can_send_byte_count_before_wrap, can_send_byte_count_after_wrap);
        }
        if (send_result < 0) {
            m_send_buffer->end_consume(0);
            const int error_code = WSAGetLastError();
            if (is_error_fatal(error_code))
            {
                const auto message = get_net_error_string(error_code);
                log_net->error("send({} bytes) failed with error {} - {}", can_send_byte_count_before_wrap, error_code, message);
                close();
                return false;
            }
            return true;
        }
        m_send_buffer->end_consume(sent_byte_count);
        if (
            (sent_byte_count < can_send_byte_count_before_wrap) ||
            (can_send_byte_count_after_wrap == 0)
        ) {
            break;
        }
    }
    return true;
}

// Sends a packet. Returns true if there was no error, false if there was an error.
auto Socket::send(const char* const data, const int length) -> bool
{
    ERHE_VERIFY(m_state == State::CONNECTED);

    // Check if new message fits to send buffer
    std::size_t can_write_count = m_send_buffer->size_available_for_write();
    if (can_write_count < sizeof(Packet_header) + length) {
        // Does not fit? Try to flush queued data
        const auto send_pending_result = send_pending();
        if (!send_pending_result) {
            return false;
        }
        // Check again how much fits
        can_write_count = m_send_buffer->size_available_for_write();
        if (can_write_count < sizeof(Packet_header) + length) {
            m_send_buffer->end_produce(0);
            log_net->warn("message ({} bytes) does not fit to send queue ({} bytes free)", length, can_write_count);
            return false;
        }
    }

    // Write header to send buffer
    const Packet_header header{static_cast<size_t>(length)};
    const auto header_byte_write_count = m_send_buffer->write(reinterpret_cast<const uint8_t*>(&header), sizeof(Packet_header));
    ERHE_VERIFY(header_byte_write_count == sizeof(Packet_header));

    // Write payload to send buffer
    const auto payload_byte_write_count = m_send_buffer->write(reinterpret_cast<const uint8_t*>(data), static_cast<size_t>(length));
    ERHE_VERIFY(payload_byte_write_count == length);

    // Try to send some or all of the queued send buffer
    return send_pending();
}

auto Socket::receive_packet_length() -> uint32_t
{
    ERHE_VERIFY(m_state == State::CONNECTED);

    Packet_header header;
    const std::size_t read_byte_count = m_receive_buffer->peek(
        reinterpret_cast<uint8_t*>(&header),
        sizeof(Packet_header)
    );
    if (read_byte_count < sizeof(Packet_header)) {
        return 0;
    }
    ERHE_VERIFY(header.magic == erhe_header_magic_u32);
    return header.length;
}

// returns false in case of error, true if ok
auto Socket::recv() -> bool
{
    ERHE_VERIFY(m_state == State::CONNECTED);
    ERHE_VERIFY(m_receive_buffer);

    for (;;) {
        if (m_receive_buffer->full()) {
            log_net->warn("receive buffer is full, unable to call recv()");
            return false;
        }

        // See how much space we have in the receive buffer
        std::size_t    can_receive_byte_count_before_wrap{0};
        std::size_t    can_receive_byte_count_after_wrap {0};
        uint8_t* const write_pointer       = m_receive_buffer->begin_produce(can_receive_byte_count_before_wrap, can_receive_byte_count_after_wrap);
        const int      recv_result         = ::recv(m_socket, reinterpret_cast<char*>(write_pointer), static_cast<int>(can_receive_byte_count_before_wrap), 0);
        const int      received_byte_count = (std::max)(0, recv_result);
        if (recv_result < 0) {
            m_receive_buffer->end_produce(0);
            const int error_code = WSAGetLastError();
            if (is_error_fatal(error_code)) {
                const auto message = get_net_error_string(error_code);
                log_net->error("recv() failed with error {} - {}", error_code, message);
                close();
                return false;
            }
            return true; // non-fatal error
        }
        m_receive_buffer->end_produce(received_byte_count);
        if (recv_result == 0) {
            //log_net->info("connection closed");
            //close();
            return true;
        }

        // Process received packets
        for (;;) {
            const uint32_t next_packet_length = receive_packet_length();
            if (next_packet_length == 0) {
                break; // Header not received
            }
            std::size_t    readable_byte_count_before_wrap{0};
            std::size_t    readable_byte_count_after_wrap {0};
            const uint8_t* read_pointer = m_receive_buffer->begin_consume(readable_byte_count_before_wrap, readable_byte_count_after_wrap);
            std::size_t    readable_byte_count_total = readable_byte_count_before_wrap + readable_byte_count_after_wrap;
            if (readable_byte_count_total < sizeof(Packet_header) + next_packet_length) {
                m_receive_buffer->end_consume(0); // Packet not fully received
                break;
            }

            log_net->trace("received message, length = {} bytes", next_packet_length);

            // Ring buffer would wrap? Rotate so that it won't
            if (readable_byte_count_before_wrap < sizeof(Packet_header) + next_packet_length) {
                log_net->trace("message ring buffer wrap - rotating ring buffer");
                m_receive_buffer->end_consume(0);
                m_receive_buffer->rotate();
                readable_byte_count_before_wrap = 0;
                readable_byte_count_after_wrap  = 0;
                read_pointer = m_receive_buffer->begin_consume(readable_byte_count_before_wrap, readable_byte_count_after_wrap);
            }
            ERHE_VERIFY(readable_byte_count_before_wrap >= sizeof(Packet_header) + next_packet_length);
            if (m_receive_handler) {
                log_net->info("calling receive handler");
                m_receive_handler(read_pointer + header_byte_count, next_packet_length);
            } else {
                log_net->warn("no receive handler set, message discarded");
            }
            m_receive_buffer->end_consume(header_byte_count + next_packet_length);
        }

        if (
            (received_byte_count < can_receive_byte_count_before_wrap) ||
            (can_receive_byte_count_after_wrap == 0)
        ) {
            break;
        }
    }
    return true;
}

void Socket::pre_select(Select_sockets& select_sockets)
{
    switch (m_state) {
        case State::CLOSED: {
            return;
        }

        case State::CLIENT_CONNECTING: {
            select_sockets.set_write (m_socket);
            select_sockets.set_except(m_socket); // For connect errors
            return;
        }

        case State::SERVER_LISTENING: {
            select_sockets.set_read(m_socket);
            return;
        }

        default: {
            // Check socket writability
            if (has_pending_writes()) {
                select_sockets.set_write(m_socket);
            }

            // Check socket readability
            select_sockets.set_read(m_socket);
        }
    }
}

// returns false in case of error, true if ok
auto Socket::post_select_send_recv(Select_sockets& select_sockets) -> bool
{
    if (select_sockets.has_write(m_socket)) {
        const bool send_ok = send_pending();
        if (!send_ok) {
            return false;
        }
    }
    if (select_sockets.has_read(m_socket)) {
        const bool recv_ok = recv();
        if (!recv_ok) {
            return false;
        }
    }
    return true;
}

void Socket::set_connected()
{
    m_state          = State::CONNECTED;
    m_send_buffer    = std::make_unique<Ring_buffer>(4 * 1024 * 1024);
    m_receive_buffer = std::make_unique<Ring_buffer>(4 * 1024 * 1024);
}

// returns false in case of error, true if ok
auto Socket::post_select_connect(Select_sockets& select_sockets) -> bool
{
    if (select_sockets.has_except(m_socket)) {
        const int  error_code = WSAGetLastError();
        const auto message    = get_net_error_string(error_code);
        log_net->error("Could not connect, error {} - {}", error_code, message);
        // close() ?
        return false;
    }

    const bool is_writable = select_sockets.has_write(m_socket);
    if (is_writable) {
        log_net->info("Connected (fd is writable)");
        set_connected();
    } else {
        const int connect_res = ::connect(m_socket, m_addr_info->ai_addr, static_cast<int>(m_addr_info->ai_addrlen));
        if (connect_res == SOCKET_ERROR) {
            const int error_code = WSAGetLastError();
            if (error_code == WSAEISCONN) {
                log_net->info("Connected (WASEISCONN)");
                set_connected();
            } else if (error_code== WSAEWOULDBLOCK) {
                log_net->info("connect in progress");
                return true;
            } else if (is_error_fatal(error_code)) {
                const auto message = get_net_error_string(error_code);
                log_net->error("Could not connect, error {} - {}", error_code, message);
                close();
                return false;
            } else {
                const auto message = get_net_error_string(error_code);
                log_net->warn("Could not connect, error {} - {}", error_code, message);
                return false;
            }
        } else if (connect_res == 0) {
            log_net->info("Connected (connect() returned 0)");
            set_connected();
        }
    }
    return true;
}

auto Socket::post_select_listen(Select_sockets& select_sockets) -> std::optional<Socket>
{
    if (select_sockets.has_read(m_socket)) {
        log_net->info("post_select_listen() has readable socket");

        sockaddr_in address{};
        socklen_t len = sizeof(address);
        const SOCKET accept_res = ::accept(m_socket, reinterpret_cast<sockaddr*>(&address), &len);
        if (accept_res == INVALID_SOCKET) {
            const int error_code = WSAGetLastError();
            if (error_code != WSAEWOULDBLOCK) {
                const auto message = get_net_error_string(error_code);
                log_net->warn("accept() failed with error {} - {}", error_code, message);
                return {};
            }
        } else {
            // TODO check if already added
            // TODO set buffer sizes
            log_net->info("accept(): new connection");
            return Socket{accept_res, address};
        }
    }
    return {};
}

}
