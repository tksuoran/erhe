#include "erhe_texgen/composer.hpp"

#include "erhe_texgen/common_library.hpp"
#include "erhe_texgen/compose_node.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <array>
#include <cmath>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace erhe::texgen {

namespace {

// Word characters for $identifier parsing. The engine scans templates
// right-to-left (like Material Maker's replace_variables): the rightmost
// '$' is resolved first, so the parameters of an outer $input(...) call are
// already fully substituted by the time the input itself is resolved.
// Identifier extraction takes the longest run of word characters after '$',
// so "$uv_scale" resolves the variable "uv_scale" and never partially
// matches "uv"; the parenthesized form "$(name)" allows gluing a variable
// against adjacent identifier text ("blend_$(mode)").
[[nodiscard]] auto is_word_letter(const char c) -> bool
{
    return
        ((c >= 'a') && (c <= 'z')) ||
        ((c >= 'A') && (c <= 'Z')) ||
        ((c >= '0') && (c <= '9')) ||
        (c == '_');
}

[[nodiscard]] auto is_space(const char c) -> bool
{
    return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\n');
}

// Returns the index of the parenthesis closing the one at open_position,
// or std::string::npos when unbalanced.
[[nodiscard]] auto find_matching_parenthesis(const std::string& text, const std::size_t open_position) -> std::size_t
{
    int level = 0;
    for (std::size_t i = open_position, end = text.length(); i < end; ++i) {
        const char c = text[i];
        if (c == '(') {
            ++level;
        } else if (c == ')') {
            --level;
            if (level == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

// Rewrites every "$rnd(a, b)" occurrence into
// "param_rnd(a, b, $seed + <offset>)" and every "$rndi(a, b)" occurrence
// into "param_rndi(a, b, $seed + <offset>)", where <offset> is derived from
// the occurrence's character position plus a per-template-string base:
// sin(position + offset_base) * 0.5 + 0.5 (like Material Maker's
// replace_rnd). "$rndi(" is matched before "$rnd(" (longest match wins).
// Occurrences are rewritten left to right, each using its own position in
// the partially rewritten string, so every occurrence gets a distinct
// deterministic offset. (Material Maker collapses textually identical calls
// to one offset; distinct per-occurrence offsets are deliberately chosen
// here so repeated $rnd(a, b) calls do not correlate.) After a rewrite the
// search resumes from the replacement's start so "$rnd(" occurrences nested
// inside the copied argument text are also found; the replacement's own
// "param_rnd(" prefix cannot re-match "$rnd(", and each rewrite removes one
// occurrence, so the loop terminates.
//
// offset_base decorrelates the different template strings of one node whose
// "$rnd(" happens to sit at the same character position (Material Maker adds
// 17 per processed string; see rnd_offset in gen_shader.gd).
[[nodiscard]] auto replace_rnd(const std::string& input, const int offset_base) -> std::string
{
    std::string text = input;
    std::size_t search_position = 0;
    while (true) {
        const std::size_t position = text.find("$rnd", search_position);
        if (position == std::string::npos) {
            break;
        }
        const char* function_name{nullptr};
        std::size_t open_position{0};
        if (text.compare(position, 6, "$rndi(") == 0) {
            function_name = "param_rndi";
            open_position = position + 5; // index of '('
        } else if (text.compare(position, 5, "$rnd(") == 0) {
            function_name = "param_rnd";
            open_position = position + 4; // index of '('
        } else {
            search_position = position + 1; // e.g. "$rnd_foo" - a plain variable
            continue;
        }
        const std::size_t close_position = find_matching_parenthesis(text, open_position);
        if (close_position == std::string::npos) {
            break; // unbalanced - leave as-is, substitution will surface the error
        }
        const std::string parameters = text.substr(open_position + 1, close_position - open_position - 1);
        const double offset =
            (std::sin(static_cast<double>(position) + static_cast<double>(offset_base)) * 0.5) + 0.5;
        const std::string replacement = fmt::format("{}({}, $seed + {:.6f})", function_name, parameters, offset);
        text = text.substr(0, position) + replacement + text.substr(close_position + 1);
        search_position = position; // re-scan the copied argument text for nested occurrences
    }
    return text;
}

// Deterministic per-template-string offset bases for replace_rnd, 17 apart
// like Material Maker's rnd_offset (gen_shader.gd::process_parameters). The
// base is a stable function of the string's slot in the descriptor layout -
// not a running counter - so a given occurrence keeps the same offset across
// repeated samplings and uv variants of the node, while occurrences at equal
// character positions in different template strings still decorrelate.
// Slot layout: enum parameter code fragments (parameter index), then the
// inline code stanza, then output expressions, then input default
// expressions.
[[nodiscard]] auto rnd_offset_base_for_parameter(const Node_descriptor&, const std::size_t parameter_index) -> int
{
    return 17 * static_cast<int>(parameter_index);
}

[[nodiscard]] auto rnd_offset_base_for_code(const Node_descriptor& descriptor) -> int
{
    return 17 * static_cast<int>(descriptor.parameters.size());
}

[[nodiscard]] auto rnd_offset_base_for_output(const Node_descriptor& descriptor, const std::size_t output_index) -> int
{
    return 17 * static_cast<int>(descriptor.parameters.size() + 1 + output_index);
}

[[nodiscard]] auto rnd_offset_base_for_input_default(const Node_descriptor& descriptor, const std::size_t input_index) -> int
{
    return 17 * static_cast<int>(descriptor.parameters.size() + 1 + descriptor.outputs.size() + input_index);
}

// ---------------------------------------------------------------------------
// Gradient / curve parameter codegen.
//
// A gradient/curve parameter emits a per-node GLSL helper function whose name
// the "$param" substitution resolves to, so an output expression such as
// colorize's "$gradient($input($uv))" becomes "o5_gradient_gradient(<input>)".
//
// DECISION (Phase 4, doc/texture-graph-plan.md): the control points are baked
// into the function body as GLSL constants rather than uploaded through uniform
// arrays. Any value edit therefore recomposes the source and recompiles - but
// the SPIR-V cache de-duplicates unchanged sources, and this keeps the codegen
// pure string logic (no std140 array-uniform layout, no per-frame upload path).
// The uniform-array live-update path (value edits that skip recompile) is a
// future optimization; see erhe_texgen notes.md.
//
// GLSL ported from Material Maker's MMGradient.get_shader / MMCurve.get_shader
// (types/gradient.gd, types/curve.gd, MIT license), with pv()/pc()/p_*_x baked
// to literals instead of uniform-array element accesses.
// ---------------------------------------------------------------------------

[[nodiscard]] auto gradient_float_literal(const float value) -> std::string
{
    return fmt::format("{:.9f}", value);
}

[[nodiscard]] auto gradient_color_literal(const std::array<float, 4>& c) -> std::string
{
    return fmt::format("vec4({:.9f}, {:.9f}, {:.9f}, {:.9f})", c[0], c[1], c[2], c[3]);
}

[[nodiscard]] auto emit_gradient_function(
    const std::string&                function_name,
    const std::vector<Gradient_stop>& stops,
    const Gradient_interpolation      interpolation
) -> std::string
{
    std::string shader = fmt::format("vec4 {}(float x) {{\n", function_name);
    if (stops.empty()) {
        shader += "    return vec4(0.0, 0.0, 0.0, 1.0);\n}\n";
        return shader;
    }
    // Compose_node::set_gradient guarantees stops are sorted with strictly
    // increasing positions, so (pos[i+1]-pos[i]) is never zero below.
    const std::size_t s  = stops.size() - 1;
    const auto        pv = [&](const std::size_t i) { return gradient_float_literal(stops[i].position); };
    const auto        pc = [&](const std::size_t i) { return gradient_color_literal(stops[i].color); };

    switch (interpolation) {
        case Gradient_interpolation::constant: {
            if (stops.size() == 1) {
                shader += fmt::format("    return {};\n", pc(0));
                break;
            }
            shader += fmt::format("    if (x < {}) {{\n", pv(1));
            shader += fmt::format("        return {};\n", pc(0));
            for (std::size_t i = 1; i < s; ++i) {
                shader += fmt::format("    }} else if (x < {}) {{\n", pv(i + 1));
                shader += fmt::format("        return {};\n", pc(i));
            }
            shader += "    }\n";
            shader += fmt::format("    return {};\n", pc(s));
            break;
        }
        case Gradient_interpolation::linear:
        case Gradient_interpolation::smoothstep: {
            // Material Maker's "function" prefix opens a parenthesis that the
            // mix() call closes; linear leaves the interpolant raw, smoothstep
            // wraps it in 0.5-0.5*cos(PI*t).
            const char* fn = (interpolation == Gradient_interpolation::linear)
                ? "("
                : "0.5-0.5*cos(3.14159265359*";
            shader += fmt::format("    if (x < {}) {{\n", pv(0));
            shader += fmt::format("        return {};\n", pc(0));
            for (std::size_t i = 0; i < s; ++i) {
                shader += fmt::format("    }} else if (x < {}) {{\n", pv(i + 1));
                shader += fmt::format(
                    "        return mix({}, {}, {}(x-{})/({}-{})));\n",
                    pc(i), pc(i + 1), fn, pv(i), pv(i + 1), pv(i)
                );
            }
            shader += "    }\n";
            shader += fmt::format("    return {};\n", pc(s));
            break;
        }
        case Gradient_interpolation::cubic: {
            shader += fmt::format("    if (x < {}) {{\n", pv(0));
            shader += fmt::format("        return {};\n", pc(0));
            for (std::size_t i = 0; i < s; ++i) {
                shader += fmt::format("    }} else if (x < {}) {{\n", pv(i + 1));
                const std::string dx = fmt::format("(x-{})/({}-{})", pv(i), pv(i + 1), pv(i));
                const std::string b  = fmt::format("mix({}, {}, {})", pc(i), pc(i + 1), dx);
                if (i > 0) {
                    const std::string a =
                        fmt::format("mix({}, {}, (x-{})/({}-{}))", pc(i - 1), pc(i), pv(i - 1), pv(i), pv(i - 1));
                    if (i < (s - 1)) {
                        const std::string c =
                            fmt::format("mix({}, {}, (x-{})/({}-{}))", pc(i + 1), pc(i + 2), pv(i + 1), pv(i + 2), pv(i + 1));
                        const std::string ac = fmt::format("mix({}, {}, 0.5-0.5*cos(3.14159265359*{}))", a, c, dx);
                        shader += fmt::format("        return 0.5*({} + {});\n", b, ac);
                    } else {
                        shader += fmt::format("        return mix({}, {}, 0.5+0.5*{});\n", a, b, dx);
                    }
                } else if (i < (s - 1)) {
                    const std::string c =
                        fmt::format("mix({}, {}, (x-{})/({}-{}))", pc(i + 1), pc(i + 2), pv(i + 1), pv(i + 2), pv(i + 1));
                    shader += fmt::format("        return mix({}, {}, 1.0-0.5*{});\n", c, b, dx);
                } else {
                    shader += fmt::format("        return {};\n", b);
                }
            }
            shader += "    }\n";
            shader += fmt::format("    return {};\n", pc(s));
            break;
        }
        default: {
            shader += "    return vec4(0.0, 0.0, 0.0, 1.0);\n";
            break;
        }
    }
    shader += "}\n";
    return shader;
}

[[nodiscard]] auto emit_curve_function(
    const std::string&              function_name,
    const std::vector<Curve_point>& points
) -> std::string
{
    std::string shader = fmt::format("float {}(float x) {{\n", function_name);
    if (points.empty()) {
        shader += "    return x;\n}\n"; // no control points: pass the input through
        return shader;
    }
    if (points.size() == 1) {
        shader += fmt::format("    return {};\n}}\n", gradient_float_literal(points[0].y));
        return shader;
    }
    // Compose_node::set_curve guarantees strictly increasing x, so no segment
    // divides by a zero span. Ported from MMCurve.get_shader: consecutive
    // guarded blocks, each returning a Hermite segment; the last (i == size-2)
    // block is unguarded and acts as the fall-through.
    const std::size_t size = points.size();
    const auto px = [&](const std::size_t i) { return gradient_float_literal(points[i].x); };
    const auto py = [&](const std::size_t i) { return gradient_float_literal(points[i].y); };
    const auto pls = [&](const std::size_t i) { return gradient_float_literal(points[i].left_slope); };
    const auto prs = [&](const std::size_t i) { return gradient_float_literal(points[i].right_slope); };
    for (std::size_t i = 0; (i + 1) < size; ++i) {
        if (i < (size - 2)) {
            shader += fmt::format("    if (x <= {}) ", px(i + 1));
        }
        shader += "{\n";
        shader += fmt::format("        float dx = x - {};\n", px(i));
        shader += fmt::format("        float d = {} - {};\n", px(i + 1), px(i));
        shader += "        float t = dx/d;\n";
        shader += "        float omt = (1.0 - t);\n";
        shader += "        float omt2 = omt * omt;\n";
        shader += "        float omt3 = omt2 * omt;\n";
        shader += "        float t2 = t * t;\n";
        shader += "        float t3 = t2 * t;\n";
        shader += "        d /= 3.0;\n";
        shader += fmt::format("        float y1 = {};\n", py(i));
        shader += fmt::format("        float yac = {} + d*{};\n", py(i), prs(i));
        shader += fmt::format("        float ybc = {} - d*{};\n", py(i + 1), pls(i + 1));
        shader += fmt::format("        float y2 = {};\n", py(i + 1));
        shader += "        return y1*omt3 + yac*omt2*t*3.0 + ybc*omt*t2*3.0 + y2*t3;\n";
        shader += "    }\n";
    }
    shader += "}\n";
    return shader;
}

// True when the whole string is one parenthesized group ("(...)").
[[nodiscard]] auto is_fully_parenthesized(const std::string& text) -> bool
{
    return
        (text.length() >= 2) &&
        (text.front() == '(') &&
        (find_matching_parenthesis(text, 0) == (text.length() - 1));
}

// Zero-ish literal of a value type, used as the error expression when
// composition must bail out (cycles, excessive depth): the marker rides in a
// comment so the shader still assembles textually.
[[nodiscard]] auto zero_literal(const Value_type type) -> const char*
{
    switch (type) {
        case Value_type::grayscale: return "0.0";
        case Value_type::rgb:       return "vec3(0.0)";
        case Value_type::rgba:      return "vec4(0.0)";
        default:                    return "0.0";
    }
}

class Variant_lookup
{
public:
    std::size_t index {0};
    bool        is_new{false};
};

// Port of Material Maker's MMGenContext (gen_context.gd, MIT license).
// Tracks, per node, which "variants" (uv expressions / output samplings)
// have already been emitted, so repeated samplings of the same subtree at
// the same coordinates share one code stanza and its locals, while a
// different coordinate expression creates a new variant with distinct local
// names. A child context (used for input helper function bodies) sees the
// parent's declaration state but numbers its variants independently.
class Variant_context
{
public:
    explicit Variant_context(Variant_context* parent = nullptr)
        : m_parent{parent}
    {
    }

    // True when the node has been processed in this context or any ancestor;
    // used to generate per-node declarations (uniforms, globals, input helper
    // functions) exactly once.
    [[nodiscard]] auto has_variant(const Compose_node* node) const -> bool
    {
        if (m_variants.contains(node)) {
            return true;
        }
        return (m_parent != nullptr) && m_parent->has_variant(node);
    }

    // Looks up variant in this context's per-node list, appending it when
    // new. Returns the variant index and whether it was newly added.
    [[nodiscard]] auto get_variant(const Compose_node* node, const std::string& variant) -> Variant_lookup
    {
        std::vector<std::string>& variants = m_variants[node];
        for (std::size_t i = 0, end = variants.size(); i < end; ++i) {
            if (variants[i] == variant) {
                return Variant_lookup{.index = i, .is_new = false};
            }
        }
        variants.push_back(variant);
        touch(node);
        return Variant_lookup{.index = variants.size() - 1, .is_new = true};
    }

private:
    // Marks the node as present (declarations generated) in this context and
    // all ancestors, without registering any variant string there.
    void touch(const Compose_node* node)
    {
        m_variants.try_emplace(node);
        if (m_parent != nullptr) {
            m_parent->touch(node);
        }
    }

    std::map<const Compose_node*, std::vector<std::string>> m_variants;
    Variant_context*                                        m_parent{nullptr};
};

// Per-node substitution scope: the simple string variables ($uv, $name,
// parameters, ...) plus the node's input names (resolved through
// replace_input()).
class Node_scope
{
public:
    const Compose_node*                node{nullptr};
    std::map<std::string, std::string> variables;
    std::map<std::string, std::size_t> inputs; // input name -> input index
};

class Compose_engine
{
public:
    explicit Compose_engine(const Compose_options& options)
        : m_options{options}
    {
    }

    [[nodiscard]] auto compose_node(
        const Compose_node& node,
        std::size_t         output_index,
        const std::string&  uv,
        Variant_context&    context
    ) -> Shader_code;

private:
    [[nodiscard]] auto replace_variables(
        const std::string& text,
        Node_scope&        scope,
        Shader_code&       rv,
        Variant_context&   context
    ) -> std::string;

    [[nodiscard]] auto replace_input(
        Node_scope&        scope,
        std::size_t        input_index,
        const std::string& parameters,
        Shader_code&       rv,
        Variant_context&   context
    ) -> std::string;

    void generate_input_function(
        Node_scope&      scope,
        std::size_t      input_index,
        Shader_code&     rv,
        Variant_context& context
    );

    // Registers one parameter/seed uniform on rv exactly once (deduplicated
    // by name inside Shader_code) and returns the accessor-prefixed reference
    // string to place into the substitution scope.
    [[nodiscard]] auto register_uniform(
        Shader_code&                rv,
        const std::string&          uniform_name,
        Uniform_kind                kind,
        const std::array<float, 4>& value,
        bool                        generate_declarations
    ) -> std::string;

    // Builds a Shader_code whose output expression is the error marker
    // rendered as the requested type's zero-ish literal (used to break
    // cycles / excessive recursion depth without crashing).
    [[nodiscard]] auto make_error_code(const char* marker, Value_type type) -> Shader_code;

    const Compose_options&        m_options;
    std::set<const Compose_node*> m_active;         // nodes on the current recursion path
    int                           m_depth{0};       // current recursion depth
    static constexpr int          s_max_depth{256}; // hard cap for pathologically deep acyclic chains
};

auto Compose_engine::register_uniform(
    Shader_code&                rv,
    const std::string&          uniform_name,
    const Uniform_kind          kind,
    const std::array<float, 4>& value,
    const bool                  generate_declarations
) -> std::string
{
    if (generate_declarations) {
        rv.add_uniform(Uniform{.name = uniform_name, .kind = kind, .value = value});
    }
    return m_options.uniform_accessor_prefix + uniform_name;
}

auto Compose_engine::make_error_code(const char* const marker, const Value_type type) -> Shader_code
{
    Shader_code rv{};
    rv.set_output(fmt::format("/* (error: {}) */ {}", marker, zero_literal(type)), type);
    return rv;
}

auto Compose_engine::replace_variables(
    const std::string& text,
    Node_scope&        scope,
    Shader_code&       rv,
    Variant_context&   context
) -> std::string
{
    std::string head = text;
    std::string tail{};
    while (true) {
        const std::size_t dollar_position = head.rfind('$');
        if (dollar_position == std::string::npos) {
            break;
        }
        std::size_t variable_end{0};
        std::string variable{};
        if (((dollar_position + 1) < head.length()) && (head[dollar_position + 1] == '(')) {
            const std::size_t close_position = find_matching_parenthesis(head, dollar_position + 1);
            if (close_position == std::string::npos) {
                variable_end = head.length();
                variable     = head.substr(dollar_position + 2);
            } else {
                variable_end = close_position + 1;
                variable     = head.substr(dollar_position + 2, close_position - dollar_position - 2);
            }
        } else {
            variable_end = dollar_position + 1;
            while ((variable_end < head.length()) && is_word_letter(head[variable_end])) {
                ++variable_end;
            }
            variable = head.substr(dollar_position + 1, variable_end - dollar_position - 1);
        }
        tail = head.substr(variable_end) + tail;
        head = head.substr(0, dollar_position);

        const auto input_iterator = scope.inputs.find(variable);
        if (input_iterator != scope.inputs.end()) {
            // $input_name(coordinate_expression)
            std::size_t open_position = 0;
            while ((open_position < tail.length()) && is_space(tail[open_position])) {
                ++open_position;
            }
            if ((open_position >= tail.length()) || (tail[open_position] != '(')) {
                head += fmt::format("(error: input {} used without coordinates)", variable);
                continue;
            }
            const std::size_t close_position = find_matching_parenthesis(tail, open_position);
            if (close_position == std::string::npos) {
                head += fmt::format("(error: unterminated coordinates for input {})", variable);
                continue;
            }
            // Keep the surrounding parentheses so the coordinate expression
            // stays a single unit; collapse one level when it is already
            // fully parenthesized ("((x))" -> "(x)"), like Material Maker.
            std::string parameters = tail.substr(open_position, close_position - open_position + 1);
            if (
                (parameters.length() >= 4) &&
                (parameters[1] == '(') &&
                (find_matching_parenthesis(parameters, 1) == (parameters.length() - 2))
            ) {
                parameters = parameters.substr(1, parameters.length() - 2);
            }
            tail = tail.substr(close_position + 1);
            head += replace_input(scope, input_iterator->second, parameters, rv, context);
            continue;
        }
        const auto variable_iterator = scope.variables.find(variable);
        if (variable_iterator != scope.variables.end()) {
            head += variable_iterator->second;
        } else {
            head += fmt::format("(error: {} not found)", variable);
        }
    }
    return head + tail;
}

auto Compose_engine::replace_input(
    Node_scope&        scope,
    const std::size_t  input_index,
    const std::string& parameters,
    Shader_code&       rv,
    Variant_context&   context
) -> std::string
{
    const Compose_node&     node       = *scope.node;
    const Node_descriptor&  descriptor = node.get_descriptor();
    const Input_descriptor& input_def  = descriptor.inputs[input_index];
    const Input_binding&    binding    = node.get_input(input_index);
    if (binding.source == nullptr) {
        // Unconnected: use the default expression with $uv rebound to the
        // sampling coordinates. $rnd/$rndi in the default is rewritten first
        // (using the owning node's seed, resolved through
        // scope.variables["seed"]). Default expressions are authored in the
        // input's own type, so no type conversion is applied.
        const std::string old_uv = scope.variables["uv"];
        scope.variables["uv"] = parameters;
        const std::string replaced = replace_variables(
            replace_rnd(input_def.default_expression, rnd_offset_base_for_input_default(descriptor, input_index)),
            scope,
            rv,
            context
        );
        scope.variables["uv"] = old_uv;
        // Parenthesize so precedence matches the connected path, which yields
        // an atomic local. Skip the wrap when the result is already one
        // parenthesized group (avoids "((x))").
        if (is_fully_parenthesized(replaced)) {
            return replaced;
        }
        return fmt::format("({})", replaced);
    }
    if (input_def.function) {
        // The helper function was emitted by generate_input_function();
        // substitute a call.
        return fmt::format("o{}_input_{}{}", node.get_id(), input_def.name, parameters);
    }
    // Inline: compose the upstream subtree with uv bound to the sampling
    // coordinates and merge its accumulated pieces.
    const Shader_code source_code = compose_node(*binding.source, binding.output_index, parameters, context);
    rv.add_uniforms(source_code);
    rv.add_globals(source_code);
    rv.append_defs(source_code.get_defs());
    rv.append_code(source_code.get_code());
    return convert(source_code.get_output_expression(), source_code.get_output_type(), input_def.type);
}

void Compose_engine::generate_input_function(
    Node_scope&       scope,
    const std::size_t input_index,
    Shader_code&      rv,
    Variant_context&  context
)
{
    const Compose_node&     node      = *scope.node;
    const Input_descriptor& input_def = node.get_descriptor().inputs[input_index];
    const Input_binding&    binding   = node.get_input(input_index);
    if (binding.source == nullptr) {
        return; // unconnected function inputs fall back to the inline default expression
    }
    // The function body composes the upstream subtree against the function's
    // own "uv" parameter. A child variant context isolates the function-local
    // variant numbering while still marking upstream declarations as done.
    Variant_context local_context{&context};
    const Shader_code source_code = compose_node(*binding.source, binding.output_index, "uv", local_context);
    rv.add_uniforms(source_code);
    rv.add_globals(source_code);
    rv.append_defs(source_code.get_defs());
    const std::string return_expression = convert(
        source_code.get_output_expression(),
        source_code.get_output_type(),
        input_def.type
    );
    rv.append_defs(
        fmt::format(
            "{} o{}_input_{}(vec2 uv) {{\n{}    return {};\n}}\n",
            glsl_type_name(input_def.type),
            node.get_id(),
            input_def.name,
            source_code.get_code(),
            return_expression
        )
    );
}

auto Compose_engine::compose_node(
    const Compose_node& node,
    const std::size_t   output_index,
    const std::string&  uv,
    Variant_context&    context
) -> Shader_code
{
    const Node_descriptor& descriptor = node.get_descriptor();
    ERHE_VERIFY(output_index < descriptor.outputs.size());

    // Cycle guard: m_active is the set of nodes currently being composed on
    // this recursion path (a path set, not a visited set - a node is removed
    // when its subtree finishes, so a diamond reached via two paths still
    // composes). Re-entering an active node means a cycle in the graph; emit a
    // deterministic error expression of the expected type and stop recursing.
    if (m_active.contains(&node)) {
        return make_error_code("cycle", descriptor.outputs[output_index].type);
    }
    // Hard recursion-depth cap for pathologically deep acyclic chains.
    if (m_depth >= s_max_depth) {
        return make_error_code("max depth", descriptor.outputs[output_index].type);
    }

    // RAII scope guard: keeps m_active / m_depth balanced across every early
    // return below.
    class Active_scope
    {
    public:
        Active_scope(std::set<const Compose_node*>& active, int& depth, const Compose_node* node)
            : m_active{active}
            , m_depth {depth}
            , m_node  {node}
        {
            m_active.insert(m_node);
            ++m_depth;
        }
        ~Active_scope()
        {
            m_active.erase(m_node);
            --m_depth;
        }
        Active_scope(const Active_scope&)            = delete;
        Active_scope& operator=(const Active_scope&) = delete;
        Active_scope(Active_scope&&)                 = delete;
        Active_scope& operator=(Active_scope&&)      = delete;
    private:
        std::set<const Compose_node*>& m_active;
        int&                           m_depth;
        const Compose_node*            m_node;
    };
    const Active_scope active_scope{m_active, m_depth, &node};

    Shader_code rv{};
    const bool generate_declarations = !context.has_variant(&node);
    const std::string genname = fmt::format("o{}", node.get_id());

    Node_scope scope{};
    scope.node = &node;
    scope.variables["uv"]      = uv;
    scope.variables["time"]    = "elapsed_time";
    scope.variables["name"]    = genname;
    scope.variables["node_id"] = std::to_string(node.get_id());
    if (descriptor.uses_seed()) {
        const std::string seed_uniform_name = fmt::format("p_{}_seed", genname);
        scope.variables["seed"] = register_uniform(
            rv,
            seed_uniform_name,
            Uniform_kind::float_uniform,
            {node.get_seed(), 0.0f, 0.0f, 0.0f},
            generate_declarations
        );
    }

    // Parameters
    for (std::size_t i = 0, end = descriptor.parameters.size(); i < end; ++i) {
        const Parameter_descriptor& parameter = descriptor.parameters[i];
        const Parameter_value&      value     = node.get_parameter_value(i);
        switch (parameter.kind) {
            case Parameter_kind::float_parameter: {
                const std::string uniform_name = fmt::format("p_{}_{}", genname, parameter.name);
                scope.variables[parameter.name] = register_uniform(
                    rv,
                    uniform_name,
                    Uniform_kind::float_uniform,
                    {value.float_value, 0.0f, 0.0f, 0.0f},
                    generate_declarations
                );
                break;
            }
            case Parameter_kind::color_parameter: {
                const std::string uniform_name = fmt::format("p_{}_{}", genname, parameter.name);
                scope.variables[parameter.name] = register_uniform(
                    rv,
                    uniform_name,
                    Uniform_kind::vec4_uniform,
                    value.color_value,
                    generate_declarations
                );
                break;
            }
            case Parameter_kind::enum_parameter: {
                std::string code_fragment{};
                if (!parameter.enum_values.empty()) {
                    const std::size_t enum_index =
                        (value.enum_index < parameter.enum_values.size()) ? value.enum_index : 0;
                    // Apply $rnd/$rndi rewriting to the selected enum code
                    // fragment so a $rnd living only inside an enum value works
                    // (its $seed then resolves through scope.variables["seed"]).
                    code_fragment = replace_rnd(
                        parameter.enum_values[enum_index].code,
                        rnd_offset_base_for_parameter(descriptor, i)
                    );
                }
                scope.variables[parameter.name] = code_fragment;
                break;
            }
            case Parameter_kind::bool_parameter: {
                scope.variables[parameter.name] = value.bool_value ? "true" : "false";
                break;
            }
            case Parameter_kind::size_parameter: {
                scope.variables[parameter.name] =
                    fmt::format("{:.1f}", std::pow(2.0, static_cast<double>(value.size_exponent)));
                break;
            }
            case Parameter_kind::gradient_parameter: {
                // $param resolves to the per-node gradient function name; the
                // function (with baked control points) is emitted once.
                const std::string function_name = fmt::format("{}_{}_gradient", genname, parameter.name);
                scope.variables[parameter.name] = function_name;
                if (generate_declarations) {
                    rv.add_global(emit_gradient_function(function_name, value.gradient_stops, value.gradient_interpolation));
                }
                break;
            }
            case Parameter_kind::curve_parameter: {
                const std::string function_name = fmt::format("{}_{}_curve", genname, parameter.name);
                scope.variables[parameter.name] = function_name;
                if (generate_declarations) {
                    rv.add_global(emit_curve_function(function_name, value.curve_points));
                }
                break;
            }
            default: {
                ERHE_FATAL("Compose_engine: unsupported parameter kind");
            }
        }
    }

    // Inputs: emit helper functions for function-form inputs (once per node),
    // and register input names for $input_name(coord) resolution.
    for (std::size_t i = 0, end = descriptor.inputs.size(); i < end; ++i) {
        const Input_descriptor& input_def = descriptor.inputs[i];
        if (generate_declarations && input_def.function) {
            generate_input_function(scope, i, rv, context);
        }
        scope.inputs[input_def.name] = i;
    }

    // Global function library (deduplicated by exact content downstream)
    if (generate_declarations && !descriptor.global.empty()) {
        rv.add_global(descriptor.global);
    }

    // Inline code stanza: one per (node, uv) variant. $name_uv is the
    // variant-unique local name prefix.
    if (!descriptor.code.empty()) {
        const Variant_lookup code_variant = context.get_variant(&node, uv);
        scope.variables["name_uv"] = fmt::format("{}_{}", genname, code_variant.index);
        if (code_variant.is_new) {
            const std::string code = replace_variables(
                replace_rnd(descriptor.code, rnd_offset_base_for_code(descriptor)), scope, rv, context
            );
            if (!code.empty()) {
                rv.append_code(code);
                if (code.back() != '\n') {
                    rv.append_code("\n");
                }
            }
        }
    }

    // Output expression: evaluated into a local variable, once per
    // (node, uv, output_index) variant; repeated samplings reuse the local.
    const Output_descriptor& output = descriptor.outputs[output_index];
    const std::string variant_string = fmt::format("{},{}", uv, output_index);
    const Variant_lookup output_variant = context.get_variant(&node, variant_string);
    const std::string output_variable = fmt::format(
        "{}_{}_{}_{}", genname, output_index, output_variant.index, value_type_name(output.type)
    );
    if (output_variant.is_new) {
        const std::string expression = replace_variables(
            replace_rnd(output.expression, rnd_offset_base_for_output(descriptor, output_index)), scope, rv, context
        );
        rv.append_code(fmt::format("{} {} = {};\n", glsl_type_name(output.type), output_variable, expression));
    }
    rv.set_output(output_variable, output.type);
    return rv;
}

} // anonymous namespace

Composer::Composer(Compose_options options)
    : m_options{std::move(options)}
{
}

auto Composer::get_options() const -> const Compose_options&
{
    return m_options;
}

auto Composer::compose(const Compose_node& sink_node, const std::size_t output_index) const -> Shader_code
{
    Compose_engine engine{m_options};
    Variant_context context{};
    return engine.compose_node(sink_node, output_index, "uv", context);
}

auto Composer::assemble_fragment(const Shader_code& shader_code) const -> std::string
{
    std::string body{};
    for (const std::string& global_source : shader_code.get_globals()) {
        body += global_source;
        if (!global_source.empty() && (global_source.back() != '\n')) {
            body += "\n";
        }
    }
    if (m_options.uniform_declaration_mode == Uniform_declaration_mode::plain_uniforms) {
        for (const Uniform& uniform : shader_code.get_uniforms()) {
            switch (uniform.kind) {
                case Uniform_kind::float_uniform: {
                    body += fmt::format("uniform float {} = {:.9f};\n", uniform.name, uniform.value[0]);
                    break;
                }
                case Uniform_kind::vec4_uniform: {
                    body += fmt::format(
                        "uniform vec4 {} = vec4({:.9f}, {:.9f}, {:.9f}, {:.9f});\n",
                        uniform.name, uniform.value[0], uniform.value[1], uniform.value[2], uniform.value[3]
                    );
                    break;
                }
                default: {
                    ERHE_FATAL("Composer: unsupported uniform kind");
                }
            }
        }
    }
    body += shader_code.get_defs();
    body += fmt::format("void {}() {{\n", m_options.function_name);
    body += fmt::format("vec2 uv = {};\n", m_options.uv_source_expression);
    body += shader_code.get_code();
    const std::string result_expression = convert(
        shader_code.get_output_expression(),
        shader_code.get_output_type(),
        Value_type::rgba
    );
    body += fmt::format("{} = {};\n", m_options.output_variable_name, result_expression);
    body += "}\n";

    const bool include_common_library =
        (m_options.common_library_mode == Common_library_mode::always) ||
        (
            (m_options.common_library_mode == Common_library_mode::when_referenced) &&
            references_common_library(body)
        );
    if (include_common_library) {
        return std::string{common_library_glsl()} + "\n" + body;
    }
    return body;
}

} // namespace erhe::texgen
