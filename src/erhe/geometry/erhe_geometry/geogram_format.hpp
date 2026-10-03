#pragma once

#include <fmt/format.h>

#include <geogram/basic/vecg.h>
#include <geogram/basic/geometry.h>

template <> struct fmt::formatter<GEO::vec2f>
{
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin())
    {
        auto it = ctx.begin(), end = ctx.end();
        if (it != end && *it != '}') {
            throw format_error("invalid format");
        }

        return it;
    }

    template <typename format_context>
    auto format(const GEO::vec2f& p, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({:.3f}, {:.3f})", p.x, p.y);
    }
};

template <> struct fmt::formatter<GEO::vec3f>
{
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin())
    {
        auto it = ctx.begin(), end = ctx.end();
        if (it != end && *it != '}') {
            throw format_error("invalid format");
        }

        return it;
    }

    template <typename format_context>
    auto format(const GEO::vec3f& p, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({:.3f}, {:.3f}, {:.3f})", p.x, p.y, p.z);
    }
};

template <> struct fmt::formatter<GEO::vec4f>
{
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin())
    {
        auto it = ctx.begin(), end = ctx.end();
        if (it != end && *it != '}') {
            throw format_error("invalid format");
        }

        return it;
    }

    template <typename format_context>
    auto format(const GEO::vec4f& p, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({:.3f}, {:.3f}, {:.3f}, {:.3f})", p.x, p.y, p.z, p.w);
    }
};

// dvec
template <> struct fmt::formatter<GEO::vec2>
{
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin())
    {
        auto it = ctx.begin(), end = ctx.end();
        if (it != end && *it != '}') {
            throw format_error("invalid format");
        }

        return it;
    }

    template <typename format_context>
    auto format(const GEO::vec2& p, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({:.3f}, {:.3f})", p.x, p.y);
    }
};

template <> struct fmt::formatter<GEO::vec3>
{
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin())
    {
        auto it = ctx.begin(), end = ctx.end();
        if (it != end && *it != '}') {
            throw format_error("invalid format");
        }

        return it;
    }

    template <typename format_context>
    auto format(const GEO::vec3& p, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({:.3f}, {:.3f}, {:.3f})", p.x, p.y, p.z);
    }
};

template <> struct fmt::formatter<GEO::vec4>
{
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin())
    {
        auto it = ctx.begin(), end = ctx.end();
        if (it != end && *it != '}') {
            throw format_error("invalid format");
        }

        return it;
    }

    template <typename format_context>
    auto format(const GEO::vec4& p, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({:.3f}, {:.3f}, {:.3f}, {:.3f})", p.x, p.y, p.z, p.w);
    }
};

// ivec
template <> struct fmt::formatter<GEO::vec2i>
{
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin())
    {
        auto it = ctx.begin(), end = ctx.end();
        if (it != end && *it != '}') {
            throw format_error("invalid format");
        }

        return it;
    }

    template <typename format_context>
    auto format(const GEO::vec2i& p, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({}, {})", p.x, p.y);
    }
};

template <> struct fmt::formatter<GEO::vec3i>
{
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin())
    {
        auto it = ctx.begin(), end = ctx.end();
        if (it != end && *it != '}') {
            throw format_error("invalid format");
        }

        return it;
    }

    template <typename format_context>
    auto format(const GEO::vec3i& p, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({}, {}, {})", p.x, p.y, p.z);
    }
};

template <> struct fmt::formatter<GEO::vec4i>
{
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin())
    {
        auto it = ctx.begin(), end = ctx.end();
        if (it != end && *it != '}') {
            throw format_error("invalid format");
        }

        return it;
    }

    template <typename format_context>
    auto format(const GEO::vec4i& p, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({}, {}, {}, {})", p.x, p.y, p.z, p.w);
    }
};

// uvec
template <> struct fmt::formatter<GEO::vec2u>
{
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin())
    {
        auto it = ctx.begin(), end = ctx.end();
        if (it != end && *it != '}') {
            throw format_error("invalid format");
        }

        return it;
    }

    template <typename format_context>
    auto format(const GEO::vec2u& p, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({}, {})", p.x, p.y);
    }
};

template <> struct fmt::formatter<GEO::vec3u>
{
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin())
    {
        auto it = ctx.begin(), end = ctx.end();
        if (it != end && *it != '}') {
            throw format_error("invalid format");
        }

        return it;
    }

    template <typename format_context>
    auto format(const GEO::vec3u& p, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({}, {}, {})", p.x, p.y, p.z);
    }
};

template <> struct fmt::formatter<GEO::vec4u>
{
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin())
    {
        auto it = ctx.begin(), end = ctx.end();
        if (it != end && *it != '}') {
            throw format_error("invalid format");
        }

        return it;
    }

    template <typename format_context>
    auto format(const GEO::vec4u& p, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({}, {}, {}, {})", p.x, p.y, p.z, p.w);
    }
};
