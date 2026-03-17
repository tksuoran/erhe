"""Generate C++ source files with versioned serialize/deserialize from struct schemas."""

from __future__ import annotations

from erhe_codegen.schema import StructSchema, FieldSchema
from erhe_codegen.types import (
    TypeBase, ScalarType, GlmType, VectorType, ArrayType, OptionalType,
    StructRefType, EnumRefType,
)
from erhe_codegen.emit_hpp import _to_snake_case


def _serialize_field_code(f: FieldSchema, indent: str = "    ") -> list[str]:
    """Generate serialization code for a single field."""
    lines: list[str] = []
    key_prefix = f'{indent}out += "\\"{f.name}\\":";'

    lines.append(key_prefix)
    lines.extend(_serialize_value_code(f.type, f"value.{f.name}", indent))
    lines.append(f"{indent}out += ',';")

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
        elif t.name == "Mat4":
            lines.append(f"{indent}erhe::codegen::serialize_mat4(out, {expr});")
    elif isinstance(t, VectorType):
        lines.append(f"{indent}erhe::codegen::serialize_vector(out, {expr});")
    elif isinstance(t, ArrayType):
        lines.append(f"{indent}erhe::codegen::serialize_array(out, {expr});")
    elif isinstance(t, StructRefType):
        # Nested struct: serialize as embedded JSON object
        lines.append(f"{indent}out += serialize({expr});")
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
    elif isinstance(t, (VectorType, ArrayType)):
        lines.append(f"{indent}erhe::codegen::deserialize_field(val, {target});")
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

    lines.append(f'#include "{snake}.hpp"')
    lines.append("")
    lines.append("#include <erhe_codegen/serialize_helpers.hpp>")
    lines.append("#include <erhe_codegen/migration.hpp>")
    lines.append("")

    # --- Serialize ---
    lines.append(f"auto serialize(const {s.name}& value) -> std::string")
    lines.append("{")
    lines.append("    std::string out;")
    lines.append("    out += '{';")
    lines.append(f'    out += "\\"_version\\":{s.version},";')

    active_fields = s.active_fields()
    for f in active_fields:
        lines.extend(_serialize_field_code(f))

    lines.append("    if (out.back() == ',') out.back() = '}'; else out += '}';")
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

    return "\n".join(lines)
