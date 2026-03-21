"""Generate C++ header files from struct schemas."""

from __future__ import annotations
from typing import Optional

from erhe_codegen.schema import StructSchema, FieldSchema
from erhe_codegen.types import (
    TypeBase, ScalarType, GlmType, VectorType, ArrayType, OptionalType,
    StructRefType, EnumRefType, String,
)


def _needs_include(types: set[type], type_names: set[str]) -> dict[str, bool]:
    """Determine which standard headers are needed."""
    return {
        "array":   ArrayType in types,
        "cstdint": any(t in type_names for t in [
            "int8_t", "uint8_t", "int16_t", "uint16_t",
            "int32_t", "uint32_t", "int64_t", "uint64_t",
        ]),
        "span":    True,  # always needed for get_fields return type
        "string":  any(
            isinstance(t, ScalarType) and t.cpp_type == "std::string"
            for t in _all_leaf_types_in(types)
        ) or String in _all_leaf_types_in(types),
        "vector":  VectorType in types,
        "glm":     GlmType in types,
    }


def _all_leaf_types_in(types: set[type]) -> set:
    """Helper — not actually useful with type classes, just return the set."""
    return types


def _collect_type_classes(s: StructSchema) -> tuple[set[type], set[str]]:
    """Collect the set of type classes and C++ type name strings used in a struct."""
    type_classes: set[type] = set()
    cpp_types: set[str] = set()

    for f in s.fields:
        _collect_from_type(f.type, type_classes, cpp_types)

    return type_classes, cpp_types


def _collect_from_type(t: TypeBase, type_classes: set[type], cpp_types: set[str]) -> None:
    if isinstance(t, ScalarType):
        cpp_types.add(t.cpp_type)
        if t.cpp_type == "std::string":
            type_classes.add(type(String))  # mark string needed
    elif isinstance(t, GlmType):
        type_classes.add(GlmType)
    elif isinstance(t, VectorType):
        type_classes.add(VectorType)
        _collect_from_type(t.element_type, type_classes, cpp_types)
    elif isinstance(t, ArrayType):
        type_classes.add(ArrayType)
        _collect_from_type(t.element_type, type_classes, cpp_types)
    elif isinstance(t, StructRefType):
        type_classes.add(StructRefType)
    elif isinstance(t, EnumRefType):
        type_classes.add(EnumRefType)


def _cpp_type_str(t: TypeBase) -> str:
    """Get the C++ type string for a type."""
    return t.cpp_type


def _version_comment(f: FieldSchema) -> str:
    """Generate a version range comment like // v1+ or // v2..v3"""
    if f.removed_in is not None:
        return f"// v{f.added_in}..v{f.removed_in}"
    return f"// v{f.added_in}+"


def _default_initializer(f: FieldSchema) -> str:
    """Generate the default member initializer."""
    if f.default is not None:
        return "{" + f.default + "}"
    return "{}"


def _needs_string_include(s: StructSchema) -> bool:
    for f in s.fields:
        if _type_needs_string(f.type):
            return True
    return False


def _type_needs_string(t: TypeBase) -> bool:
    if isinstance(t, ScalarType) and t.cpp_type == "std::string":
        return True
    if isinstance(t, (VectorType, ArrayType, OptionalType)):
        return _type_needs_string(t.element_type)
    return False


def _needs_vector_include(s: StructSchema) -> bool:
    return any(_type_contains(f.type, VectorType) for f in s.fields)


def _needs_array_include(s: StructSchema) -> bool:
    return any(_type_contains(f.type, ArrayType) for f in s.fields)


def _needs_optional_include(s: StructSchema) -> bool:
    return any(_type_contains(f.type, OptionalType) for f in s.fields)


def _type_contains(t: TypeBase, cls: type) -> bool:
    """Check if a type is or contains the given type class."""
    if isinstance(t, cls):
        return True
    if isinstance(t, (VectorType, ArrayType, OptionalType)):
        return _type_contains(t.element_type, cls)
    return False


def _needs_glm_include(s: StructSchema) -> bool:
    for f in s.fields:
        if _type_needs_glm(f.type):
            return True
    return False


def _type_needs_glm(t: TypeBase) -> bool:
    if isinstance(t, GlmType):
        return True
    if isinstance(t, (VectorType, ArrayType, OptionalType)):
        return _type_needs_glm(t.element_type)
    return False


def _needs_cstdint(s: StructSchema) -> bool:
    cstdint_types = {
        "int8_t", "uint8_t", "int16_t", "uint16_t",
        "int32_t", "uint32_t", "int64_t", "uint64_t",
    }
    for f in s.fields:
        if isinstance(f.type, ScalarType) and f.type.cpp_type in cstdint_types:
            return True
    return False


def _collect_struct_refs(s: StructSchema) -> list[str]:
    """Collect StructRef names for forward includes."""
    refs: list[str] = []
    for f in s.fields:
        _collect_refs_from_type(f.type, refs)
    return sorted(set(refs))


def _collect_enum_refs(s: StructSchema) -> list[str]:
    """Collect EnumRef names for includes."""
    refs: list[str] = []
    for f in s.fields:
        _collect_enum_refs_from_type(f.type, refs)
    return sorted(set(refs))


def _collect_refs_from_type(t: TypeBase, refs: list[str]) -> None:
    if isinstance(t, StructRefType):
        refs.append(t.name)
    elif isinstance(t, (VectorType, ArrayType, OptionalType)):
        _collect_refs_from_type(t.element_type, refs)


def _collect_enum_refs_from_type(t: TypeBase, refs: list[str]) -> None:
    if isinstance(t, EnumRefType):
        refs.append(t.name)
    elif isinstance(t, (VectorType, ArrayType, OptionalType)):
        _collect_enum_refs_from_type(t.element_type, refs)


def _to_snake_case(name: str) -> str:
    """Convert PascalCase to snake_case."""
    result = []
    for i, c in enumerate(name):
        if c.isupper() and i > 0 and name[i - 1] != '_':
            result.append('_')
        result.append(c.lower())
    return ''.join(result)


def emit_struct_hpp(s: StructSchema) -> str:
    """Generate the C++ header for a struct."""
    lines: list[str] = []
    lines.append("#pragma once")
    lines.append("")

    # Standard includes
    includes: list[str] = []
    if _needs_array_include(s):
        includes.append("<array>")
    includes.append("<cstdint>")  # always for current_version
    if _needs_optional_include(s):
        includes.append("<optional>")
    includes.append("<span>")     # always for get_fields
    if _needs_string_include(s):
        includes.append("<string>")
    if _needs_vector_include(s):
        includes.append("<vector>")

    for inc in includes:
        lines.append(f"#include {inc}")

    lines.append("")

    # Library includes
    if _needs_glm_include(s):
        lines.append("#include <glm/glm.hpp>")
    lines.append("#include <simdjson.h>")
    lines.append("")
    lines.append("#include <erhe_codegen/field_info.hpp>")

    # StructRef includes
    struct_refs = _collect_struct_refs(s)
    enum_refs = _collect_enum_refs(s)
    if struct_refs or enum_refs:
        lines.append("")
    for ref in struct_refs:
        lines.append(f'#include "{_to_snake_case(ref)}.hpp"')
    for ref in enum_refs:
        lines.append(f'#include "{_to_snake_case(ref)}.hpp"')

    lines.append("")

    # Struct declaration
    lines.append(f"struct {s.name}")
    lines.append("{")
    lines.append(f"    static constexpr uint32_t current_version = {s.version};")
    lines.append("")

    for f in s.fields:
        cpp_type = _cpp_type_str(f.type)
        default = _default_initializer(f)
        comment = _version_comment(f)

        if f.is_removed():
            lines.append(f'    [[deprecated("removed in v{f.removed_in}")]]')

        lines.append(f"    {cpp_type} {f.name}{default}; {comment}")

    lines.append("};")
    lines.append("")

    # Function declarations
    lines.append(f"auto serialize  (const {s.name}& value, int indent = 0) -> std::string;")
    lines.append(f"auto deserialize(simdjson::ondemand::object obj, {s.name}& out) -> simdjson::error_code;")
    lines.append("")
    lines.append(f"auto get_struct_info(const {s.name}*) -> const erhe::codegen::Struct_info&;")
    lines.append(f"auto get_fields     (const {s.name}*) -> std::span<const erhe::codegen::Field_info>;")
    lines.append("")

    return "\n".join(lines)
