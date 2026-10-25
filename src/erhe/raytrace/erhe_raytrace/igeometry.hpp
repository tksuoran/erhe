#pragma once

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_raytrace/prebuilt_bvh.hpp"

#include <cstdint>
#include <memory>
#include <optional>
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

    // BVH export and import (doc/erhe/raytrace.md "Prebuilt BVH"). Backends
    // without it (embree, tinybvh, none) keep these defaults: nothing to
    // report, export and import not available.

    // Content hash of the committed triangles (the cache/bvh key); empty
    // before a successful commit() or when the backend does not compute it.
    [[nodiscard]] virtual auto get_triangle_hash () const -> std::optional<uint64_t> { return {}; }
    // Triangle count of the last successful commit(), 0 otherwise.
    [[nodiscard]] virtual auto get_triangle_count() const -> std::size_t { return 0; }
    // Fills out with the committed BVH. Returns false when there is no
    // committed BVH or the backend cannot export one.
    [[nodiscard]] virtual auto export_bvh(Prebuilt_bvh& out) const -> bool { static_cast<void>(out); return false; }
    // Supplies a BVH for the next commit(). commit() validates it against the
    // content and, when it is accepted, uses it instead of the cache or a
    // build. A rejected BVH logs one warning and commit() continues as if
    // none was supplied. Returns false when the backend cannot import a BVH.
    [[nodiscard]] virtual auto set_prebuilt_bvh(Prebuilt_bvh&& prebuilt) -> bool { static_cast<void>(prebuilt); return false; }
    // Where the BVH of the last commit() came from.
    [[nodiscard]] virtual auto get_bvh_source() const -> Bvh_source { return Bvh_source::none; }
    // Why the BVH supplied for the last commit() was rejected; empty when
    // none was supplied or it was used.
    [[nodiscard]] virtual auto get_prebuilt_bvh_rejection() const -> std::string_view { return {}; }

    [[nodiscard]] static auto create       (std::string_view debug_label, Geometry_type geometry_type) -> IGeometry*;
    [[nodiscard]] static auto create_shared(std::string_view debug_label, Geometry_type geometry_type) -> std::shared_ptr<IGeometry>;
    [[nodiscard]] static auto create_unique(std::string_view debug_label, Geometry_type geometry_type) -> std::unique_ptr<IGeometry>;
};

} // namespace erhe::raytrace
