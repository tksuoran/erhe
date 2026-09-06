#include "erhe_property/expression.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/enum_info.hpp"

#include "tinyexpr/tinyexpr.h"

#include <fmt/format.h>

#include <cmath>
#include <string>

namespace erhe::property {

namespace {

// Top-level (parenthesis depth zero, outside braces) comma split.
auto split_components(const std::string_view text) -> std::vector<std::string_view>
{
    std::vector<std::string_view> result;
    int         depth = 0;
    bool        brace = false;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (brace) {
            if (c == '}') {
                brace = false;
            }
            continue;
        }
        switch (c) {
            case '{': brace = true; break;
            case '(': ++depth; break;
            case ')': --depth; break;
            case ',': {
                if (depth == 0) {
                    result.push_back(text.substr(start, i - start));
                    start = i + 1;
                }
                break;
            }
            default: break;
        }
    }
    result.push_back(text.substr(start));
    return result;
}

auto trim(std::string_view s) -> std::string_view
{
    while (!s.empty() && ((s.front() == ' ') || (s.front() == '\t') || (s.front() == '\n') || (s.front() == '\r'))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && ((s.back() == ' ') || (s.back() == '\t') || (s.back() == '\n') || (s.back() == '\r'))) {
        s.remove_suffix(1);
    }
    return s;
}

auto component_letter(const int component) -> char
{
    switch (component) {
        case 0: return 'x';
        case 1: return 'y';
        case 2: return 'z';
        case 3: return 'w';
        default: return '?';
    }
}

// `[object/]property[.component]` -> Reference. False with out_error on a
// malformed reference.
auto parse_reference(const std::string_view inner, Expression::Reference& out, std::string& out_error) -> bool
{
    std::string_view rest = trim(inner);
    if (rest.empty()) {
        out_error = "empty reference {}";
        return false;
    }
    const std::size_t slash = rest.rfind('/');
    if (slash != std::string_view::npos) {
        out.object_path = std::string{trim(rest.substr(0, slash))};
        rest            = trim(rest.substr(slash + 1));
    } else {
        out.object_path.clear();
    }
    out.component = -1;
    if ((rest.size() >= 2) && (rest[rest.size() - 2] == '.')) {
        switch (rest.back()) {
            case 'x': out.component = 0; break;
            case 'y': out.component = 1; break;
            case 'z': out.component = 2; break;
            case 'w': out.component = 3; break;
            default: {
                out_error = fmt::format("unknown component '{}' in {{{}}} (x, y, z or w)", rest.back(), inner);
                return false;
            }
        }
        rest = rest.substr(0, rest.size() - 2);
    }
    if (rest.empty() || (rest.find('.') != std::string_view::npos)) {
        out_error = fmt::format("malformed reference {{{}}}: expected [object/]property[.x|.y|.z|.w]", inner);
        return false;
    }
    out.property_name = std::string{rest};
    return true;
}

// Component `component` of `value` as a double. False for a string, or a
// component the value does not have.
auto component_of(const Property_value& value, const int component, double& out) -> bool
{
    switch (type_of(value)) {
        case Property_type::boolean:     if (component != 0) { return false; } out = std::get<bool>(value) ? 1.0 : 0.0; return true;
        case Property_type::integer:     if (component != 0) { return false; } out = static_cast<double>(std::get<int>(value)); return true;
        case Property_type::floating:    if (component != 0) { return false; } out = static_cast<double>(std::get<float>(value)); return true;
        case Property_type::vec2:        if (component > 1) { return false; } out = static_cast<double>(std::get<glm::vec2>(value)[component]); return true;
        case Property_type::vec3:        if (component > 2) { return false; } out = static_cast<double>(std::get<glm::vec3>(value)[component]); return true;
        case Property_type::vec4:        if (component > 3) { return false; } out = static_cast<double>(std::get<glm::vec4>(value)[component]); return true;
        case Property_type::quat:        if (component > 3) { return false; } out = static_cast<double>(std::get<glm::quat>(value)[component]); return true; // glm quat operator[] is x y z w
        case Property_type::string:      return false;
        case Property_type::enumeration: if (component != 0) { return false; } out = static_cast<double>(std::get<Enum_value>(value).value); return true;
        case Property_type::ivec2:       if (component > 1) { return false; } out = static_cast<double>(std::get<glm::ivec2>(value)[component]); return true;
        case Property_type::ivec3:       if (component > 2) { return false; } out = static_cast<double>(std::get<glm::ivec3>(value)[component]); return true;
        case Property_type::ivec4:       if (component > 3) { return false; } out = static_cast<double>(std::get<glm::ivec4>(value)[component]); return true;
        case Property_type::object:      return false;
        case Property_type::double_floating: if (component != 0) { return false; } out = std::get<double>(value); return true;
        case Property_type::mat4:        return false; // 16 components; not a formula target or source
    }
    return false;
}

// Functions beyond tinyexpr's own (abs, ceil, floor, sqrt, pow, exp, ln,
// log10, the trigonometric set, fmod, pi, e): the vector-math and logic
// helpers a formula needs, with comparisons as functions because tinyexpr
// has no comparison operators. Logic values are 0 and 1.
auto fn_min       (const double a, const double b) -> double { return (a < b) ? a : b; }
auto fn_max       (const double a, const double b) -> double { return (a > b) ? a : b; }
auto fn_clamp     (const double x, const double lo, const double hi) -> double { return (x < lo) ? lo : (x > hi) ? hi : x; }
auto fn_lerp      (const double a, const double b, const double t) -> double { return a + (b - a) * t; }
auto fn_step      (const double edge, const double x) -> double { return (x < edge) ? 0.0 : 1.0; }
auto fn_smoothstep(const double e0, const double e1, const double x) -> double
{
    const double t = fn_clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
auto fn_sign  (const double x) -> double { return (x > 0.0) ? 1.0 : (x < 0.0) ? -1.0 : 0.0; }
auto fn_round (const double x) -> double { return std::round(x); }
auto fn_frac  (const double x) -> double { return x - std::floor(x); }
auto fn_deg   (const double x) -> double { return x * 57.29577951308232; }
auto fn_rad   (const double x) -> double { return x * 0.017453292519943295; }
auto fn_lt    (const double a, const double b) -> double { return (a <  b) ? 1.0 : 0.0; }
auto fn_le    (const double a, const double b) -> double { return (a <= b) ? 1.0 : 0.0; }
auto fn_gt    (const double a, const double b) -> double { return (a >  b) ? 1.0 : 0.0; }
auto fn_ge    (const double a, const double b) -> double { return (a >= b) ? 1.0 : 0.0; }
auto fn_eq    (const double a, const double b) -> double { return (a == b) ? 1.0 : 0.0; }
auto fn_ne    (const double a, const double b) -> double { return (a != b) ? 1.0 : 0.0; }
auto fn_not   (const double a) -> double { return (a == 0.0) ? 1.0 : 0.0; }
auto fn_and   (const double a, const double b) -> double { return ((a != 0.0) && (b != 0.0)) ? 1.0 : 0.0; }
auto fn_or    (const double a, const double b) -> double { return ((a != 0.0) || (b != 0.0)) ? 1.0 : 0.0; }
auto fn_select(const double c, const double a, const double b) -> double { return (c != 0.0) ? a : b; }

const te_variable c_functions[] = {
    {"min",        reinterpret_cast<const void*>(&fn_min),        TE_FUNCTION2 | TE_FLAG_PURE, nullptr},
    {"max",        reinterpret_cast<const void*>(&fn_max),        TE_FUNCTION2 | TE_FLAG_PURE, nullptr},
    {"clamp",      reinterpret_cast<const void*>(&fn_clamp),      TE_FUNCTION3 | TE_FLAG_PURE, nullptr},
    {"lerp",       reinterpret_cast<const void*>(&fn_lerp),       TE_FUNCTION3 | TE_FLAG_PURE, nullptr},
    {"step",       reinterpret_cast<const void*>(&fn_step),       TE_FUNCTION2 | TE_FLAG_PURE, nullptr},
    {"smoothstep", reinterpret_cast<const void*>(&fn_smoothstep), TE_FUNCTION3 | TE_FLAG_PURE, nullptr},
    {"sign",       reinterpret_cast<const void*>(&fn_sign),       TE_FUNCTION1 | TE_FLAG_PURE, nullptr},
    {"round",      reinterpret_cast<const void*>(&fn_round),      TE_FUNCTION1 | TE_FLAG_PURE, nullptr},
    {"frac",       reinterpret_cast<const void*>(&fn_frac),       TE_FUNCTION1 | TE_FLAG_PURE, nullptr},
    {"deg",        reinterpret_cast<const void*>(&fn_deg),        TE_FUNCTION1 | TE_FLAG_PURE, nullptr},
    {"rad",        reinterpret_cast<const void*>(&fn_rad),        TE_FUNCTION1 | TE_FLAG_PURE, nullptr},
    {"lt",         reinterpret_cast<const void*>(&fn_lt),         TE_FUNCTION2 | TE_FLAG_PURE, nullptr},
    {"le",         reinterpret_cast<const void*>(&fn_le),         TE_FUNCTION2 | TE_FLAG_PURE, nullptr},
    {"gt",         reinterpret_cast<const void*>(&fn_gt),         TE_FUNCTION2 | TE_FLAG_PURE, nullptr},
    {"ge",         reinterpret_cast<const void*>(&fn_ge),         TE_FUNCTION2 | TE_FLAG_PURE, nullptr},
    {"eq",         reinterpret_cast<const void*>(&fn_eq),         TE_FUNCTION2 | TE_FLAG_PURE, nullptr},
    {"ne",         reinterpret_cast<const void*>(&fn_ne),         TE_FUNCTION2 | TE_FLAG_PURE, nullptr},
    {"not",        reinterpret_cast<const void*>(&fn_not),        TE_FUNCTION1 | TE_FLAG_PURE, nullptr},
    {"and",        reinterpret_cast<const void*>(&fn_and),        TE_FUNCTION2 | TE_FLAG_PURE, nullptr},
    {"or",         reinterpret_cast<const void*>(&fn_or),         TE_FUNCTION2 | TE_FLAG_PURE, nullptr},
    {"select",     reinterpret_cast<const void*>(&fn_select),     TE_FUNCTION3 | TE_FLAG_PURE, nullptr},
};

} // anonymous namespace

auto Expression::Reference::describe() const -> std::string
{
    std::string result = object_path.empty() ? property_name : object_path + "/" + property_name;
    if (component >= 0) {
        result += '.';
        result += component_letter(component);
    }
    return result;
}

auto Expression::component_count(const Property_type type) -> int
{
    switch (type) {
        case Property_type::boolean:     return 1;
        case Property_type::integer:     return 1;
        case Property_type::floating:    return 1;
        case Property_type::vec2:        return 2;
        case Property_type::vec3:        return 3;
        case Property_type::vec4:        return 4;
        case Property_type::quat:        return 4;
        case Property_type::string:      return 0;
        case Property_type::enumeration: return 1;
        case Property_type::ivec2:       return 2;
        case Property_type::ivec3:       return 3;
        case Property_type::ivec4:       return 4;
        case Property_type::object:      return 0;
        case Property_type::double_floating: return 1;
        case Property_type::mat4:        return 0; // not expressible: Expression::compile refuses it
    }
    return 0;
}

auto Expression::compile(const std::string_view text, const Property_type type, std::string& out_error) -> std::unique_ptr<Expression>
{
    const int count = component_count(type);
    if (count == 0) {
        out_error = fmt::format("a {} property cannot be driven by an expression", c_str(type));
        return nullptr;
    }
    const std::vector<std::string_view> formulas = split_components(text);
    if ((formulas.size() != 1) && (formulas.size() != static_cast<std::size_t>(count))) {
        out_error = fmt::format("expected 1 or {} comma-separated components for a {} target, got {}", count, c_str(type), formulas.size());
        return nullptr;
    }

    std::unique_ptr<Expression> expression{new Expression{}};
    expression->m_text = std::string{text};
    expression->m_type = type;

    // Pass 1: collect references (deduplicated by their written form) and
    // rewrite each formula with `(refN)` in place of `{...}`.
    std::vector<std::string> rewritten;
    std::vector<std::string> keys;
    for (std::size_t k = 0; k < formulas.size(); ++k) {
        const std::string_view formula = formulas[k];
        std::string out;
        std::size_t i = 0;
        while (i < formula.size()) {
            const char c = formula[i];
            if (c != '{') {
                out += c;
                ++i;
                continue;
            }
            const std::size_t close = formula.find('}', i + 1);
            if (close == std::string_view::npos) {
                out_error = fmt::format("component {}: unterminated reference '{}'", k + 1, formula.substr(i));
                return nullptr;
            }
            const std::string key{trim(formula.substr(i + 1, close - i - 1))};
            std::size_t slot = 0;
            for (; slot < keys.size(); ++slot) {
                if (keys[slot] == key) {
                    break;
                }
            }
            if (slot == keys.size()) {
                Reference reference;
                if (!parse_reference(key, reference, out_error)) {
                    return nullptr;
                }
                keys.push_back(key);
                expression->m_references.push_back(std::move(reference));
            }
            out += fmt::format("(ref{})", slot);
            i = close + 1;
        }
        if (trim(out).empty()) {
            out_error = fmt::format("component {} is empty", k + 1);
            return nullptr;
        }
        rewritten.push_back(std::move(out));
    }

    // Pass 2: compile against the value slots. The slot vector is sized
    // once here so the addresses tinyexpr binds stay valid.
    expression->m_values.assign(expression->m_references.size(), 0.0);
    std::vector<std::string> names;
    std::vector<te_variable> variables{std::begin(c_functions), std::end(c_functions)};
    names.reserve(expression->m_references.size());
    variables.reserve(variables.size() + expression->m_references.size());
    for (std::size_t slot = 0; slot < expression->m_references.size(); ++slot) {
        names.push_back(fmt::format("ref{}", slot));
    }
    for (std::size_t slot = 0; slot < expression->m_references.size(); ++slot) {
        variables.push_back(te_variable{.name = names[slot].c_str(), .address = &expression->m_values[slot], .type = TE_VARIABLE, .context = nullptr});
    }
    for (std::size_t k = 0; k < rewritten.size(); ++k) {
        int error = 0;
        te_expr* tree = te_compile(rewritten[k].c_str(), variables.data(), static_cast<int>(variables.size()), &error);
        if (tree == nullptr) {
            out_error = (formulas.size() == 1)
                ? fmt::format("syntax error near position {} in '{}'", error, trim(formulas[k]))
                : fmt::format("component {}: syntax error near position {} in '{}'", k + 1, error, trim(formulas[k]));
            return nullptr;
        }
        expression->m_components.push_back(tree);
    }
    return expression;
}

Expression::~Expression() noexcept
{
    for (te_expr* tree : m_components) {
        te_free(tree);
    }
}

auto Expression::clone() const -> std::unique_ptr<Expression>
{
    std::string error;
    return compile(m_text, m_type, error); // the text compiled once, it compiles again
}

auto Expression::has_unresolved_references() const -> bool
{
    for (const Reference& reference : m_references) {
        if (!reference.is_resolved()) {
            return true;
        }
    }
    return false;
}

auto Expression::begin_evaluation() -> bool
{
    if (m_evaluating) {
        m_error = "cycle: the expression reads a property that depends on it";
        return false;
    }
    m_evaluating = true;
    return true;
}

void Expression::end_evaluation()
{
    m_evaluating = false;
}

auto Expression::evaluate(const Enum_info* enum_info) -> std::optional<Property_value>
{
    const int count = component_count(m_type);
    double result[4] = {0.0, 0.0, 0.0, 0.0};
    for (int k = 0; k < count; ++k) {
        te_expr* tree = (m_components.size() == 1) ? m_components.front() : m_components[static_cast<std::size_t>(k)];
        for (std::size_t slot = 0; slot < m_references.size(); ++slot) {
            const Reference& reference = m_references[slot];
            if (!reference.is_resolved()) {
                m_error = fmt::format("unresolved reference {{{}}}", reference.describe());
                return std::nullopt;
            }
            const Property_value source_value = reference.object->get_value(*reference.property);
            // An implicit component follows the component being evaluated;
            // a scalar source broadcasts.
            const int component = (reference.component >= 0) ? reference.component : (component_count(type_of(source_value)) <= 1) ? 0 : k;
            double d = 0.0;
            if (!component_of(source_value, component, d)) {
                m_error = (type_of(source_value) == Property_type::string)
                    ? fmt::format("{{{}}} is a string property", reference.describe())
                    : (type_of(source_value) == Property_type::object)
                    ? fmt::format("{{{}}} is an object reference property", reference.describe())
                    : (type_of(source_value) == Property_type::mat4)
                    ? fmt::format("{{{}}} is a matrix property", reference.describe())
                    : fmt::format("{{{}}} has no component {}", reference.describe(), component_letter(component));
                return std::nullopt;
            }
            m_values[slot] = d;
        }
        const double v = te_eval(tree);
        if (std::isnan(v)) {
            m_error = (count == 1) ? std::string{"result is not a number"} : fmt::format("component {} is not a number", component_letter(k));
            return std::nullopt;
        }
        result[k] = v;
    }

    Property_value value;
    switch (m_type) {
        case Property_type::boolean:  value = (result[0] != 0.0); break;
        case Property_type::integer:  value = static_cast<int>(std::lround(result[0])); break;
        case Property_type::floating: value = static_cast<float>(result[0]); break;
        case Property_type::vec2:     value = glm::vec2{static_cast<float>(result[0]), static_cast<float>(result[1])}; break;
        case Property_type::vec3:     value = glm::vec3{static_cast<float>(result[0]), static_cast<float>(result[1]), static_cast<float>(result[2])}; break;
        case Property_type::vec4:     value = glm::vec4{static_cast<float>(result[0]), static_cast<float>(result[1]), static_cast<float>(result[2]), static_cast<float>(result[3])}; break;
        case Property_type::quat:     value = glm::quat{static_cast<float>(result[3]), static_cast<float>(result[0]), static_cast<float>(result[1]), static_cast<float>(result[2])}; break; // glm ctor is (w, x, y, z)
        case Property_type::enumeration: {
            const int32_t v = static_cast<int32_t>(std::lround(result[0]));
            if ((enum_info != nullptr) && !enum_info->contains(v)) {
                m_error = fmt::format("{} is not a {} value", v, enum_info->get_type_name());
                return std::nullopt;
            }
            value = Enum_value{v};
            break;
        }
        case Property_type::string: return std::nullopt;
        case Property_type::object: return std::nullopt;
        case Property_type::double_floating: value = result[0]; break;
        case Property_type::mat4: return std::nullopt; // component_count() is 0, so compile() already refused
        case Property_type::ivec2: value = glm::ivec2{static_cast<int>(std::lround(result[0])), static_cast<int>(std::lround(result[1]))}; break;
        case Property_type::ivec3: value = glm::ivec3{static_cast<int>(std::lround(result[0])), static_cast<int>(std::lround(result[1])), static_cast<int>(std::lround(result[2]))}; break;
        case Property_type::ivec4: value = glm::ivec4{static_cast<int>(std::lround(result[0])), static_cast<int>(std::lround(result[1])), static_cast<int>(std::lround(result[2])), static_cast<int>(std::lround(result[3]))}; break;
    }
    m_error.clear();
    return value;
}

auto validate_expression_text(const Dependency_property& property, const std::string_view text, std::string& out_error) -> bool
{
    const std::unique_ptr<Expression> expression = Expression::compile(text, property.get_type(), out_error);
    if (!expression) {
        return false;
    }
    for (const Expression::Reference& reference : expression->get_references()) {
        if (reference.object_path.empty() && (reference.property_name == property.get_name())) {
            out_error = "the expression references its own target";
            return false;
        }
    }
    return true;
}

} // namespace erhe::property
