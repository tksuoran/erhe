#include "erhe/log/log.hpp"
#include "erhe/toolkit/verify.hpp"

#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <iostream>

#if defined _WIN32
#   include <windows.h>

/// #   include <io.h>
/// #   include <fcntl.h>
/// #   include <sys\types.h>
/// #   include <sys\stat.h>
#else
#   include <unistd.h>
#endif

namespace erhe::log
{


#if defined _WIN32
auto Log::print_color() -> bool
{
    return true;
}

void Log::set_text_color(const int c)
{
    HANDLE hConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsoleHandle, static_cast<WORD>(c));
}

/// const char* error_str(int code)
/// {
///     switch (code)
///     {
///         case EACCES: return "EACCES";
///         case EEXIST: return "EEXIST";
///         case EINVAL: return "EINVAL";
///         case EMFILE: return "EMFILE";
///         case ENOENT: return "ENOENT";
///         default:     return "?";
///     }
/// }

void Log::console_init()
{
    HWND   hwnd           = GetConsoleWindow();
    HICON  icon           = LoadIcon(NULL, MAKEINTRESOURCE(32516));
    HANDLE hConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD  mode           = 0;
    GetConsoleMode(hConsoleHandle, &mode);
    SetConsoleMode(hConsoleHandle, (mode & ~ENABLE_MOUSE_INPUT) | ENABLE_QUICK_EDIT_MODE | ENABLE_EXTENDED_FLAGS);

    SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)(icon));
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)(icon));

    std::setlocale(LC_CTYPE, ".UTF8");
    SetConsoleOutputCP(CP_UTF8);

    FILE* const l = fopen("log.txt", "wb");
    if (l)
    {
        fprintf(l, "");
        fclose(l);
    }
    /// {
    ///     int a = _open("a.txt", _O_CREAT | _O_BINARY | _O_TRUNC | _O_WRONLY, _S_IREAD | _S_IWRITE);
    ///     if (a != -1)
    ///     {
    ///         _write(a, "\xef\xbb\xbf", 3);
    ///         _write(a, "°", 1);
    ///         _close(a);
    ///     }
    ///     else
    ///     {
    ///         int code = errno;
    ///         fprintf(stderr, "_open(): error %d %s\n", code, error_str(code));
    ///         perror("_open() failed");
    ///     }
    ///
    ///     int b = _open("b.txt", _O_CREAT | _O_BINARY | _O_TRUNC | _O_WRONLY, _S_IREAD | _S_IWRITE);
    ///     if (b != -1)
    ///     {
    ///         _write(b, "\xef\xbb\xbf", 3);
    ///         _write(b, "\xc2\xb0", 2);
    ///         _close(b);
    ///     }
    ///     else
    ///     {
    ///         int code = errno;
    ///         fprintf(stderr, "_open(): error %d %s\n", code, error_str(code));
    ///         perror("_open() failed");
    ///     }
    /// }

    /// int l = _open("log.txt", _O_CREAT | _O_BINARY | _O_TRUNC | _O_WRONLY, _S_IREAD | _S_IWRITE);
    /// if (l != -1)
    /// {
    ///     _write(l, "°", 1);
    ///     _close(l);
    /// }
}
#else

auto Log::print_color()
-> bool
{
#    if defined(__APPLE__)
    return false;
#    else
    return isatty(fileno(stdout)) != 0;
# endif
}

void Log::set_text_color(const int c)
{
#    if defined(__APPLE__)
    (void)c;
#    else
    switch (c)
    {
        case Console_color::DARK_BLUE:    fputs("\033[22;34m", stdout); break;
        case Console_color::DARK_GREEN:   fputs("\033[22;32m", stdout); break;
        case Console_color::DARK_RED:     fputs("\033[22;31m", stdout); break;
        case Console_color::DARK_CYAN:    fputs("\033[22;36m", stdout); break;
        case Console_color::DARK_MAGENTA: fputs("\033[22;35m", stdout); break;
        case Console_color::DARK_YELLOW:  fputs("\033[22;33m", stdout); break;
        case Console_color::BLUE:         fputs("\033[1;34m", stdout); break;
        case Console_color::GREEN:        fputs("\033[1;32m", stdout); break;
        case Console_color::RED:          fputs("\033[1;31m", stdout); break;
        case Console_color::CYAN:         fputs("\033[1;36m", stdout); break;
        case Console_color::MAGENTA:      fputs("\033[1;35m", stdout); break;
        case Console_color::YELLOW:       fputs("\033[1;33m", stdout); break;
        case Console_color::DARK_GREY:    fputs("\033[1;30m", stdout); break;
        case Console_color::GREY:         fputs("\033[22;37m", stdout); break;
        case Console_color::WHITE:        fputs("\033[1;37m", stdout); break;
        default: break;
    }
#    endif
}

void Log::console_init()
{
    FILE* l = fopen("log.txt", "wb");
    if (l != nullptr)
    {
        fclose(l);
    }
}
#endif

int        Log::s_indent{0};
std::mutex Log::s_mutex;

void Log::indent(const int indent_amount)
{
    s_indent += indent_amount;
}

Indenter::Indenter(const int amount)
    : m_amount{amount}
{
    const std::lock_guard<std::mutex> lock{Log::s_mutex};
    Log::indent(m_amount);
}

Indenter::~Indenter()
{
    const std::lock_guard<std::mutex> lock{Log::s_mutex};
    Log::indent(-m_amount);
}

void Category::set_sink(ILog_sink* sink)
{
    m_sink = sink;
}

void Category::write(const bool indent, const int level, const char* format, fmt::format_args args)
{
    if (level < m_level)
    {
        return;
    }

    const std::string text = fmt::vformat(format, args);

    if (m_sink != nullptr)
    {
        m_sink->write(m_color, text);
    }
    //else
    {
        write(indent, text);
    }
}

void Category::write(const bool indent, const int level, const std::string& text)
{
    if (level < m_level)
    {
        return;
    }

    write(indent, text);
}

//          111111111122
// 123456789012345678901
// 20211022 14:17:01.337
auto timestamp() -> std::string
{
    struct timespec ts{};
    timespec_get(&ts, TIME_UTC);

    struct tm time;
#ifdef _MSC_VER
    localtime_s(&time, &ts.tv_sec);
#else
    localtime_r(&ts.tv_sec, &time);
#endif

    // Write time
    return fmt::format(
        "{:04d}{:02d}{:02d} {:02}:{:02}:{:02}.{:03d} ",
        time.tm_year + 1900,
        time.tm_mon + 1,
        time.tm_mday,
        time.tm_hour,
        time.tm_min,
        time.tm_sec,
        ts.tv_nsec / 1000000
    );
}

void Category::write(const bool indent, const std::string& text)
{
    const std::lock_guard<std::mutex> lock{Log::s_mutex};

    // Log to console
    if (Log::print_color())
    {
        const char* p;
        const char* span;
        size_t span_len;
        char c;
        switch (m_colorizer)
        {
            //using enum Colorizer;
            case Colorizer::default_:
            {
                Log::set_text_color(m_color.console);
                p = span = text.data();
                span_len = 0;
                char prev = 0;
                for (;;)
                {
                    c = p[0];
                    if ((c != '\0') && m_newline && indent)
                    {
                        if (span_len > 0)
                        {
                            fwrite(span, 1, span_len, stdout);
                            //fflush(stdout);
                        }

                        if constexpr (true)
                        {
                            const auto stamp_string = timestamp();
                            fputs(stamp_string.c_str(), stdout);
                        }

                        if (indent)
                        {
                            for (int i = 0; i < Log::s_indent; ++i)
                            {
                                putc(' ', stdout);
                            }
                        }
                        m_newline = false;
                    }

                    char next = (c != '\0') ? p[1] : '\0';
                    if (c == '(' || (c == ':' && next != ':' && prev != ':'))
                    {
                        fwrite(span, 1, span_len + 1, stdout);
                        //fflush(stdout);
                        prev = c;
                        ++p;
                        //++span_len;
                        span = p;
                        span_len = 0;
                        Log::set_text_color(Console_color::GRAY);
                    }
                    else if (c == ')')
                    {
                        if (span_len > 0)
                        {
                            fwrite(span, 1, span_len, stdout);
                            //fflush(stdout);
                        }
                        Log::set_text_color(m_color.console);
                        span = p;
                        ++p;
                        span_len = 1;
                        //fwrite(span, 1, span_len, stdout);
                        //fflush(stdout);
                        //++p;
                        //c = *p;
                    }
                    else if (c == '\n')
                    {
                        {
                            fwrite(span, 1, span_len + 1, stdout);
                            //fflush(stdout);
                        }
                        ++p;
                        span = p;
                        span_len = 0;
                        Log::set_text_color(m_color.console);
                        m_newline = true;
                    }
                    else if (c == 0)
                    {
                        fputs(span, stdout);
                        //fflush(stdout);
                        Log::set_text_color(Console_color::GRAY);
                        break;
                    }
                    else
                    {
                        prev = c;
                        ++p;
                        ++span_len;
                    }
                }
                break;
            }

            case Colorizer::glsl:
            {
                Log::set_text_color(m_color.console);
                p = span = text.data();
                span_len = 1;
                for (;;)
                {
                    c = *p;

                    if ((c != '\0') && m_newline && indent)
                    {
                        for (int i = 0; i < Log::s_indent; ++i)
                        {
                            putc(' ', stdout);
                        }
                        m_newline = false;
                    }

                    p++;
                    if (c == ':')
                    {
                        fwrite(span, 1, span_len, stdout);
                        //fflush(stdout);
                        span = p;
                        span_len = 1;
                        Log::set_text_color(Console_color::GRAY);
                    }
                    else if (c == '\n')
                    {
                        fwrite(span, 1, span_len, stdout);
                        //fflush(stdout);
                        span = p;
                        span_len = 1;
                        Log::set_text_color(m_color.console);
                        m_newline = true;
                    }
                    else if (c == 0)
                    {
                        fputs(span, stdout);
                        //fflush(stdout);
                        Log::set_text_color(Console_color::GRAY);
                        break;
                    }
                    else
                    {
                        ++span_len;
                    }
                }
                break;
            }

            default:
            {
                ERHE_FATAL("Bad log colorizer\n");
            }
        }
        Log::set_text_color(Console_color::GRAY);
    }

    // Log to file
    FILE* log_file = fopen("log.txt", "ab+"); // ,ccs=UTF-8
    if (log_file != nullptr)
    {
        fputs(text.data(), log_file);
        fclose(log_file);
    }

    /// int l = _open("log.txt", _O_BINARY | _O_APPEND | _O_WRONLY, _S_IREAD | _S_IWRITE);
    /// if (l != -1)
    /// {
    ///     _write(l, text.data(), static_cast<unsigned int>(text.size()));
    ///     _close(l);
    /// }

    // Log to debugger
#if defined(_WIN32)
    OutputDebugStringA(text.c_str());
#endif
}

} // namespace erhe::log
