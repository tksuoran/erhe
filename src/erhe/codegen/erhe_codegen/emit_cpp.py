"""Generate C++ source files with versioned serialize/deserialize from struct schemas."""

from __future__ import annotations

from erhe_codegen.schema import StructSchema, FieldSchema, get_struct_registry
from erhe_codegen.types import (
    TypeBase, ScalarType, GlmType, VectorType, ArrayType, OptionalType,
    MapType, StructRefType, EnumRefType,
)
from erhe_codegen.emit_hpp import _to_snake_case, _collect_struct_refs


def _can_use_serialize_template(t: TypeBase) -> bool:
    """Check if a type can use the serialize_element/deserialize_field template overloads."""
    return isinstance(t, (ScalarType, GlmType))


def _at_default_condition(f: FieldSchema) -> str:
    """C++ bool expression that is true iff value.<name> equals its current default.

    Used both to suppress default-valued fields during omit-defaults serialization and
    to build the per-struct is_default() predicate. Types without a meaningful scalar
    default (arrays, enums without a default) return "false" so they are always emitted.
    """
    t = f.type
    expr = f"value.{f.name}"
    if isinstance(t, ScalarType):
        if t.cpp_type == "std::string":
            if f.default is None or f.default == '""':
                return f"{expr}.empty()"
            return f"{expr} == ({f.default})"
        if f.default is not None:
            return f"{expr} == ({f.default})"
        return f"{expr} == ({t.cpp_type}{{}})"
    if isinstance(t, GlmType):
        # glm defaults are the braced-initializer contents (e.g. "1.0f" broadcasts, or
        # "1.0f, 0.5f, 0.0f"); construct the glm type so the comparison type-matches the
        # member initializer ({cpp_type}{default}), not a bare scalar.
        inner = f.default if f.default is not None else ""
        return f"{expr} == ({t.cpp_type}{{{inner}}})"
    if isinstance(t, VectorType):
        return f"{expr}.empty()"
    if isinstance(t, MapType):
        return f"{expr}.empty()"
    if isinstance(t, OptionalType):
        return f"!{expr}.has_value()"
    if isinstance(t, StructRefType):
        return f"is_default({expr})"
    if isinstance(t, EnumRefType):
        if f.default is not None:
            return f"{expr} == ({f.default})"
        return "false"
    # ArrayType and anything else: no per-element default mechanism, always emit.
    return "false"


def _has_float_comparison(s: StructSchema) -> bool:
    """True if is_default()/omit-defaults serialize would perform a floating-point ==.

    Covers float/double scalars and glm vector/matrix types (whose operator== compares
    floats). Used to gate a -Wfloat-equal suppression pragma in the generated source.
    """
    for f in s.fields:
        t = f.type
        if isinstance(t, ScalarType) and t.cpp_type in ("float", "double"):
            return True
        if isinstance(t, GlmType):
            return True
    return False


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
        elif t.name == "IVec3":
            lines.append(f"{indent}erhe::codegen::serialize_ivec3(out, {expr});")
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
    elif isinstance(t, MapType):
        # Map: serialize as JSON object. std::map iterates in sorted key order,
        # giving stable output suitable for diffs.
        lines.append(f"{indent}{{")
        lines.append(f'{indent}    out += "{{\\n";')
        lines.append(f"{indent}    bool map_first = true;")
        lines.append(f"{indent}    for (const auto& [map_k, map_v] : {expr}) {{")
        lines.append(f'{indent}        if (!map_first) out += ",\\n";')
        lines.append(f"{indent}        out.append((indent + 2) * 4, ' ');")
        lines.append(f"{indent}        erhe::codegen::serialize_string(out, map_k);")
        lines.append(f'{indent}        out += ": ";')
        lines.extend(_serialize_value_code(t.value_type, "map_v", indent + "        "))
        lines.append(f"{indent}        map_first = false;")
        lines.append(f"{indent}    }}")
        lines.append(f"{indent}    if (!{expr}.empty()) out += '\\n';")
        lines.append(f"{indent}    out.append((indent + 1) * 4, ' ');")
        lines.append(f"{indent}    out += '}}';")
        lines.append(f"{indent}}}")
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

    if f.default_history:
        # Field absent from the document: reconstruct the default that was current at the
        # document's _version, instead of keeping the latest member-initializer default.
        lines.append(f"{inner}}} else {{")
        ladder_inner = inner + "    "
        lines.append(f"{ladder_inner}// {f.name} absent: reconstruct version-appropriate default")
        for idx, (changed_in, prev_default) in enumerate(f.default_history):
            prefix = "if" if idx == 0 else "else if"
            lines.append(f"{ladder_inner}{prefix} (version < {changed_in}) {{ out.{f.name} = {prev_default}; }}")
        lines.append(f"{inner}}}")
    else:
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
    elif isinstance(t, MapType):
        value_cpp = t.value_type.cpp_type
        lines.append(f"{indent}{{")
        lines.append(f"{indent}    simdjson::ondemand::object map_obj;")
        lines.append(f"{indent}    if (!val.get_object().get(map_obj)) {{")
        lines.append(f"{indent}        {target}.clear();")
        lines.append(f"{indent}        for (auto map_field : map_obj) {{")
        lines.append(f"{indent}            std::string_view map_key_view;")
        lines.append(f"{indent}            if (map_field.unescaped_key().get(map_key_view) != simdjson::SUCCESS) {{")
        lines.append(f"{indent}                continue;")
        lines.append(f"{indent}            }}")
        lines.append(f"{indent}            simdjson::ondemand::value map_val;")
        lines.append(f"{indent}            if (map_field.value().get(map_val) != simdjson::SUCCESS) {{")
        lines.append(f"{indent}                continue;")
        lines.append(f"{indent}            }}")
        lines.append(f"{indent}            {value_cpp} map_value_tmp{{}};")
        lines.append(f"{indent}            erhe::codegen::deserialize_field(map_val, map_value_tmp);")
        lines.append(f"{indent}            {target}.emplace(std::string{{map_key_view}}, std::move(map_value_tmp));")
        lines.append(f"{indent}        }}")
        lines.append(f"{indent}    }}")
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
    struct_registry = get_struct_registry()
    for ref in struct_refs:
        prefix = struct_registry[ref].include_prefix if ref in struct_registry else ""
        lines.append(f'#include "{prefix}{_to_snake_case(ref)}_serialization.hpp"')

    lines.append("")
    lines.append("#include <cstddef>")
    lines.append("")
    lines.append("#include <erhe_codegen/serialize_helpers.hpp>")
    lines.append("#include <erhe_codegen/migration.hpp>")
    lines.append("")

    # Suppress deprecation warnings for removed fields accessed in deserialize/reflection,
    # and -Wfloat-equal for the floating-point == comparisons in is_default()/omit-defaults.
    has_deprecated = any(f.removed_in is not None for f in s.fields)
    needs_float_guard = _has_float_comparison(s)
    needs_guard = has_deprecated or needs_float_guard
    if needs_guard:
        lines.append("#if defined(_MSC_VER)")
        lines.append("#   pragma warning(push)")
        if has_deprecated:
            lines.append('#   pragma warning(disable : 4996)')
        lines.append("#elif defined(__GNUC__) || defined(__clang__)")
        lines.append('#   pragma GCC diagnostic push')
        if has_deprecated:
            lines.append('#   pragma GCC diagnostic ignored "-Wdeprecated-declarations"')
        if needs_float_guard:
            lines.append('#   pragma GCC diagnostic ignored "-Wfloat-equal"')
        lines.append("#endif")
        lines.append("")

    # --- Serialize ---
    lines.append(f"auto serialize(const {s.name}& value, int indent) -> std::string")
    lines.append("{")
    lines.append("    std::string out;")
    lines.append('    out += "{\\n";')
    if s.version > 1:
        lines.append("    out.append((indent + 1) * 4, ' ');")
        lines.append(f'    out += "\\"_version\\": {s.version},\\n";')

    active_fields = s.active_fields()
    for f in active_fields:
        if s.omit_defaults:
            # Emit the field only when it differs from its current default. The trailing-comma
            # cleanup below tolerates any number of skipped fields; the _version line (when
            # version > 1) keeps the object non-empty.
            lines.append(f"    if (!({_at_default_condition(f)})) {{")
            lines.extend(_serialize_field_code(f, indent="        "))
            lines.append("    }")
        else:
            lines.extend(_serialize_field_code(f))

    lines.append("    if (out.size() >= 2 && out[out.size()-2] == ',' && out.back() == '\\n') {")
    lines.append("        out.resize(out.size() - 2);")
    lines.append("        out += '\\n';")
    lines.append("    }")
    lines.append("    out.append(indent * 4, ' ');")
    lines.append("    out += '}';")
    lines.append("    erhe::codegen::collapse_single_line_object(out);")
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
    lines.append("            simdjson::error_code version_error = v.get_uint64().get(version);")
    lines.append("            static_cast<void>(version_error);")
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

    # --- is_default ---
    lines.append(f"auto is_default(const {s.name}& value) -> bool")
    lines.append("{")
    lines.append("    static_cast<void>(value);")
    active_for_default = s.active_fields()
    if not active_for_default:
        lines.append("    return true;")
    else:
        lines.append("    return")
        for idx, f in enumerate(active_for_default):
            sep = " &&" if idx < len(active_for_default) - 1 else ";"
            lines.append(f"        ({_at_default_condition(f)}){sep}")
    lines.append("}")
    lines.append("")

    # --- Reflection ---
    from erhe_codegen.emit_reflect import emit_struct_reflect
    lines.append(emit_struct_reflect(s))

    if needs_guard:
        lines.append("#if defined(_MSC_VER)")
        lines.append("#   pragma warning(pop)")
        lines.append("#elif defined(__GNUC__) || defined(__clang__)")
        lines.append("#   pragma GCC diagnostic pop")
        lines.append("#endif")
        lines.append("")

    return "\n".join(lines)
