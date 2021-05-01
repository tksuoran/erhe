#ifndef log_hpp_erhe_log
#define log_hpp_erhe_log

#include <fmt/core.h>

#include <array>
#include <string>

#include <glm/glm.hpp>

#include <fmt/format.h>

template <>
struct fmt::formatter<glm::vec4>
{
	constexpr auto parse(format_parse_context& ctx)
    {
        return ctx.begin();
    }

	template <typename FormatContext>
    auto format(const glm::vec4& vec, FormatContext& ctx)
    {
        return format_to(ctx.out(), "({: #08f}, {: #08f}, {: #08f}, {: #08f})", vec.x, vec.y, vec.z, vec.w);
    }
};

template <>
struct fmt::formatter<glm::vec3>
{
	constexpr auto parse(format_parse_context& ctx)
    {
        return ctx.begin();
    }

	template <typename FormatContext>
    auto format(const glm::vec3& vec, FormatContext& ctx)
    {
        return format_to(ctx.out(), "({: #08f}, {: #08f}, {: #08f})", vec.x, vec.y, vec.z);
    }
};

template <>
struct fmt::formatter<glm::vec2>
{
	constexpr auto parse(format_parse_context& ctx)
    {
        return ctx.begin();
    }

	template <typename FormatContext>
    auto format(const glm::vec2& vec, FormatContext& ctx)
    {
        return format_to(ctx.out(), "({: #08f}, {: #08f})", vec.x, vec.y);
    }
};

template <>
struct fmt::formatter<glm::ivec2>
    : fmt::formatter<string_view>
{
	constexpr auto parse(format_parse_context& ctx)
    {
        return ctx.begin();
    }

	template <typename FormatContext>
    auto format(const glm::ivec2& vec, FormatContext& ctx)
    {
        return format_to(ctx.out(), "({}, {})", vec.x, vec.y);
    }
};

namespace erhe::log
{

struct Log
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

    static int s_indent;

    static bool print_color();

    static void indent(int indent_amount);

    static void set_text_color(int c);

    static void console_init();

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
};

} // namespace erhe::log

#if _MSC_VER

#if defined(_WIN32)
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   define VC_EXTRALEAN
#   ifndef STRICT
#       define STRICT
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX       // Macros min(a,b) and max(a,b)
#   endif
#   include <windows.h>
#endif

#define FATAL(format, ...) do { fmt::print("{}:{} " format, __FILE__, __LINE__, ##__VA_ARGS__); DebugBreak(); abort(); } while (1)
#define VERIFY(expression) do { if (!(expression)) { FATAL("assert {} failed in {}", #expression, __func__); } } while (0)

#else

#define FATAL(format, ...) do { fmt::print("{}:{} " format, __FILE__, __LINE__, ##__VA_ARGS__); __builtin_trap(); __builtin_unreachable(); abort(); } while (1)
#define VERIFY(expression) do { if (!(expression)) { FATAL("assert {} failed in {}", #expression, __func__); } } while (0)

#endif

#endif
