#pragma once

#include <fmt/core.h>

#include <array>
#include <mutex>
#include <string>

#include <fmt/format.h>

namespace erhe::log
{

enum class Colorizer : unsigned int
{
    default_ = 0,
    glsl     = 1
};

class Level
{
public:
    static constexpr int LEVEL_ALL  {0};
    static constexpr int LEVEL_TRACE{1};
    static constexpr int LEVEL_INFO {2};
    static constexpr int LEVEL_WARN {3};
    static constexpr int LEVEL_ERROR{4};
};

class Console_color
{
public:
    static constexpr int DARK_BLUE   {1};
    static constexpr int DARK_GREEN  {2};
    static constexpr int DARK_RED    {4};
    static constexpr int DARK_CYAN   {1 | 2};
    static constexpr int DARK_MAGENTA{1 | 4};
    static constexpr int DARK_YELLOW {2 | 4};
    static constexpr int BLUE        {1 | 8};
    static constexpr int GREEN       {2 | 8};
    static constexpr int RED         {4 | 8};
    static constexpr int CYAN        {1 | 2 | 8};
    static constexpr int MAGENTA     {1 | 4 | 8};
    static constexpr int YELLOW      {2 | 4 | 8};
    static constexpr int DARK_GRAY   {8};
    static constexpr int DARK_GREY   {8};
    static constexpr int GRAY        {1 | 2 | 4};
    static constexpr int GREY        {1 | 2 | 4};
    static constexpr int WHITE       {1 | 2 | 4 | 8};
};

class Color
{
public:
    Color(float r, float g, float b, int console)
        : r      {r}
        , g      {g}
        , b      {b}
        , a      {1.0f}
        , console{console}
    {
    }
    float r{1.0f};
    float g{1.0f};
    float b{1.0f};
    float a{1.0f};
    int   console = {Console_color::GRAY};
};

class ILog_sink
{
public:
    virtual void write(
        const Color&     color,
        std::string_view text
    ) = 0;
};

class Category
{
public:
    Category(
        const float     r,
        const float     g,
        const float     b,
        const int       console_color,
        const int       level,
        const Colorizer colorizer = Colorizer::default_
    ) noexcept
        : m_color    {r, g, b, console_color}
        , m_level    {level}
        , m_colorizer(colorizer)
    {
    }

    void set_sink(ILog_sink* sink);

    void write(const bool indent, const int level, const char* format, fmt::format_args args);

    void write(const bool indent, const int level, const std::string& text);

    template <typename... Args>
    void log(const int /*level*/, const char* format, const Args& ... args)
    {
        write(true, format, fmt::make_format_args(args...));
    }

    template <typename... Args>
    void log_ni(const int level, const char* format, const Args& ... args)
    {
        write(false, level, format, fmt::make_format_args(args...));
    }

    template <typename... Args>
    void trace(const char* format, const Args& ... args)
    {
        write(true, Level::LEVEL_TRACE, format, fmt::make_format_args(args...));
    }

    void trace(const std::string& text)
    {
        write(true, Level::LEVEL_TRACE, text);
    }

    template <typename... Args>
    void info(const char* format, const Args& ... args)
    {
        write(true, Level::LEVEL_INFO, format, fmt::make_format_args(args...));
    }

    void info(const std::string& text)
    {
        write(true, Level::LEVEL_INFO, text);
    }

    template <typename... Args>
    void warn(const char* format, const Args& ... args)
    {
        write(true, Level::LEVEL_WARN, format, fmt::make_format_args(args...));
    }

    void warn(const std::string& text)
    {
        write(true, Level::LEVEL_WARN, text);
    }

    template <typename... Args>
    void error(const char* format, const Args& ... args)
    {
        write(true, Level::LEVEL_ERROR, format, fmt::make_format_args(args...));
    }

    void error(const std::string& text)
    {
        write(true, Level::LEVEL_ERROR, text);
    }

protected:
    void write(const bool indent, const std::string& text);

    Color      m_color;
    int        m_level    {Level::LEVEL_ALL};
    Colorizer  m_colorizer{Colorizer::default_};
    int        m_indent   {0};
    bool       m_newline  {true};
    ILog_sink* m_sink     {nullptr};
};

class Log
{
public:
    static int        s_indent;
    static std::mutex s_mutex;

    static bool print_color   ();
    static void indent        (const int indent_amount);
    static void set_text_color(const int c);
    static void console_init  ();
};

class Indenter final
{
public:
    Indenter(const int amount = 3);
    ~Indenter() noexcept;

private:
    int m_amount{0};
};

} // namespace erhe::log

