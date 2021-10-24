#pragma once

#include "erhe/primitive/index_range.hpp"
#include "erhe/primitive/enums.hpp"

#include <memory>
#include <optional>

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::primitive
{

class Primitive_geometry;
class Material;


class Primitive
{
public:
    Primitive();
    Primitive(
        std::shared_ptr<Primitive_geometry> primitive_geometry,
        std::shared_ptr<Material>           material
    );

    std::shared_ptr<Primitive_geometry> primitive_geometry;
    std::shared_ptr<Material>           material;
};

std::optional<gl::Primitive_type> primitive_type(Primitive_mode primitive_mode);

} // namespace erhe::primitive
