#pragma once

#include "erhe_dataformat/dataformat.hpp"
#include <cstdint>
#include <memory>
#include <string_view>

namespace erhe::buffer {
    class Cpu_buffer;
}

namespace erhe::raytrace {

class IScene;
class Ray;
class Hit;

enum class Buffer_type : int {
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

enum class Geometry_type : int {
    GEOMETRY_TYPE_TRIANGLE    = 0, // triangle mesh
    GEOMETRY_TYPE_QUAD        = 1, // quad (triangle pair) mesh
    GEOMETRY_TYPE_SUBDIVISION = 8, // Catmull-Clark subdivision surface
};

class IInstance;

class IGeometry
{
public:
    virtual ~IGeometry() noexcept {};

    virtual void commit                    () = 0;
    virtual void enable                    () = 0;
    virtual void disable                   () = 0;
    virtual void set_mask                  (uint32_t mask) = 0;
    virtual void set_vertex_attribute_count(unsigned int count) = 0;
    virtual void set_buffer(
        Buffer_type               type,
        unsigned int              slot,
        erhe::dataformat::Format  format,
        erhe::buffer::Cpu_buffer* buffer,
        std::size_t               byte_offset,
        std::size_t               byte_stride,
        std::size_t               item_count
    ) = 0;
    virtual void set_user_data(const void* ptr) = 0;
    [[nodiscard]] virtual auto get_mask     () const -> uint32_t         = 0;
    [[nodiscard]] virtual auto get_user_data() const -> const void*      = 0;
    [[nodiscard]] virtual auto is_enabled   () const -> bool             = 0;
    [[nodiscard]] virtual auto debug_label  () const -> std::string_view = 0;

    [[nodiscard]] static auto create       (const std::string_view debug_label, const Geometry_type geometry_type) -> IGeometry*;
    [[nodiscard]] static auto create_shared(const std::string_view debug_label, const Geometry_type geometry_type) -> std::shared_ptr<IGeometry>;
    [[nodiscard]] static auto create_unique(const std::string_view debug_label, const Geometry_type geometry_type) -> std::unique_ptr<IGeometry>;
};

} // namespace erhe::raytrace
