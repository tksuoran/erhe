#include "erhe_log/log.hpp"
#include "erhe_net/client.hpp"
#include "erhe_net/server.hpp"
#include "erhe_net/net_log.hpp"

#if defined(ERHE_TERMINAL_LIBRARY_CPP_TERMINAL)
#   include "cpp-terminal/base.hpp"
#   include "cpp-terminal/exception.hpp"
#   include "cpp-terminal/input.hpp"
#   include "cpp-terminal/terminal.hpp"
#   include "cpp-terminal/window.hpp"
#   include "cpp-terminal/tty.hpp"
#endif

#include <cxxopts.hpp>
#include <fmt/format.h>

#include <cstdio>
#include <algorithm>
#include <memory>

class Peer
{
public:
    virtual ~Peer() = default;
    virtual auto get_status() -> std::string = 0;
    virtual void send      (const std::string& message) = 0;
};

class Server_peer : public Peer
{
public:
    Server_peer() = default;
    explicit Server_peer(erhe::net::Server* server)
        : m_server{server}
    {
    }
    ~Server_peer() override = default;

    auto get_status() -> std::string override
    {
        return fmt::format(
            "{} - {} clients",
            c_str(m_server->get_state()),
            m_server->get_client_count()
        );
    }
    void send(const std::string& message) override
    {
        m_server->broadcast(message);
    }

private:
    erhe::net::Server* m_server{nullptr};
};

class Client_peer : public Peer
{
public:
    Client_peer() = default;
    explicit Client_peer(erhe::net::Client* client)
        : m_client{client}
    {
    }
    ~Client_peer() override = default;

    auto get_status() -> std::string override
    {
        return fmt::format(
            "{}",
            c_str(m_client->get_state())
        );
    }
    void send(const std::string& message) override
    {
        m_client->send(message);
    }

private:
    erhe::net::Client* m_client{nullptr};
};

#if defined(ERHE_TERMINAL_LIBRARY_CPP_TERMINAL)
class Window
{
public:
    explicit Window(Peer* peer)
        : m_peer{peer}
    {
    }

    void update_layout(const std::size_t first_row, const std::size_t last_row)
    {
        if ((first_row == 0) || (last_row == 0)) {
            m_row_status   = 0;
            m_row_input    = 0;
            m_row_messages = 0;
            m_row_log      = 0;
        }
        m_row_status   = first_row;
        m_row_input    = m_row_status + 1;
        m_row_messages = m_row_input + 1;
        m_row_log      = m_row_messages + (last_row - m_row_messages) / 2;
    }
    void render_status()
    {
        if ((m_peer == nullptr) || (m_row_status == 0)) {
            return;
        }
        std::string s;
        s.reserve(16 * 1024);
        s.append(style(Term::Style::RESET));
        s.append(Term::color_fg(Term::Color::Name::Cyan));
        s.append(Term::cursor_move(m_row_status, 1));
        s.append(m_peer->get_status());
        s.append(Term::clear_eol());
        s.append(style(Term::Style::RESET));
        std::cout << s << std::flush;
    }
    void render_line()
    {
        if (m_row_input == 0) {
            return;
        }
        std::string s;
        s.reserve(16 * 1024);
        s.append(Term::cursor_move(m_row_input, 1));
        s.append(style(Term::Style::RESET));
        s.append(Term::color_fg(Term::Color::Name::BrightYellow));
        s.append("> ");
        s.append(Term::color_fg(Term::Color::Name::BrightWhite));
        if (m_cursor_position > 0) {
            s.append(m_line.substr(0, m_cursor_position));
        }
        s.append(style(Term::Style::REVERSED));
        if (m_cursor_position < m_line.length()) {
            s.append(1, m_line.at(m_cursor_position));
        } else {
            s.append(1, ' ');
        }
        s.append(style(Term::Style::RESET));
        s.append(Term::color_fg(Term::Color::Name::BrightWhite));
        if (m_cursor_position + 1 < m_line.length()) {
            const auto remaining_length = m_line.length() - m_cursor_position + 1;
            s.append(m_line.substr(m_cursor_position + 1, remaining_length));
        }
        s.append(Term::clear_eol());
        s.append(style(Term::Style::RESET));
        std::cout << s << std::flush;
    }
    void render_logs()
    {
        if (m_row_log == 0) {
            return;
        }
        std::string s;
        s.reserve(16 * 1024);
        s.append(style(Term::Style::RESET));
        s.append(Term::color_fg(Term::Color::Name::Cyan));
        s.append(Term::cursor_move(m_row_log, 1));

        auto& tail  = erhe::log::get_tail_store_log();
        //auto& frame = erhe::log::get_frame_store_log();

        auto& tail_entries = tail->get_log();
        const auto visible_count = (std::min)(
            static_cast<size_t>(10),
            tail_entries.size()
        );
        for (
            auto i = tail_entries.rbegin(),
            end = tail_entries.rbegin() + visible_count;
            i != end;
            ++i
        ) {
            auto& entry = *i;
            s.append(Term::color_fg(Term::Color::Name::Blue));
            s.append(entry.timestamp.c_str());
            s.append(Term::color_fg(Term::Color::Name::Gray));
            s.append(entry.message);
            s.append(Term::clear_eol());
            s.append("\n");
        }
        //for (
        //    auto i = tail_entries.begin(),
        //    end = tail_entries.begin() + visible_count;
        //    i != end;
        //    ++i
        //) {
        //    auto& entry = *i;
        //    s.append(entry);
        //}
        s.append(style(Term::Style::RESET));
        std::cout << s << std::flush;
    }
    void render_messages()
    {
        if (m_row_messages == 0) {
            return;
        }
        std::string s;
        s.reserve(16 * 1024);
        s.append(Term::cursor_move(m_row_messages, 1));
        s.append(style(Term::Style::RESET));
        s.append(Term::color_fg(Term::Color::Name::BrightGreen));
        for (const auto& line : m_messages) {
            s.append(line);
            s.append(Term::clear_eol());
            s.append("\n");
        }
        std::cout << s << std::flush;
    }
    void sent(const std::string& message)
    {
        m_messages.push_back("sent: " + message);
        render_messages();
    }
    void received(const std::string& message)
    {
        m_messages.push_back("received: " + message);
        render_messages();
    }
    void home()
    {
        m_cursor_position = 0;
    }
    void end()
    {
        m_cursor_position = m_line.length();
    }
    void move_left()
    {
        if (m_cursor_position == 0) {
            return;
        }
        --m_cursor_position;
    }
    void move_left_word()
    {
        m_cursor_position = (std::min)(m_cursor_position, m_line.length());
        if (m_cursor_position == 0) {
            return;
        }
        for (;;) {
            --m_cursor_position;
            if (m_cursor_position == 0) {
                return;
            }
            if (!isspace(m_line.at(m_cursor_position - 1))) {
                break;
            }
        }
        for (;;) {
            --m_cursor_position;
            if (m_cursor_position == 0) {
                return;
            }
            if (isspace(m_line.at(m_cursor_position - 1))) {
                break;
            }
        }
    }
    void move_right()
    {
        ++m_cursor_position;
    }
    void move_right_word()
    {
        if (m_cursor_position >= m_line.length()) {
            return;
        }
        for (;;) {
            ++m_cursor_position;
            if (m_cursor_position + 1 >= m_line.length()) {
                return;
            }
            if (!isspace(m_line.at(m_cursor_position + 1))) {
                break;
            }
        }
        for (;;) {
            ++m_cursor_position;
            if (m_cursor_position + 1 >= m_line.length()) {
                return;
            }
            if (isspace(m_line.at(m_cursor_position + 1))) {
                break;
            }
        }
    }
    void insert(const char key)
    {
        if (m_cursor_position > m_line.length()) {
            const auto fill_count = m_cursor_position - m_line.length();
            m_line.append(fill_count, ' ');
        }
        m_line.insert(m_cursor_position++, &key, 1);
    }
    void delete_left()
    {
        if (m_cursor_position == 0) {
            return;
        }
        --m_cursor_position;
        m_line.erase(m_cursor_position, 1);
    }
    void delete_right()
    {
        if (m_cursor_position >= m_line.length()) {
            return;
        }
        m_line.erase(m_cursor_position, 1);
    }
    void remove_start_of_line()
    {
        if (m_cursor_position == 0) {
            return;
        }
        m_line.erase(0, m_cursor_position);
        m_cursor_position = 0;
    }
    void remove_end_of_line()
    {
        if (m_cursor_position == m_line.length()) {
            return;
        }
        const auto count = m_line.length() - m_cursor_position;
        m_line.erase(m_cursor_position, count);
    }
    void send()
    {
        if (m_peer == nullptr) {
            return;
        }
        m_peer->send(m_line);
        sent(m_line);
        m_line.clear();
        m_cursor_position = 0;
        render_line();
    }
    void quit()
    {
    }
    void update()
    {
        render_status();
        render_logs();
        const int key = Term::read_key0();
        switch (key) {
            case Term::Key::NO_KEY:      return;
            case Term::Key::DEL:         delete_right(); break;
            case Term::Key::BACKSPACE:   delete_left(); break;
            case Term::Key::CTRL_B:
            case Term::Key::ARROW_LEFT:  move_left(); break;
            case Term::Key::CTRL_LEFT:   move_left_word(); break;
            case Term::Key::CTRL_F:
            case Term::Key::ARROW_RIGHT: move_right(); break;
            case Term::Key::CTRL_RIGHT:  move_right_word(); break;
            case Term::Key::CTRL_U:      remove_start_of_line(); break;
            case Term::Key::CTRL_K:      remove_end_of_line(); break;
            case Term::Key::ARROW_UP:    break;
            case Term::Key::ARROW_DOWN:  break;
            case Term::Key::CTRL_A:
            case Term::Key::HOME:        home(); break;
            case Term::Key::END:         end(); break;
            case Term::Key::ENTER:       send(); break;
            case Term::Key::ESC:         quit(); break;
            case Term::Key::CTRL + 'c':  quit(); break;
            default: {
                if (!iscntrl(key)) {
                    insert(static_cast<char>(key));
                }
                break;
            }
        }
        render_line();
    }

private:
    Peer*                    m_peer{nullptr};
    std::string              m_line;
    std::size_t              m_cursor_position{0};
    std::vector<std::string> m_messages;
    std::size_t              m_row_status     {0};
    std::size_t              m_row_input      {0};
    std::size_t              m_row_messages   {0};
    std::size_t              m_row_log        {0};
};
#endif

auto str(const bool value) -> const char*
{
    return value ? "true" : "false";
}

class Options
{
public:
    Options(int argc, char** argv)
    {
        cxxopts::Options options{"net-test", "Erhe net testing app (C) 2023 Timo Suoranta"};

        options.add_options("Client configuration")
            ("client",          "Enable net test client", cxxopts::value<bool>()->default_value(str(run_client)))
            ("connect-address", "Address where client connects to", cxxopts::value<std::string>()->default_value("127.0.0.1"), "<address>")
            ("connect-port",    "Port where client connects to", cxxopts::value<int>()->default_value("34567"), "<port>");

        options.add_options("Server configuration")
            ("server",          "Enable net test server", cxxopts::value<bool>()->default_value(str(run_server)))
            ("listen-address",  "Address where server listen for client connections", cxxopts::value<std::string>()->default_value("127.0.0.1"), "<address>")
            ("listen-port",     "Port where server listen for client connections", cxxopts::value<int>()->default_value("34567"), "<port>");

        options.add_options("Terminal configuration")
            ("terminal",        "Enable terminal", cxxopts::value<bool>()->default_value(str(terminal)));

        try {
            auto arguments = options.parse(argc, argv);

            terminal        = arguments["terminal"       ].as<bool>();
            run_client      = arguments["client"         ].as<bool>();
            connect_address = arguments["connect-address"].as<std::string>();
            connect_port    = arguments["connect-port"   ].as<int>();
            run_server      = arguments["server"         ].as<bool>();
            listen_address  = arguments["listen-address" ].as<std::string>();
            listen_port     = arguments["listen-port"    ].as<int>();
        } catch (const std::exception& e) {
            fmt::print(
                "Error parsing command line argumenst: {}",
                e.what()
            );
        }
    }

    bool        terminal{false};
    bool        run_client{true};
    std::string connect_address;
    int         connect_port;
    bool        run_server{false};
    std::string listen_address;
    int         listen_port;
};

auto main(int argc, char** argv) -> int
{
    std::unique_ptr<Server_peer>    server_peer;
    std::unique_ptr<Client_peer>    client_peer;
    Options                         options{argc, argv};

#if defined(ERHE_TERMINAL_LIBRARY_CPP_TERMINAL)
    std::unique_ptr<Term::Terminal> terminal;
    std::unique_ptr<Window>         client_window;
    std::unique_ptr<Window>         server_window;
    int window_count = 0;
#endif

    erhe::log::console_init();

#if defined(ERHE_TERMINAL_LIBRARY_CPP_TERMINAL)
    if (Term::is_stdin_a_tty() && options.terminal) {
        try {
            terminal = std::make_unique<Term::Terminal>(true, true, true);
        } catch (...) {
            // NOP
        }
    }
    if (!terminal)
#endif
    {
        erhe::log::log_to_console();
    }
    erhe::log::initialize_log_sinks();
    erhe::net::initialize_logging();
    erhe::net::initialize_net();

    erhe::net::Client client;
    erhe::net::Server server;

    if (options.run_server) {
        server.listen(options.listen_address.c_str(), options.listen_port);
        server_peer = std::make_unique<Server_peer>(&server);
#if defined(ERHE_TERMINAL_LIBRARY_CPP_TERMINAL)
        if (terminal) {
            server_window = std::make_unique<Window>(server_peer.get());
            ++window_count;
        }
#endif
        server.set_receive_handler(
            [
#if defined(ERHE_TERMINAL_LIBRARY_CPP_TERMINAL)
                &terminal,
                &server_window
#endif
            ](const uint8_t* data, const std::size_t length) {
                if ((data == nullptr) || (length == 0)) {
                    return;
                }
                static_cast<void>(length);
#if defined(ERHE_TERMINAL_LIBRARY_CPP_TERMINAL)
                if (server_window) {
                    server_window->received(std::string{reinterpret_cast<const char*>(data), length});
                } else
#endif
                {
                    // Note: data is not NUL-terminated
                    fputs("server received: ", stdout);
                    fwrite(data, sizeof(char), length, stdout);
                    fputc('\n', stdout);
                    fflush(stdout);
                }
            }
        );
    }

    if (options.run_client) {
        client.connect(options.connect_address.c_str(), options.connect_port);
        client_peer = std::make_unique<Client_peer>(&client);
#if defined(ERHE_TERMINAL_LIBRARY_CPP_TERMINAL)
        if (terminal) {
            client_window = std::make_unique<Window>(client_peer.get());
            ++window_count;
        }
#endif
        client.set_receive_handler(
            [
#if defined(ERHE_TERMINAL_LIBRARY_CPP_TERMINAL)
                &terminal,
                &client_window
#endif
            ](const uint8_t* data, const std::size_t length)
            {
                if ((data == nullptr) || (length == 0)) {
                    return;
                }
                static_cast<void>(length);
#if defined(ERHE_TERMINAL_LIBRARY_CPP_TERMINAL)
                if (client_window) {
                    client_window->received(std::string{reinterpret_cast<const char*>(data), length});
                } else
#endif
                {
                    // Note: data is not NUL-terminated
                    fputs("client received: ", stdout);
                    fwrite(data, sizeof(char), length, stdout);
                    fputc('\n', stdout);
                    fflush(stdout);
                }
            }
        );
    }

    for (;;) {
        if (options.run_server) {
            if (server.get_state() != erhe::net::Socket::State::CLOSED) {
                server.poll(10);
            } // TODO else - re-listen?
        }
        if (options.run_client) {
            if (client.get_state() != erhe::net::Socket::State::CLOSED) {
                client.poll(10);
            } else {
                // Connection was lost - reconnect
                client.connect(options.connect_address.c_str(), options.connect_port);
            }
        }
#if defined(ERHE_TERMINAL_LIBRARY_CPP_TERMINAL)
        if (terminal && (window_count > 0)) {
            static std::size_t last_width     = 0;
            static std::size_t last_row_count = 0;
            const auto size      = Term::get_size();
            const auto width     = std::get<0>(size);
            const auto row_count = std::get<1>(size);
            if (
                (width != last_width) ||
                (row_count != last_row_count)
            )
            {
                std::size_t next_row         = 1;
                std::size_t window_row_count = row_count / window_count;
                if (server_window) {
                    std::size_t last_row = (std::min)(
                        next_row + window_row_count - 1,
                        row_count
                    );
                    server_window->update_layout(next_row, last_row);
                    next_row = last_row + 1;
                }
                if (client_window) {
                    std::size_t last_row = (std::min)(
                        next_row + window_row_count - 1,
                        row_count
                    );
                    client_window->update_layout(next_row, last_row);
                    next_row = last_row + 1;
                }
            }
            if (server_window) {
                server_window->update();
            }
            if (client_window) {
                client_window->update();
            }
        }
#endif
    }

    return EXIT_SUCCESS;
}
