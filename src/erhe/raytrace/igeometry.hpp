#pragma once

#include <memory>
#include <string_view>

namespace erhe::raytrace
{

class IScene;

enum class Buffer_type : int
{
    BUFFER_TYPE_INDEX                = 0,
    BUFFER_TYPE_VERTEX               = 1,
    BUFFER_TYPE_VERTEX_ATTRIBUTE     = 2,
    BUFFER_TYPE_NORMAL               = 3,
    BUFFER_TYPE_TANGENT              = 4,
    BUFFER_TYPE_NORMAL_DERIVATIVE    = 5,
    BUFFER_TYPE_GRID                 = 8,
    BUFFER_TYPE_FACE                 = 16,
    BUFFER_TYPE_LEVEL                = 17,
    BUFFER_TYPE_EDGE_CREASE_INDEX    = 18,
    BUFFER_TYPE_EDGE_CREASE_WEIGHT   = 19,
    BUFFER_TYPE_VERTEX_CREASE_INDEX  = 20,
    BUFFER_TYPE_VERTEX_CREASE_WEIGHT = 21,
    BUFFER_TYPE_HOLE                 = 22,
    BUFFER_TYPE_FLAGS                = 32
};

enum class Format : int
{
    FORMAT_UNDEFINED = 0,

    // 8-bit unsigned integer
    FORMAT_UCHAR = 0x1001,
    FORMAT_UCHAR2,
    FORMAT_UCHAR3,
    FORMAT_UCHAR4,

    // 8-bit signed integer
    FORMAT_CHAR = 0x2001,
    FORMAT_CHAR2,
    FORMAT_CHAR3,
    FORMAT_CHAR4,

    // 16-bit unsigned integer
    FORMAT_USHORT = 0x3001,
    FORMAT_USHORT2,
    FORMAT_USHORT3,
    FORMAT_USHORT4,

    // 16-bit signed integer
    FORMAT_SHORT = 0x4001,
    FORMAT_SHORT2,
    FORMAT_SHORT3,
    FORMAT_SHORT4,

    // 32-bit unsigned integer
    FORMAT_UINT = 0x5001,
    FORMAT_UINT2,
    FORMAT_UINT3,
    FORMAT_UINT4,

    // 32-bit signed integer
    FORMAT_INT = 0x6001,
    FORMAT_INT2,
    FORMAT_INT3,
    FORMAT_INT4,

    // 64-bit unsigned integer
    FORMAT_ULLONG = 0x7001,
    FORMAT_ULLONG2,
    FORMAT_ULLONG3,
    FORMAT_ULLONG4,

    // 64-bit signed integer
    FORMAT_LLONG = 0x8001,
    FORMAT_LLONG2,
    FORMAT_LLONG3,
    FORMAT_LLONG4,

    // 32-bit float
    FORMAT_FLOAT = 0x9001,
    FORMAT_FLOAT2,
    FORMAT_FLOAT3,
    FORMAT_FLOAT4,
    FORMAT_FLOAT5,
    FORMAT_FLOAT6,
    FORMAT_FLOAT7,
    FORMAT_FLOAT8,
    FORMAT_FLOAT9,
    FORMAT_FLOAT10,
    FORMAT_FLOAT11,
    FORMAT_FLOAT12,
    FORMAT_FLOAT13,
    FORMAT_FLOAT14,
    FORMAT_FLOAT15,
    FORMAT_FLOAT16,

    // 32-bit float matrix (row-major order)
    FORMAT_FLOAT2X2_ROW_MAJOR = 0x9122,
    FORMAT_FLOAT2X3_ROW_MAJOR = 0x9123,
    FORMAT_FLOAT2X4_ROW_MAJOR = 0x9124,
    FORMAT_FLOAT3X2_ROW_MAJOR = 0x9132,
    FORMAT_FLOAT3X3_ROW_MAJOR = 0x9133,
    FORMAT_FLOAT3X4_ROW_MAJOR = 0x9134,
    FORMAT_FLOAT4X2_ROW_MAJOR = 0x9142,
    FORMAT_FLOAT4X3_ROW_MAJOR = 0x9143,
    FORMAT_FLOAT4X4_ROW_MAJOR = 0x9144,

    // 32-bit float matrix (column-major order)
    FORMAT_FLOAT2X2_COLUMN_MAJOR = 0x9222,
    FORMAT_FLOAT2X3_COLUMN_MAJOR = 0x9223,
    FORMAT_FLOAT2X4_COLUMN_MAJOR = 0x9224,
    FORMAT_FLOAT3X2_COLUMN_MAJOR = 0x9232,
    FORMAT_FLOAT3X3_COLUMN_MAJOR = 0x9233,
    FORMAT_FLOAT3X4_COLUMN_MAJOR = 0x9234,
    FORMAT_FLOAT4X2_COLUMN_MAJOR = 0x9242,
    FORMAT_FLOAT4X3_COLUMN_MAJOR = 0x9243,
    FORMAT_FLOAT4X4_COLUMN_MAJOR = 0x9244,

    // special 12-byte format for grids
    FORMAT_GRID = 0xA001
};

enum class Geometry_type : int
{
    GEOMETRY_TYPE_TRIANGLE    = 0, // triangle mesh
    GEOMETRY_TYPE_QUAD        = 1, // quad (triangle pair) mesh
    GEOMETRY_TYPE_SUBDIVISION = 8, // Catmull-Clark subdivision surface
};

class IBuffer;

class IGeometry
{
public:
    virtual ~IGeometry(){};

    virtual void commit                    () = 0;
    virtual void enable                    () = 0;
    virtual void disable                   () = 0;
    virtual void set_vertex_attribute_count(const unsigned int count) = 0;
    virtual void set_buffer(
        const Buffer_type  type,
        const unsigned int slot,
        const Format       format,
        const IBuffer*     buffer,
        const size_t       byte_offset,
        const size_t       byte_stride,
        const size_t       item_count
    ) = 0;
    virtual void set_user_data(void* ptr) = 0;
    [[nodiscard]] virtual auto get_user_data() -> void* = 0;
    [[nodiscard]] virtual auto debug_label() const -> std::string_view = 0;

    [[nodiscard]] static auto create       (const std::string_view debug_label, const Geometry_type geometry_type) -> IGeometry*;
    [[nodiscard]] static auto create_shared(const std::string_view debug_label, const Geometry_type geometry_type) -> std::shared_ptr<IGeometry>;
    [[nodiscard]] static auto create_unique(const std::string_view debug_label, const Geometry_type geometry_type) -> std::unique_ptr<IGeometry>;
};

} // namespace erhe::raytrace
