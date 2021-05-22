#pragma once

#include <fmt/core.h>

#include <array>
#include <string>

#include <fmt/format.h>

namespace erhe::log
{

enum class Colorizer : unsigned int
{
    default_ = 0,
    glsl     = 1
};

struct Level
{
    static constexpr int LEVEL_ALL  {0};
    static constexpr int LEVEL_TRACE{1};
    static constexpr int LEVEL_INFO {2};
    static constexpr int LEVEL_WARN {3};
    static constexpr int LEVEL_ERROR{4};
};

struct Color
{
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

struct Category
{
    Category(int color0, int color1, int level, Colorizer colorizer = Colorizer::default_) noexcept
        : m_color    {color0, color1}
        , m_level    {level}
        , m_colorizer(colorizer)
    {
    }

    void write(bool indent, int level, const char* format, fmt::format_args args);

    void write(bool indent, int level, const std::string& text);

    template <typename... Args>
    void log(int /*level*/, const char* format, const Args& ... args)
    {
        write(true, format, fmt::make_format_args(args...));
    }

    template <typename... Args>
    void log_ni(int level, const char* format, const Args& ... args)
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
    void write(bool indent, const std::string& text);

    std::array<int, 2> m_color;
    int                m_level{Level::LEVEL_ALL};
    Colorizer          m_colorizer{Colorizer::default_};
    int                m_indent{0};
    bool               m_newline{false};
};

struct Log
{
    static int s_indent;
    static bool print_color();
    static void indent(int indent_amount);
    static void set_text_color(int c);
    static void console_init();
};

class Indenter
{
public:
    Indenter(int amount = 3)
        : m_amount{amount}
    {
        Log::indent(m_amount);
    }

    ~Indenter()
    {
        Log::indent(-m_amount);
    }

private:
    int m_amount{0};
};

} // namespace erhe::log

