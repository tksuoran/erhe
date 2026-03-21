"""Generate C++ reflection tables (Field_info, Struct_info) for struct schemas."""

from __future__ import annotations

from erhe_codegen.schema import StructSchema, FieldSchema, get_enum_registry
from erhe_codegen.types import (
    TypeBase, ScalarType, GlmType, VectorType, ArrayType, OptionalType,
    StructRefType, EnumRefType,
)
from erhe_codegen.emit_hpp import _to_snake_case


def _c_string_literal(s: str | None) -> str:
    if s is None:
        return "nullptr"
    escaped = s.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def _cpp_type_name(t: TypeBase) -> str:
    """Get the C++ type name string for reflection."""
    return t.cpp_type


def _is_numeric(t: TypeBase) -> bool:
    return isinstance(t, ScalarType) and t.is_numeric


def _is_enum_field(t: TypeBase) -> bool:
    return isinstance(t, EnumRefType)


def _numeric_limits_code(f: FieldSchema, indent: str) -> str:
    """Generate Numeric_limits initializer."""
    if not _is_numeric(f.type):
        return f"{indent}.numeric_limits = {{}},"

    parts = []
    parts.append(f"{indent}.numeric_limits = {{")

    ui_min = f"static_cast<double>({f.ui_min})" if f.ui_min is not None else "0.0"
    ui_max = f"static_cast<double>({f.ui_max})" if f.ui_max is not None else "0.0"
    hard_min = f"static_cast<double>({f.hard_min})" if f.hard_min is not None else "0.0"
    hard_max = f"static_cast<double>({f.hard_max})" if f.hard_max is not None else "0.0"

    parts.append(f"{indent}    .ui_min       = {ui_min},")
    parts.append(f"{indent}    .ui_max       = {ui_max},")
    parts.append(f"{indent}    .hard_min     = {hard_min},")
    parts.append(f"{indent}    .hard_max     = {hard_max},")
    parts.append(f"{indent}    .has_ui_min   = {'true' if f.ui_min is not None else 'false'},")
    parts.append(f"{indent}    .has_ui_max   = {'true' if f.ui_max is not None else 'false'},")
    parts.append(f"{indent}    .has_hard_min = {'true' if f.hard_min is not None else 'false'},")
    parts.append(f"{indent}    .has_hard_max = {'true' if f.hard_max is not None else 'false'},")
    parts.append(f"{indent}}},")

    return "\n".join(parts)


def _enum_info_ptr(t: TypeBase) -> str:
    """Return the enum_info pointer expression, or nullptr."""
    if isinstance(t, EnumRefType):
        return f"&{_to_snake_case(t.name)}_enum_info"
    return "nullptr"


def emit_struct_reflect(s: StructSchema) -> str:
    """Generate reflection tables for a struct. Appended to the .cpp file."""
    lines: list[str] = []
    snake = _to_snake_case(s.name)

    # Need to forward-declare extern enum_info for any EnumRef fields
    enum_refs: set[str] = set()
    for f in s.fields:
        _collect_enum_refs(f.type, enum_refs)

    for enum_name in sorted(enum_refs):
        enum_snake = _to_snake_case(enum_name)
        lines.append(f"extern const erhe::codegen::Enum_info {enum_snake}_enum_info;")

    if enum_refs:
        lines.append("")

    # Field_info array
    lines.append(f"static const erhe::codegen::Field_info {snake}_fields[] = {{")

    for f in s.fields:
        lines.append("    {")
        lines.append(f"        .name          = \"{f.name}\",")
        lines.append(f"        .type_name     = \"{_cpp_type_name(f.type)}\",")
        lines.append(f"        .offset        = offsetof({s.name}, {f.name}),")
        lines.append(f"        .size          = sizeof({_cpp_type_name(f.type)}),")
        lines.append(f"        .added_in      = {f.added_in},")
        lines.append(f"        .removed_in    = {f.removed_in if f.removed_in is not None else 0},")
        lines.append(f"        .short_desc    = {_c_string_literal(f.short_desc)},")
        lines.append(f"        .long_desc     = {_c_string_literal(f.long_desc)},")
        lines.append(f"        .default_value = {_c_string_literal(f.default)},")
        lines.append(_numeric_limits_code(f, "        "))
        lines.append(f"        .is_numeric    = {'true' if _is_numeric(f.type) else 'false'},")
        lines.append(f"        .is_enum       = {'true' if _is_enum_field(f.type) else 'false'},")
        lines.append(f"        .enum_info     = {_enum_info_ptr(f.type)},")
        lines.append("    },")

    lines.append("};")
    lines.append("")

    # Struct_info
    lines.append(f"static const erhe::codegen::Struct_info {snake}_struct_info = {{")
    lines.append(f"    .name    = \"{s.name}\",")
    lines.append(f"    .version = {s.version},")
    lines.append(f"    .fields  = {snake}_fields,")
    lines.append("};")
    lines.append("")

    # get_struct_info
    lines.append(f"auto get_struct_info(const {s.name}*) -> const erhe::codegen::Struct_info&")
    lines.append("{")
    lines.append(f"    return {snake}_struct_info;")
    lines.append("}")
    lines.append("")

    # get_fields
    lines.append(f"auto get_fields(const {s.name}*) -> std::span<const erhe::codegen::Field_info>")
    lines.append("{")
    lines.append(f"    return {snake}_fields;")
    lines.append("}")
    lines.append("")

    return "\n".join(lines)


def _collect_enum_refs(t: TypeBase, refs: set[str]) -> None:
    if isinstance(t, EnumRefType):
        refs.add(t.name)
    elif isinstance(t, (VectorType, ArrayType, OptionalType)):
        _collect_enum_refs(t.element_type, refs)
