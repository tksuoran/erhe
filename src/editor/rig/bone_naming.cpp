#include "rig/bone_naming.hpp"

#include <array>
#include <cstddef>

namespace editor {

namespace {

// Where the side token of a name sits: [begin, end) within the name, the side
// it marks and the text that replaces it on a flip.
class Side_token
{
public:
    std::size_t      begin{0};
    std::size_t      end  {0};
    Bone_side        side {Bone_side::none};
    std::string_view flipped{};
};

[[nodiscard]] auto is_digit(const char c) -> bool
{
    return (c >= '0') && (c <= '9');
}

[[nodiscard]] auto is_lower(const char c) -> bool
{
    return (c >= 'a') && (c <= 'z');
}

[[nodiscard]] auto is_upper(const char c) -> bool
{
    return (c >= 'A') && (c <= 'Z');
}

[[nodiscard]] auto is_letter(const char c) -> bool
{
    return is_lower(c) || is_upper(c);
}

// The length of the stem: the name without its trailing index group (a '.'
// or '_' followed by one or more digits, at the very end). A name that is
// only an index group keeps it as its stem.
[[nodiscard]] auto stem_length(const std::string_view name) -> std::size_t
{
    std::size_t digits_begin = name.size();
    while ((digits_begin > 0) && is_digit(name[digits_begin - 1])) {
        --digits_begin;
    }
    if ((digits_begin == name.size()) || (digits_begin < 2)) {
        return name.size();
    }
    const char separator = name[digits_begin - 1];
    if ((separator != '.') && (separator != '_')) {
        return name.size();
    }
    return digits_begin - 1;
}

class Suffix_rule
{
public:
    std::string_view text;
    Bone_side        side;
    std::string_view flipped;
};

constexpr std::array<Suffix_rule, 8> c_separator_suffixes{{
    {".L", Bone_side::left,  ".R"},
    {".R", Bone_side::right, ".L"},
    {"_L", Bone_side::left,  "_R"},
    {"_R", Bone_side::right, "_L"},
    {".l", Bone_side::left,  ".r"},
    {".r", Bone_side::right, ".l"},
    {"_l", Bone_side::left,  "_r"},
    {"_r", Bone_side::right, "_l"}
}};

constexpr std::array<Suffix_rule, 2> c_capitalized_words{{
    {"Left",  Bone_side::left,  "Right"},
    {"Right", Bone_side::right, "Left" }
}};

constexpr std::array<Suffix_rule, 2> c_lowercase_words{{
    {"left",  Bone_side::left,  "right"},
    {"right", Bone_side::right, "left" }
}};

[[nodiscard]] auto ends_with(const std::string_view text, const std::string_view suffix) -> bool
{
    return (text.size() >= suffix.size()) && (text.substr(text.size() - suffix.size()) == suffix);
}

[[nodiscard]] auto find_side_token(const std::string_view name) -> Side_token
{
    const std::string_view stem = name.substr(0, stem_length(name));

    // Separator suffixes: the separator is part of the token, so the stem
    // needs at least one character before it ("_L" alone is not a side name).
    for (const Suffix_rule& rule : c_separator_suffixes) {
        if ((stem.size() > rule.text.size()) && ends_with(stem, rule.text)) {
            return Side_token{
                .begin   = stem.size() - rule.text.size(),
                .end     = stem.size(),
                .side    = rule.side,
                .flipped = rule.flipped
            };
        }
    }

    // Whole trailing words. "Left" after a lowercase letter is a camelCase
    // word boundary; after an uppercase letter it is not (an uppercase run
    // such as "HLeft" is not split into words).
    for (const Suffix_rule& rule : c_capitalized_words) {
        if (!ends_with(stem, rule.text)) {
            continue;
        }
        const std::size_t begin = stem.size() - rule.text.size();
        if ((begin == 0) || !is_upper(stem[begin - 1])) {
            return Side_token{.begin = begin, .end = stem.size(), .side = rule.side, .flipped = rule.flipped};
        }
    }
    for (const Suffix_rule& rule : c_lowercase_words) {
        if (!ends_with(stem, rule.text)) {
            continue;
        }
        const std::size_t begin = stem.size() - rule.text.size();
        if ((begin == 0) || (!is_letter(stem[begin - 1]) && !is_digit(stem[begin - 1]))) {
            return Side_token{.begin = begin, .end = stem.size(), .side = rule.side, .flipped = rule.flipped};
        }
    }
    return Side_token{};
}

} // anonymous namespace

auto bone_side(const std::string_view name) -> Bone_side
{
    return find_side_token(name).side;
}

auto flip_side_name(const std::string_view name) -> std::string
{
    const Side_token token = find_side_token(name);
    if (token.side == Bone_side::none) {
        return std::string{name};
    }
    std::string result;
    result.reserve(name.size() + 1);
    result.append(name.substr(0, token.begin));
    result.append(token.flipped);
    result.append(name.substr(token.end));
    return result;
}

}
