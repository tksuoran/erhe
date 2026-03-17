"""Generate C++ header and source files for enum definitions."""

from __future__ import annotations

from erhe_codegen.schema import EnumSchema
from erhe_codegen.emit_hpp import _to_snake_case


def _needs_cstdint(e: EnumSchema) -> bool:
    cstdint_types = {
        "int8_t", "uint8_t", "int16_t", "uint16_t",
        "int32_t", "uint32_t", "int64_t", "uint64_t",
    }
    return e.underlying_type.cpp_type in cstdint_types


def _c_string_literal(s: str | None) -> str:
    if s is None:
        return "nullptr"
    escaped = s.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def emit_enum_hpp(e: EnumSchema) -> str:
    """Generate the C++ header for an enum."""
    lines: list[str] = []
    lines.append("#pragma once")
    lines.append("")

    if _needs_cstdint(e):
        lines.append("#include <cstdint>")
    lines.append("#include <string_view>")
    lines.append("")
    lines.append("#include <erhe_codegen/field_info.hpp>")
    lines.append("")

    # enum class declaration
    underlying = e.underlying_type.cpp_type
    lines.append(f"enum class {e.name} : {underlying}")
    lines.append("{")
    for v in e.values:
        lines.append(f"    {v.name} = {v.numeric_value},")
    lines.append("};")
    lines.append("")

    # String conversion declarations
    lines.append("// String conversion (for serialization)")
    lines.append(f"auto to_string  ({e.name} value) -> std::string_view;")
    lines.append(f"auto from_string(std::string_view str, {e.name}& out) -> bool;")
    lines.append("")

    # Reflection declaration
    lines.append("// Reflection")
    lines.append(f"auto get_enum_info(const {e.name}*) -> const erhe::codegen::Enum_info&;")
    lines.append("")

    return "\n".join(lines)


def emit_enum_cpp(e: EnumSchema) -> str:
    """Generate the C++ source for an enum."""
    lines: list[str] = []
    snake = _to_snake_case(e.name)

    lines.append(f'#include "{snake}.hpp"')
    lines.append("")

    # to_string
    lines.append(f"auto to_string({e.name} value) -> std::string_view")
    lines.append("{")
    lines.append("    switch (value) {")
    for v in e.values:
        lines.append(f'        case {e.name}::{v.name}: return "{v.name}";')
    lines.append("    }")
    if e.values:
        lines.append(f'    return "{e.values[0].name}"; // fallback to first value')
    else:
        lines.append(f'    return "";')
    lines.append("}")
    lines.append("")

    # from_string
    lines.append(f"auto from_string(std::string_view str, {e.name}& out) -> bool")
    lines.append("{")
    for v in e.values:
        lines.append(f'    if (str == "{v.name}") {{ out = {e.name}::{v.name}; return true; }}')
    lines.append("    return false; // unknown string, out unchanged")
    lines.append("}")
    lines.append("")

    # Enum_value_info reflection table
    lines.append(f"static constexpr erhe::codegen::Enum_value_info {snake}_values[] = {{")
    for v in e.values:
        short = _c_string_literal(v.short_desc)
        long = _c_string_literal(v.long_desc)
        lines.append(
            f"    {{ .name = \"{v.name}\", .value = {v.numeric_value}, "
            f".short_desc = {short}, .long_desc = {long} }},"
        )
    lines.append("};")
    lines.append("")

    # Enum_info
    underlying_name = e.underlying_type.cpp_type
    lines.append(f"const erhe::codegen::Enum_info {snake}_enum_info = {{")
    lines.append(f'    .name                 = "{e.name}",')
    lines.append(f'    .underlying_type_name = "{underlying_name}",')
    lines.append(f"    .values               = {snake}_values,")
    lines.append("};")
    lines.append("")

    # get_enum_info
    lines.append(f"auto get_enum_info(const {e.name}*) -> const erhe::codegen::Enum_info&")
    lines.append("{")
    lines.append(f"    return {snake}_enum_info;")
    lines.append("}")
    lines.append("")

    return "\n".join(lines)
