#pragma once

#include "erhe/log/log.hpp"

#include <glm/glm.hpp>

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
        //return format_to(ctx.out(), "({: #08f}, {: #08f}, {: #08f}, {: #08f})", vec.x, vec.y, vec.z, vec.w);
        return format_to(ctx.out(), "({: 14f}, {: 14f}, {: 14f}, {: 14f})", vec.x, vec.y, vec.z, vec.w);
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
        //return format_to(ctx.out(), "({: #08f}, {: #08f}, {: #08f})", vec.x, vec.y, vec.z);
        return format_to(ctx.out(), "({: 14f}, {: 14f}, {: 14f})", vec.x, vec.y, vec.z);
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
        //return format_to(ctx.out(), "({: #08f}, {: #08f})", vec.x, vec.y);
        return format_to(ctx.out(), "({: 14f}, {: 14f})", vec.x, vec.y);
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
