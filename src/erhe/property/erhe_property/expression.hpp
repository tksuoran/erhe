#pragma once

#include "erhe_property/property_value.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

struct te_expr;

namespace erhe::property {

class Dependency_object;
class Dependency_property;
class Enum_info;

// The formula text of a driven property (doc/property_system.md D22),
// kept distinct from a string value so the local layer of a property can be
// either.
class Expression_text
{
public:
    std::string text;
    [[nodiscard]] auto operator==(const Expression_text&) const -> bool = default;
};

// The exact local layer of a property on an object: a stored value or a
// formula. nullopt (outside the variant) means "no local value".
using Local_state = std::variant<Property_value, Expression_text>;

// A compiled formula driving one property (D22): a comma-separated list of
// tinyexpr expressions, one per component of the target type (one formula
// broadcasts to every component), with references written as
// `{[object/]property[.component]}`. The Expression owns the parse and the
// evaluation; Dependency_object resolves the references and keeps the
// dependent lists (see Dependency_object::evaluate_expression).
class Expression
{
public:
    class Reference
    {
    public:
        std::string                object_path;   // "" = the target's own object, ".." = its inheritance parent, else an item name
        std::string                property_name;
        int                        component{-1}; // 0..3 for x y z w, -1 = the component being evaluated
        Dependency_object*         object  {nullptr}; // resolved by Dependency_object; null = unresolved
        const Dependency_property* property{nullptr};

        [[nodiscard]] auto is_resolved() const -> bool { return (object != nullptr) && (property != nullptr); }
        [[nodiscard]] auto describe   () const -> std::string;
    };

    // Parses `text` for a target of `type`. Returns nullptr and fills
    // out_error on a syntax error, an empty component, a component count
    // that is neither one nor the target's, or a string target.
    [[nodiscard]] static auto compile(std::string_view text, Property_type type, std::string& out_error) -> std::unique_ptr<Expression>;

    Expression(const Expression&) = delete;
    Expression& operator=(const Expression&) = delete;
    ~Expression() noexcept;

    // A fresh compile of the same text: references unresolved.
    [[nodiscard]] auto clone() const -> std::unique_ptr<Expression>;

    [[nodiscard]] auto get_text      () const -> std::string_view       { return m_text; }
    [[nodiscard]] auto get_type      () const -> Property_type          { return m_type; }
    [[nodiscard]] auto get_error     () const -> std::string_view       { return m_error; }
    [[nodiscard]] auto get_references()       -> std::vector<Reference>& { return m_references; }
    [[nodiscard]] auto get_references() const -> const std::vector<Reference>& { return m_references; }
    [[nodiscard]] auto has_unresolved_references() const -> bool;

    void set_error(std::string error) { m_error = std::move(error); }

    // Re-entry guard spanning evaluation and the notification of its
    // result: begin_evaluation is false (and get_error() says cycle) while
    // an evaluation of this expression is already in progress up the stack.
    [[nodiscard]] auto begin_evaluation() -> bool;
    void               end_evaluation  ();

    // Evaluates with the resolved references. nullopt (and get_error())
    // when a reference is unresolved, a component is missing on a source,
    // the result is not a number, or an enumeration result is outside its
    // table.
    [[nodiscard]] auto evaluate(const Enum_info* enum_info) -> std::optional<Property_value>;

    [[nodiscard]] static auto component_count(Property_type type) -> int;

private:
    Expression() = default;

    std::string            m_text;
    Property_type          m_type{Property_type::floating};
    std::string            m_error;
    std::vector<Reference> m_references;
    std::vector<double>    m_values;     // one slot per reference, bound by tinyexpr; never resized after compile
    std::vector<te_expr*>  m_components; // one compiled tree per component formula
    bool                   m_evaluating{false};
};

// The checks set_expression makes before touching the object: `text`
// compiles for the property's type and does not reference its own target
// on the same object. For callers that want the message up front (MCP, the
// Properties row, the startup command).
[[nodiscard]] auto validate_expression_text(const Dependency_property& property, std::string_view text, std::string& out_error) -> bool;

} // namespace erhe::property
