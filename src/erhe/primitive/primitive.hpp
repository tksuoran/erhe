#pragma once

#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/primitive/enums.hpp"

#include <memory>
#include <optional>

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::raytrace
{
    class IBuffer;
}

namespace erhe::primitive
{

class Material;


class Primitive
{
public:
    std::shared_ptr<Material>                 material             {};
    Primitive_geometry                        gl_primitive_geometry{};
    Primitive_geometry                        rt_primitive_geometry{};
    std::shared_ptr<erhe::raytrace::IBuffer>  rt_vertex_buffer     {};
    std::shared_ptr<erhe::raytrace::IBuffer>  rt_index_buffer      {};
    std::shared_ptr<erhe::geometry::Geometry> source_geometry      {};
    Normal_style                              normal_style         {Normal_style::none};
};

[[nodiscard]] auto primitive_type(const Primitive_mode primitive_mode) -> std::optional<gl::Primitive_type>;

} // namespace erhe::primitive
