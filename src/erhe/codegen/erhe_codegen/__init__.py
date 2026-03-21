"""erhe_codegen — Python definitions for C++ struct/enum code generation."""

from erhe_codegen.types import (
    Bool,
    Int, UInt,
    Int8, UInt8,
    Int16, UInt16,
    Int32, UInt32,
    Int64, UInt64,
    Float, Double,
    String,
    Vec2, Vec3, Vec4, Mat4,
    Vector, Array, Optional,
    StructRef, EnumRef,
)

from erhe_codegen.schema import (
    struct,
    field,
    enum,
    value,
)

__all__ = [
    # Types
    "Bool",
    "Int", "UInt",
    "Int8", "UInt8",
    "Int16", "UInt16",
    "Int32", "UInt32",
    "Int64", "UInt64",
    "Float", "Double",
    "String",
    "Vec2", "Vec3", "Vec4", "Mat4",
    "Vector", "Array", "Optional",
    "StructRef", "EnumRef",
    # Schema
    "struct", "field",
    "enum", "value",
]
