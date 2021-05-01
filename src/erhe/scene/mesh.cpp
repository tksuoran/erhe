#include "erhe/scene/mesh.hpp"

namespace erhe::scene
{

Mesh::Mesh(const std::string&         name,
           Node*                      node,
           erhe::primitive::Primitive primitive)
    : name{name}
    , node{node}
{
    primitives.emplace_back(primitive);
}

}