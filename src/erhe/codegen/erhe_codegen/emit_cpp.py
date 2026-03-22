"""Generate C++ source files with versioned serialize/deserialize from struct schemas."""

from __future__ import annotations

from erhe_codegen.schema import StructSchema, FieldSchema
from erhe_codegen.types import (
    TypeBase, ScalarType, GlmType, VectorType, ArrayType, OptionalType,
    StructRefType, EnumRefType,
)
from erhe_codegen.emit_hpp import _to_snake_case, _collect_struct_refs


def _can_use_serialize_template(t: TypeBase) -> bool:
    """Check if a type can use the serialize_element/deserialize_field template overloads."""
    return isinstance(t, (ScalarType, GlmType))


def _deserialize_element_code(t: TypeBase, target: str, indent: str) -> list[str]:
    """Generate deserialization for an element inside a vector/array loop (uses elem_val)."""
    lines: list[str] = []
    if isinstance(t, StructRefType):
        lines.append(f"{indent}simdjson::ondemand::object nested_obj;")
        lines.append(f"{indent}if (!elem_val.get_object().get(nested_obj)) {{")
        lines.append(f"{indent}    deserialize(nested_obj, {target});")
        lines.append(f"{indent}}}")
    elif isinstance(t, EnumRefType):
        lines.append(f"{indent}std::string_view str;")
        lines.append(f"{indent}if (!elem_val.get_string().get(str)) {{")
        lines.append(f"{indent}    from_string(str, {target});")
        lines.append(f"{indent}}}")
    else:
        lines.append(f"{indent}erhe::codegen::deserialize_field(elem_val, {target});")
    return lines


def _serialize_field_code(f: FieldSchema, indent: str = "    ") -> list[str]:
    """Generate serialization code for a single field."""
    lines: list[str] = []
    lines.append(f"{indent}out.append((indent + 1) * 4, ' ');")
    lines.append(f'{indent}out += "\\"{f.name}\\": ";')
    lines.extend(_serialize_value_code(f.type, f"value.{f.name}", indent))
    lines.append(f'{indent}out += ",\\n";')

    return lines


def _serialize_value_code(t: TypeBase, expr: str, indent: str) -> list[str]:
    """Generate serialization code for a value expression of given type."""
    lines: list[str] = []

    if isinstance(t, ScalarType):
        if t.cpp_type == "bool":
            lines.append(f"{indent}erhe::codegen::serialize_bool(out, {expr});")
        elif t.cpp_type == "std::string":
            lines.append(f"{indent}erhe::codegen::serialize_string(out, {expr});")
        elif t.cpp_type == "float":
            lines.append(f"{indent}erhe::codegen::serialize_float(out, {expr});")
        elif t.cpp_type == "double":
            lines.append(f"{indent}erhe::codegen::serialize_double(out, {expr});")
        elif t.cpp_type in ("int", "int8_t", "int16_t", "int32_t", "int64_t"):
            lines.append(f"{indent}erhe::codegen::serialize_int(out, static_cast<int64_t>({expr}));")
        elif t.cpp_type in ("unsigned int", "uint8_t", "uint16_t", "uint32_t", "uint64_t"):
            lines.append(f"{indent}erhe::codegen::serialize_uint(out, static_cast<uint64_t>({expr}));")
    elif isinstance(t, GlmType):
        if t.name == "Vec2":
            lines.append(f"{indent}erhe::codegen::serialize_vec2(out, {expr});")
        elif t.name == "Vec3":
            lines.append(f"{indent}erhe::codegen::serialize_vec3(out, {expr});")
        elif t.name == "Vec4":
            lines.append(f"{indent}erhe::codegen::serialize_vec4(out, {expr});")
        elif t.name == "IVec2":
            lines.append(f"{indent}erhe::codegen::serialize_ivec2(out, {expr});")
        elif t.name == "Mat4":
            lines.append(f"{indent}erhe::codegen::serialize_mat4(out, {expr});")
    elif isinstance(t, VectorType):
        if _can_use_serialize_template(t.element_type):
            lines.append(f"{indent}erhe::codegen::serialize_vector(out, {expr});")
        elif isinstance(t.element_type, StructRefType):
            # Multi-line array of objects
            lines.append(f'{indent}out += "[\\n";')
            lines.append(f"{indent}for (std::size_t i = 0; i < {expr}.size(); ++i) {{")
            lines.append(f'{indent}    if (i > 0) out += ",\\n";')
            lines.append(f"{indent}    out.append((indent + 2) * 4, ' ');")
            lines.append(f"{indent}    out += serialize({expr}[i], indent + 2);")
            lines.append(f"{indent}}}")
            lines.append(f"{indent}if (!{expr}.empty()) out += '\\n';")
            lines.append(f"{indent}out.append((indent + 1) * 4, ' ');")
            lines.append(f"{indent}out += ']';")
        else:
            lines.append(f"{indent}out += '[';")
            lines.append(f"{indent}for (std::size_t i = 0; i < {expr}.size(); ++i) {{")
            lines.append(f"{indent}    if (i > 0) out += ',';")
            lines.extend(_serialize_value_code(t.element_type, f"{expr}[i]", indent + "    "))
            lines.append(f"{indent}}}")
            lines.append(f"{indent}out += ']';")
    elif isinstance(t, ArrayType):
        if _can_use_serialize_template(t.element_type):
            lines.append(f"{indent}erhe::codegen::serialize_array(out, {expr});")
        elif isinstance(t.element_type, StructRefType):
            # Multi-line array of objects
            lines.append(f'{indent}out += "[\\n";')
            lines.append(f"{indent}for (std::size_t i = 0; i < {t.size}; ++i) {{")
            lines.append(f'{indent}    if (i > 0) out += ",\\n";')
            lines.append(f"{indent}    out.append((indent + 2) * 4, ' ');")
            lines.append(f"{indent}    out += serialize({expr}[i], indent + 2);")
            lines.append(f"{indent}}}")
            lines.append(f"{indent}out += '\\n';")
            lines.append(f"{indent}out.append((indent + 1) * 4, ' ');")
            lines.append(f"{indent}out += ']';")
        else:
            lines.append(f"{indent}out += '[';")
            lines.append(f"{indent}for (std::size_t i = 0; i < {t.size}; ++i) {{")
            lines.append(f"{indent}    if (i > 0) out += ',';")
            lines.extend(_serialize_value_code(t.element_type, f"{expr}[i]", indent + "    "))
            lines.append(f"{indent}}}")
            lines.append(f"{indent}out += ']';")
    elif isinstance(t, StructRefType):
        # Nested struct: serialize as embedded JSON object
        lines.append(f"{indent}out += serialize({expr}, indent + 1);")
    elif isinstance(t, EnumRefType):
        # Enum: serialize as string via to_string
        lines.append(f"{indent}erhe::codegen::serialize_string(out, to_string({expr}));")
    elif isinstance(t, OptionalType):
        # Optional: serialize value or null
        lines.append(f"{indent}if ({expr}.has_value()) {{")
        lines.extend(_serialize_value_code(t.element_type, f"{expr}.value()", indent + "    "))
        lines.append(f"{indent}}} else {{")
        lines.append(f"{indent}    out += \"null\";")
        lines.append(f"{indent}}}")

    return lines


def _deserialize_field_code(f: FieldSchema, indent: str = "    ") -> list[str]:
    """Generate deserialization code for a single field with version guard."""
    lines: list[str] = []

    # Version guard
    conditions: list[str] = []
    if f.added_in > 1:
        conditions.append(f"version >= {f.added_in}")
    if f.removed_in is not None:
        conditions.append(f"version < {f.removed_in}")

    guard = " && ".join(conditions) if conditions else ""

    if guard:
        lines.append(f"{indent}// {f.name}: added_in={f.added_in}" +
                      (f", removed_in={f.removed_in}" if f.removed_in else ""))
        lines.append(f"{indent}if ({guard}) {{")
        inner = indent + "    "
    else:
        lines.append(f"{indent}// {f.name}: added_in={f.added_in}")
        inner = indent

    # Field access + deserialization
    lines.append(f'{inner}if (!obj["{f.name}"].get(val)) {{')
    lines.extend(_deserialize_value_code(f.type, f"out.{f.name}", inner + "    "))

    lines.append(f"{inner}}}")

    if guard:
        lines.append(f"{indent}}}")

    return lines


def _deserialize_value_code(t: TypeBase, target: str, indent: str) -> list[str]:
    """Generate deserialization code for a value."""
    lines: list[str] = []

    if isinstance(t, (ScalarType, GlmType)):
        lines.append(f"{indent}erhe::codegen::deserialize_field(val, {target});")
    elif isinstance(t, VectorType):
        if _can_use_serialize_template(t.element_type):
            lines.append(f"{indent}erhe::codegen::deserialize_field(val, {target});")
        else:
            elem_cpp = t.element_type.cpp_type
            lines.append(f"{indent}{{")
            lines.append(f"{indent}    simdjson::ondemand::array arr;")
            lines.append(f"{indent}    if (!val.get_array().get(arr)) {{")
            lines.append(f"{indent}        {target}.clear();")
            lines.append(f"{indent}        for (auto element : arr) {{")
            lines.append(f"{indent}            {elem_cpp} item{{}};")
            lines.append(f"{indent}            simdjson::ondemand::value elem_val;")
            lines.append(f"{indent}            if (!element.get(elem_val)) {{")
            # Recurse for the element deserialization, using elem_val as the value
            lines.extend(_deserialize_element_code(t.element_type, "item", indent + "                "))
            lines.append(f"{indent}            }}")
            lines.append(f"{indent}            {target}.push_back(std::move(item));")
            lines.append(f"{indent}        }}")
            lines.append(f"{indent}    }}")
            lines.append(f"{indent}}}")
    elif isinstance(t, ArrayType):
        if _can_use_serialize_template(t.element_type):
            lines.append(f"{indent}erhe::codegen::deserialize_field(val, {target});")
        else:
            elem_cpp = t.element_type.cpp_type
            lines.append(f"{indent}{{")
            lines.append(f"{indent}    simdjson::ondemand::array arr;")
            lines.append(f"{indent}    if (!val.get_array().get(arr)) {{")
            lines.append(f"{indent}        std::size_t i = 0;")
            lines.append(f"{indent}        for (auto element : arr) {{")
            lines.append(f"{indent}            if (i >= {t.size}) break;")
            lines.append(f"{indent}            simdjson::ondemand::value elem_val;")
            lines.append(f"{indent}            if (!element.get(elem_val)) {{")
            lines.extend(_deserialize_element_code(t.element_type, f"{target}[i]", indent + "                "))
            lines.append(f"{indent}            }}")
            lines.append(f"{indent}            ++i;")
            lines.append(f"{indent}        }}")
            lines.append(f"{indent}    }}")
            lines.append(f"{indent}}}")
    elif isinstance(t, StructRefType):
        # Nested struct: get as object, call deserialize recursively
        lines.append(f"{indent}simdjson::ondemand::object nested_obj;")
        lines.append(f"{indent}if (!val.get_object().get(nested_obj)) {{")
        lines.append(f"{indent}    deserialize(nested_obj, {target});")
        lines.append(f"{indent}}}")
    elif isinstance(t, EnumRefType):
        # Enum: read as string, call from_string
        lines.append(f"{indent}std::string_view str;")
        lines.append(f"{indent}if (!val.get_string().get(str)) {{")
        lines.append(f"{indent}    from_string(str, {target});")
        lines.append(f"{indent}}}")
    elif isinstance(t, OptionalType):
        # Optional: null → nullopt, otherwise deserialize inner value
        inner_cpp = t.element_type.cpp_type
        lines.append(f"{indent}if (val.is_null()) {{")
        lines.append(f"{indent}    {target} = std::nullopt;")
        lines.append(f"{indent}}} else {{")
        lines.append(f"{indent}    {inner_cpp} tmp{{}};")
        # For the inner deserialization, we need to use 'val' and target 'tmp'
        lines.extend(_deserialize_value_code(t.element_type, "tmp", indent + "    "))
        lines.append(f"{indent}    {target} = std::move(tmp);")
        lines.append(f"{indent}}}")

    return lines


def emit_struct_cpp(s: StructSchema) -> str:
    """Generate the C++ source for a struct (serialize + deserialize)."""
    lines: list[str] = []
    snake = _to_snake_case(s.name)

    lines.append(f'#include "{snake}_serialization.hpp"')

    # Include serialization headers for referenced structs (needed for their serialize/deserialize)
    struct_refs = _collect_struct_refs(s)
    for ref in struct_refs:
        lines.append(f'#include "{_to_snake_case(ref)}_serialization.hpp"')

    lines.append("")
    lines.append("#include <cstddef>")
    lines.append("")
    lines.append("#include <erhe_codegen/serialize_helpers.hpp>")
    lines.append("#include <erhe_codegen/migration.hpp>")
    lines.append("")

    # Suppress deprecation warnings for removed fields accessed in deserialize/reflection
    has_deprecated = any(f.removed_in is not None for f in s.fields)
    if has_deprecated:
        lines.append("#if defined(_MSC_VER)")
        lines.append("#   pragma warning(push)")
        lines.append('#   pragma warning(disable : 4996)')
        lines.append("#elif defined(__GNUC__) || defined(__clang__)")
        lines.append('#   pragma GCC diagnostic push')
        lines.append('#   pragma GCC diagnostic ignored "-Wdeprecated-declarations"')
        lines.append("#endif")
        lines.append("")

    # --- Serialize ---
    lines.append(f"auto serialize(const {s.name}& value, int indent) -> std::string")
    lines.append("{")
    lines.append("    std::string out;")
    lines.append('    out += "{\\n";')
    lines.append("    out.append((indent + 1) * 4, ' ');")
    lines.append(f'    out += "\\"_version\\": {s.version},\\n";')

    active_fields = s.active_fields()
    for f in active_fields:
        lines.extend(_serialize_field_code(f))

    lines.append("    if (out.size() >= 2 && out[out.size()-2] == ',' && out.back() == '\\n') {")
    lines.append("        out.resize(out.size() - 2);")
    lines.append("        out += '\\n';")
    lines.append("    }")
    lines.append("    out.append(indent * 4, ' ');")
    lines.append("    out += '}';")
    lines.append("    return out;")
    lines.append("}")
    lines.append("")

    # --- Deserialize ---
    lines.append(f"auto deserialize(simdjson::ondemand::object obj, {s.name}& out) -> simdjson::error_code")
    lines.append("{")
    lines.append("    uint64_t version = 1;")
    lines.append("    {")
    lines.append("        simdjson::ondemand::value v;")
    lines.append('        if (!obj["_version"].get(v)) {')
    lines.append("            v.get_uint64().get(version);")
    lines.append("        }")
    lines.append("    }")
    lines.append("")
    lines.append("    simdjson::ondemand::value val;")
    lines.append("")

    for f in s.fields:
        lines.extend(_deserialize_field_code(f))
        lines.append("")

    # Migration callback
    lines.append(f"    if (version != {s.name}::current_version) {{")
    lines.append(f"        erhe::codegen::run_migrations(")
    lines.append(f"            out,")
    lines.append(f"            static_cast<uint32_t>(version),")
    lines.append(f"            {s.name}::current_version")
    lines.append(f"        );")
    lines.append(f"    }}")
    lines.append("")
    lines.append("    return simdjson::SUCCESS;")
    lines.append("}")
    lines.append("")

    # --- Reflection ---
    from erhe_codegen.emit_reflect import emit_struct_reflect
    lines.append(emit_struct_reflect(s))

    if has_deprecated:
        lines.append("#if defined(_MSC_VER)")
        lines.append("#   pragma warning(pop)")
        lines.append("#elif defined(__GNUC__) || defined(__clang__)")
        lines.append("#   pragma GCC diagnostic pop")
        lines.append("#endif")
        lines.append("")

    return "\n".join(lines)
