#include "erhe/log/log.hpp"
#include "erhe/toolkit/verify.hpp"

#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <iostream>

#if defined _WIN32
#    include <windows.h>
#else
#    include <unistd.h>
#endif

namespace erhe::log
{


#if defined _WIN32
auto Log::print_color()
-> bool
{
    return true;
}

void Log::set_text_color(int c)
{
    HANDLE hConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsoleHandle, static_cast<WORD>(c));
}

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

    FILE* l = fopen("log.txt", "wb");
    if (l)
    {
        fprintf(l, "");
        fclose(l);
    }
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

void Log::set_text_color(int c)
{
#    if defined(__APPLE__)
    (void)c;
#    else
    switch (c)
    {
        case Log::Color::DARK_BLUE:    fputs("\033[22;34m", stdout); break;
        case Log::Color::DARK_GREEN:   fputs("\033[22;32m", stdout); break;
        case Log::Color::DARK_RED:     fputs("\033[22;31m", stdout); break;
        case Log::Color::DARK_CYAN:    fputs("\033[22;36m", stdout); break;
        case Log::Color::DARK_MAGENTA: fputs("\033[22;35m", stdout); break;
        case Log::Color::DARK_YELLOW:  fputs("\033[22;33m", stdout); break;
        case Log::Color::BLUE:         fputs("\033[1;34m", stdout); break;
        case Log::Color::GREEN:        fputs("\033[1;32m", stdout); break;
        case Log::Color::RED:          fputs("\033[1;31m", stdout); break;
        case Log::Color::CYAN:         fputs("\033[1;36m", stdout); break;
        case Log::Color::MAGENTA:      fputs("\033[1;35m", stdout); break;
        case Log::Color::YELLOW:       fputs("\033[1;33m", stdout); break;
        case Log::Color::DARK_GREY:    fputs("\033[1;30m", stdout); break;
        case Log::Color::GREY:         fputs("\033[22;37m", stdout); break;
        case Log::Color::WHITE:        fputs("\033[1;37m", stdout); break;
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

int Log::s_indent{0};

void Log::indent(int indent_amount)
{
    s_indent += indent_amount;
}

void Category::write(bool indent, int level, const char* format, fmt::format_args args)
{                       
    if (level < m_level)
    {
        return;
    }

    std::string text = fmt::vformat(format, args);

    write(indent, text);
}

void Category::write(bool indent, int level, const std::string& text)
{
    if (level < m_level)
    {
        return;
    }

    write(indent, text);
}

std::mutex Category::s_mutex;

void Category::write(bool indent, const std::string& text)
{
    std::lock_guard<std::mutex> lock{s_mutex};
    // Log to console
    if (Log::print_color())
    {
        const char* p;
        const char* span;
        size_t span_len;
        char c;
        switch (m_colorizer)
        {
            case Colorizer::default_:
            {
                Log::set_text_color(m_color[0]);
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

                        for (int i = 0; i < Log::s_indent; ++i)
                        {
                            putc(' ', stdout);
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
                        Log::set_text_color(m_color[1]);
                    }
                    else if (c == ')')
                    {
                        if (span_len > 0)
                        {
                            fwrite(span, 1, span_len, stdout);
                            //fflush(stdout);
                        }
                        Log::set_text_color(m_color[0]);
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
                        if (span_len > 0)
                        {
                            fwrite(span, 1, span_len, stdout);
                            //fflush(stdout);
                        }
                        span = p;
                        ++p;
                        span_len = 1;
                        Log::set_text_color(m_color[0]);
                        m_newline = true;
                    }
                    else if (c == 0)
                    {
                        fputs(span, stdout);
                        //fflush(stdout);
                        Log::set_text_color(m_color[1]);
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
                Log::set_text_color(m_color[0]);
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
                        Log::set_text_color(m_color[1]);
                    }
                    else if (c == '\n')
                    {
                        fwrite(span, 1, span_len, stdout);
                        //fflush(stdout);
                        span = p;
                        span_len = 1;
                        Log::set_text_color(m_color[0]);
                        m_newline = true;
                    }
                    else if (c == 0)
                    {
                        fputs(span, stdout);
                        //fflush(stdout);
                        Log::set_text_color(m_color[1]);
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
                FATAL("Bad log colorizer");
            }
        }
        Log::set_text_color(Color::GRAY);
    }

    // Log to file
    FILE* log_file = fopen("log.txt", "ab+");
    if (log_file != nullptr)
    {
        fputs(text.data(), log_file);
        fclose(log_file);
    }

    // Log to debugger
#if defined(_WIN32)
    OutputDebugStringA(text.c_str());
#endif
}

} // namespace erhe::log
