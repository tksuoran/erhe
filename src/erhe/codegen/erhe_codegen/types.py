"""Type definitions for erhe_codegen."""


class TypeBase:
    """Base class for all types."""
    pass


class ScalarType(TypeBase):
    """A scalar (POD) type."""

    def __init__(self, name: str, cpp_type: str, simdjson_access: str, is_numeric: bool = False, is_integer: bool = False):
        self.name = name
        self.cpp_type = cpp_type
        self.simdjson_access = simdjson_access
        self.is_numeric = is_numeric
        self.is_integer = is_integer

    def __repr__(self) -> str:
        return f"ScalarType({self.name!r})"


class GlmType(TypeBase):
    """A glm vector/matrix type."""

    def __init__(self, name: str, cpp_type: str, component_count: int):
        self.name = name
        self.cpp_type = cpp_type
        self.component_count = component_count
        self.is_numeric = False
        self.is_integer = False

    def __repr__(self) -> str:
        return f"GlmType({self.name!r})"


class VectorType(TypeBase):
    """std::vector<T>"""

    def __init__(self, element_type: TypeBase):
        self.element_type = element_type
        self.is_numeric = False
        self.is_integer = False

    @property
    def cpp_type(self) -> str:
        return f"std::vector<{_cpp_type_of(self.element_type)}>"

    def __repr__(self) -> str:
        return f"Vector({self.element_type!r})"


class ArrayType(TypeBase):
    """std::array<T, N>"""

    def __init__(self, element_type: TypeBase, size: int):
        self.element_type = element_type
        self.size = size
        self.is_numeric = False
        self.is_integer = False

    @property
    def cpp_type(self) -> str:
        return f"std::array<{_cpp_type_of(self.element_type)}, {self.size}>"

    def __repr__(self) -> str:
        return f"Array({self.element_type!r}, {self.size})"


class OptionalType(TypeBase):
    """std::optional<T>"""

    def __init__(self, element_type: TypeBase):
        self.element_type = element_type
        self.is_numeric = False
        self.is_integer = False

    @property
    def cpp_type(self) -> str:
        return f"std::optional<{_cpp_type_of(self.element_type)}>"

    def __repr__(self) -> str:
        return f"Optional({self.element_type!r})"


class StructRefType(TypeBase):
    """Reference to another generated struct by name."""

    def __init__(self, name: str):
        self.name = name
        self.is_numeric = False
        self.is_integer = False

    @property
    def cpp_type(self) -> str:
        return self.name

    def __repr__(self) -> str:
        return f"StructRef({self.name!r})"


class EnumRefType(TypeBase):
    """Reference to a generated enum by name."""

    def __init__(self, name: str):
        self.name = name
        self.is_numeric = False
        self.is_integer = False

    @property
    def cpp_type(self) -> str:
        return self.name

    def __repr__(self) -> str:
        return f"EnumRef({self.name!r})"


def _cpp_type_of(t: TypeBase) -> str:
    """Get the C++ type string for any TypeBase."""
    if hasattr(t, "cpp_type"):
        return t.cpp_type
    raise TypeError(f"Cannot determine C++ type for {t!r}")


# Scalar type instances
Bool   = ScalarType("Bool",   "bool",         ".get_bool()")
Int    = ScalarType("Int",    "int",           ".get_int64()",  is_numeric=True, is_integer=True)
UInt   = ScalarType("UInt",   "unsigned int",  ".get_uint64()", is_numeric=True, is_integer=True)
Int8   = ScalarType("Int8",   "int8_t",        ".get_int64()",  is_numeric=True, is_integer=True)
UInt8  = ScalarType("UInt8",  "uint8_t",       ".get_uint64()", is_numeric=True, is_integer=True)
Int16  = ScalarType("Int16",  "int16_t",       ".get_int64()",  is_numeric=True, is_integer=True)
UInt16 = ScalarType("UInt16", "uint16_t",      ".get_uint64()", is_numeric=True, is_integer=True)
Int32  = ScalarType("Int32",  "int32_t",       ".get_int64()",  is_numeric=True, is_integer=True)
UInt32 = ScalarType("UInt32", "uint32_t",      ".get_uint64()", is_numeric=True, is_integer=True)
Int64  = ScalarType("Int64",  "int64_t",       ".get_int64()",  is_numeric=True, is_integer=True)
UInt64 = ScalarType("UInt64", "uint64_t",      ".get_uint64()", is_numeric=True, is_integer=True)
Float  = ScalarType("Float",  "float",         ".get_double()", is_numeric=True)
Double = ScalarType("Double", "double",        ".get_double()", is_numeric=True)
String = ScalarType("String", "std::string",   ".get_string()")

# glm type instances
Vec2 = GlmType("Vec2", "glm::vec2", 2)
Vec3 = GlmType("Vec3", "glm::vec3", 3)
Vec4 = GlmType("Vec4", "glm::vec4", 4)
Mat4 = GlmType("Mat4", "glm::mat4", 16)

# Composite type constructors
Vector    = VectorType
Array     = ArrayType
Optional  = OptionalType
StructRef = StructRefType
EnumRef   = EnumRefType

# All integer types (for enum underlying_type validation)
INTEGER_TYPES = {Int, UInt, Int8, UInt8, Int16, UInt16, Int32, UInt32, Int64, UInt64}
