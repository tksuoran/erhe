#include "erhe_texgen/value_type.hpp"

#include "erhe_verify/verify.hpp"

namespace erhe::texgen {

namespace {

// Indexed by Value_type
constexpr const char* c_value_type_names[value_type_count] = { "f", "rgb", "rgba" };
constexpr const char* c_glsl_type_names [value_type_count] = { "float", "vec3", "vec4" };

// Conversion expression templates, indexed [from][to].
// "$(value)" is the placeholder for the source expression.
// Expressions are verbatim from Material Maker's io_types.mmt (MIT license).
constexpr const char* c_conversion_templates[value_type_count][value_type_count] = {
    // from grayscale ("f")
    {
        nullptr,                        // f    -> f
        "vec3($(value))",               // f    -> rgb
        "vec4(vec3($(value)), 1.0)"     // f    -> rgba
    },
    // from rgb
    {
        "(dot($(value), vec3(1.0))/3.0)", // rgb  -> f
        nullptr,                          // rgb  -> rgb
        "vec4($(value), 1.0)"             // rgb  -> rgba
    },
    // from rgba
    {
        "(dot(($(value)).rgb, vec3(1.0))/3.0)", // rgba -> f
        "(($(value)).rgb)",                     // rgba -> rgb
        nullptr                                 // rgba -> rgba
    }
};

} // anonymous namespace

auto value_type_name(const Value_type type) -> const char*
{
    const std::size_t index = static_cast<std::size_t>(type);
    ERHE_VERIFY(index < value_type_count);
    return c_value_type_names[index];
}

auto glsl_type_name(const Value_type type) -> const char*
{
    const std::size_t index = static_cast<std::size_t>(type);
    ERHE_VERIFY(index < value_type_count);
    return c_glsl_type_names[index];
}

auto conversion_template(const Value_type from, const Value_type to) -> const char*
{
    const std::size_t from_index = static_cast<std::size_t>(from);
    const std::size_t to_index   = static_cast<std::size_t>(to);
    ERHE_VERIFY(from_index < value_type_count);
    ERHE_VERIFY(to_index   < value_type_count);
    return c_conversion_templates[from_index][to_index];
}

auto convert(const std::string_view expression, const Value_type from, const Value_type to) -> std::string
{
    if (from == to) {
        return std::string{expression};
    }
    const char* const conversion = conversion_template(from, to);
    ERHE_VERIFY(conversion != nullptr);
    std::string result{conversion};
    const std::string placeholder{"$(value)"};
    std::size_t search_position = 0;
    while (true) {
        const std::size_t position = result.find(placeholder, search_position);
        if (position == std::string::npos) {
            break;
        }
        result.replace(position, placeholder.length(), expression);
        // Advance past the inserted expression: it may itself contain the
        // literal text "$(value)", which must not be re-matched (that would
        // loop forever).
        search_position = position + expression.length();
    }
    return result;
}

} // namespace erhe::texgen
