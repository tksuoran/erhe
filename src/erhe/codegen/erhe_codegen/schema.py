"""Schema classes and global registry for erhe_codegen definitions."""

from __future__ import annotations
from typing import Optional

from erhe_codegen.types import (
    TypeBase, ScalarType, GlmType, VectorType, ArrayType,
    StructRefType, EnumRefType, INTEGER_TYPES,
)


class FieldSchema:
    """Schema for a single struct field."""

    def __init__(
        self,
        name: str,
        type: TypeBase,
        *,
        added_in: int,
        removed_in: Optional[int] = None,
        default: Optional[str] = None,
        short_desc: Optional[str] = None,
        long_desc: Optional[str] = None,
        ui_min: Optional[str] = None,
        ui_max: Optional[str] = None,
        hard_min: Optional[str] = None,
        hard_max: Optional[str] = None,
        comment: Optional[str] = None,
    ):
        self.name = name
        self.type = type
        self.added_in = added_in
        self.removed_in = removed_in
        self.default = default
        self.short_desc = short_desc
        self.long_desc = long_desc
        self.ui_min = ui_min
        self.ui_max = ui_max
        self.hard_min = hard_min
        self.hard_max = hard_max
        self.comment = comment

    def is_active_in(self, version: int) -> bool:
        """Return True if this field is active in the given struct version."""
        if version < self.added_in:
            return False
        if self.removed_in is not None and version >= self.removed_in:
            return False
        return True

    def is_removed(self) -> bool:
        return self.removed_in is not None

    def __repr__(self) -> str:
        return f"FieldSchema({self.name!r}, {self.type!r}, added_in={self.added_in})"


class EnumValueSchema:
    """Schema for a single enum value."""

    def __init__(
        self,
        name: str,
        numeric_value: int,
        *,
        short_desc: Optional[str] = None,
        long_desc: Optional[str] = None,
    ):
        self.name = name
        self.numeric_value = numeric_value
        self.short_desc = short_desc
        self.long_desc = long_desc

    def __repr__(self) -> str:
        return f"EnumValueSchema({self.name!r}, {self.numeric_value})"


class EnumSchema:
    """Schema for an enum definition."""

    def __init__(
        self,
        name: str,
        values: list[EnumValueSchema],
        *,
        underlying_type: TypeBase = None,
    ):
        from erhe_codegen.types import Int
        self.name = name
        self.values = values
        self.underlying_type = underlying_type if underlying_type is not None else Int

    def __repr__(self) -> str:
        return f"EnumSchema({self.name!r}, {len(self.values)} values)"


class StructSchema:
    """Schema for a struct definition."""

    def __init__(
        self,
        name: str,
        fields: list[FieldSchema],
        *,
        version: int,
    ):
        self.name = name
        self.fields = fields
        self.version = version

    def active_fields(self, version: Optional[int] = None) -> list[FieldSchema]:
        """Return fields active in the given version (default: current)."""
        v = version if version is not None else self.version
        return [f for f in self.fields if f.is_active_in(v)]

    def __repr__(self) -> str:
        return f"StructSchema({self.name!r}, v{self.version}, {len(self.fields)} fields)"


# ---------------------------------------------------------------------------
# Global registry
# ---------------------------------------------------------------------------

_struct_registry: dict[str, StructSchema] = {}
_enum_registry: dict[str, EnumSchema] = {}


def get_struct_registry() -> dict[str, StructSchema]:
    return _struct_registry


def get_enum_registry() -> dict[str, EnumSchema]:
    return _enum_registry


def clear_registry() -> None:
    """Clear all registered structs and enums. Useful for testing."""
    _struct_registry.clear()
    _enum_registry.clear()


# ---------------------------------------------------------------------------
# Definition API functions
# ---------------------------------------------------------------------------

def field(
    name: str,
    type: TypeBase,
    *,
    added_in: int,
    removed_in: Optional[int] = None,
    default: Optional[str] = None,
    short_desc: Optional[str] = None,
    long_desc: Optional[str] = None,
    ui_min: Optional[str] = None,
    ui_max: Optional[str] = None,
    hard_min: Optional[str] = None,
    hard_max: Optional[str] = None,
    comment: Optional[str] = None,
) -> FieldSchema:
    """Create a field definition (used inside struct() calls)."""
    return FieldSchema(
        name, type,
        added_in=added_in,
        removed_in=removed_in,
        default=default,
        short_desc=short_desc,
        long_desc=long_desc,
        ui_min=ui_min,
        ui_max=ui_max,
        hard_min=hard_min,
        hard_max=hard_max,
        comment=comment,
    )


def value(
    name: str,
    numeric_value: int,
    *,
    short_desc: Optional[str] = None,
    long_desc: Optional[str] = None,
) -> EnumValueSchema:
    """Create an enum value definition (used inside enum() calls)."""
    return EnumValueSchema(
        name, numeric_value,
        short_desc=short_desc,
        long_desc=long_desc,
    )


def struct(name: str, *fields: FieldSchema, version: int) -> StructSchema:
    """Register a struct definition."""
    schema = StructSchema(name, list(fields), version=version)
    _validate_struct(schema)
    _struct_registry[name] = schema
    return schema


def enum(name: str, *values: EnumValueSchema, underlying_type: TypeBase = None) -> EnumSchema:
    """Register an enum definition."""
    schema = EnumSchema(name, list(values), underlying_type=underlying_type)
    _validate_enum(schema)
    _enum_registry[name] = schema
    return schema


# ---------------------------------------------------------------------------
# Validation
# ---------------------------------------------------------------------------

def _validate_struct(s: StructSchema) -> None:
    """Validate a struct schema, raising ValueError on problems."""
    if s.version < 1:
        raise ValueError(f"Struct {s.name!r}: version must be >= 1, got {s.version}")

    seen_names: set[str] = set()
    for f in s.fields:
        # Duplicate field names
        if f.name in seen_names:
            raise ValueError(f"Struct {s.name!r}: duplicate field name {f.name!r}")
        seen_names.add(f.name)

        # added_in range
        if f.added_in < 1 or f.added_in > s.version:
            raise ValueError(
                f"Struct {s.name!r}, field {f.name!r}: "
                f"added_in={f.added_in} must be in [1, {s.version}]"
            )

        # removed_in range
        if f.removed_in is not None:
            if f.removed_in <= f.added_in:
                raise ValueError(
                    f"Struct {s.name!r}, field {f.name!r}: "
                    f"removed_in={f.removed_in} must be > added_in={f.added_in}"
                )
            if f.removed_in > s.version + 1:
                raise ValueError(
                    f"Struct {s.name!r}, field {f.name!r}: "
                    f"removed_in={f.removed_in} must be <= version+1={s.version + 1}"
                )

        # Numeric limit options only on numeric scalar types
        has_limits = any(x is not None for x in [f.ui_min, f.ui_max, f.hard_min, f.hard_max])
        if has_limits:
            is_numeric_scalar = isinstance(f.type, ScalarType) and f.type.is_numeric
            if not is_numeric_scalar:
                raise ValueError(
                    f"Struct {s.name!r}, field {f.name!r}: "
                    f"numeric limit options are only valid on numeric scalar types, got {f.type!r}"
                )


def _validate_enum(e: EnumSchema) -> None:
    """Validate an enum schema, raising ValueError on problems."""
    # Underlying type must be an integer type
    if e.underlying_type not in INTEGER_TYPES:
        raise ValueError(
            f"Enum {e.name!r}: underlying_type must be an integer type, got {e.underlying_type!r}"
        )

    seen_names: set[str] = set()
    seen_values: set[int] = set()
    for v in e.values:
        if v.name in seen_names:
            raise ValueError(f"Enum {e.name!r}: duplicate enumerator name {v.name!r}")
        seen_names.add(v.name)

        if v.numeric_value in seen_values:
            raise ValueError(
                f"Enum {e.name!r}: duplicate numeric value {v.numeric_value} "
                f"for enumerator {v.name!r}"
            )
        seen_values.add(v.numeric_value)


def validate_references() -> None:
    """Validate all StructRef and EnumRef targets exist in their registries.

    Call this after all definition files have been loaded.
    """
    for s in _struct_registry.values():
        for f in s.fields:
            _validate_field_type_refs(s.name, f.name, f.type)


def _validate_field_type_refs(struct_name: str, field_name: str, t: TypeBase) -> None:
    """Recursively validate type references."""
    if isinstance(t, StructRefType):
        if t.name not in _struct_registry:
            raise ValueError(
                f"Struct {struct_name!r}, field {field_name!r}: "
                f"StructRef({t.name!r}) target not found in registry"
            )
    elif isinstance(t, EnumRefType):
        if t.name not in _enum_registry:
            raise ValueError(
                f"Struct {struct_name!r}, field {field_name!r}: "
                f"EnumRef({t.name!r}) target not found in registry"
            )
    elif isinstance(t, (VectorType, ArrayType)):
        element = t.element_type if isinstance(t, VectorType) else t.element_type
        _validate_field_type_refs(struct_name, field_name, element)
