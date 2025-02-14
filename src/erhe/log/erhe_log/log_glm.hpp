#pragma once

#include <fmt/format.h>

#include <glm/glm.hpp>

template <> struct fmt::formatter<glm::vec2>
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
    auto format(const glm::vec2& p, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({:.3f}, {:.3f})", p.x, p.y);
    }
};

template <> struct fmt::formatter<glm::vec3>
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
    auto format(const glm::vec3& p, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({:.3f}, {:.3f}, {:.3f})", p.x, p.y, p.z);
    }
};

template <> struct fmt::formatter<glm::vec4>
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
    auto format(const glm::vec4& p, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({:.3f}, {:.3f}, {:.3f}, {:.3f})", p.x, p.y, p.z, p.w);
    }
};

// dvec
template <> struct fmt::formatter<glm::dvec2>
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
    auto format(const glm::dvec2& p, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({:.3f}, {:.3f})", p.x, p.y);
    }
};

template <> struct fmt::formatter<glm::dvec3>
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
    auto format(const glm::dvec3& p, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({:.3f}, {:.3f}, {:.3f})", p.x, p.y, p.z);
    }
};

template <> struct fmt::formatter<glm::dvec4>
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
    auto format(const glm::dvec4& p, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({:.3f}, {:.3f}, {:.3f}, {:.3f})", p.x, p.y, p.z, p.w);
    }
};

// ivec
template <> struct fmt::formatter<glm::ivec2>
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
    auto format(const glm::ivec2& p, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({}, {})", p.x, p.y);
    }
};

template <> struct fmt::formatter<glm::ivec3>
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
    auto format(const glm::ivec3& p, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({}, {}, {})", p.x, p.y, p.z);
    }
};

template <> struct fmt::formatter<glm::ivec4>
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
    auto format(const glm::ivec4& p, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({}, {}, {}, {})", p.x, p.y, p.z, p.w);
    }
};
