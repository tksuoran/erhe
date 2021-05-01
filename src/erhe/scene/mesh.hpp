#pragma once

#include "erhe/primitive/primitive.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace erhe::scene
{

class Node;

class Mesh
{
public:
    Mesh() = default;

    explicit Mesh(const std::string& name)
        : name{name}
    {}

    Mesh(const std::string&         name,
         Node*                      node,
         erhe::primitive::Primitive primitive);

    ~Mesh() = default;

    // glm::vec3 position() const;

    static constexpr const uint64_t c_visibility_content     = (1ul << 0ul);
    static constexpr const uint64_t c_visibility_shadow_cast = (1ul << 1ul);
    static constexpr const uint64_t c_visibility_id          = (1ul << 2ul);
    static constexpr const uint64_t c_visibility_tool        = (1ul << 3ul);
    static constexpr const uint64_t c_visibility_brush       = (1ul << 4ul);
    static constexpr const uint64_t c_visibility_selection   = (1ul << 5ul);
    static constexpr const uint64_t c_visibility_all         = ~uint64_t(0);

    std::string                             name;
    Node*                                   node           {nullptr};
    std::vector<erhe::primitive::Primitive> primitives;    
    glm::vec4                               wireframe_color{0.0f, 0.0f, 0.0f, 1.0f};
    float                                   point_size     {3.0f};
    float                                   line_width     {1.0f};
    uint64_t                                visibility_mask{c_visibility_all};
};

}
