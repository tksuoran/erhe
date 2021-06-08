#pragma once

#include "erhe/scene/node.hpp"
#include "erhe/primitive/primitive.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace erhe::scene
{

class Mesh
    : public INode_attachment
{
public:
    Mesh         ();
    explicit Mesh(std::string_view name);
    Mesh         (std::string_view name, erhe::primitive::Primitive primitive);
    ~Mesh        () override;

    // Implements INode_attachment
    auto name     () const -> const std::string&;
    void on_attach(Node& node);
    void on_detach(Node& node);

    auto node() const -> std::shared_ptr<Node>
    {
        return m_node;
    }

    static constexpr const uint64_t c_visibility_content     = (1ul << 0ul);
    static constexpr const uint64_t c_visibility_shadow_cast = (1ul << 1ul);
    static constexpr const uint64_t c_visibility_id          = (1ul << 2ul);
    static constexpr const uint64_t c_visibility_tool        = (1ul << 3ul);
    static constexpr const uint64_t c_visibility_brush       = (1ul << 4ul);
    static constexpr const uint64_t c_visibility_selection   = (1ul << 5ul);
    static constexpr const uint64_t c_visibility_all         = ~uint64_t(0);

    std::string                             m_name;
    std::shared_ptr<Node>                   m_node;
    std::vector<erhe::primitive::Primitive> primitives;    
    glm::vec4                               wireframe_color{0.0f, 0.0f, 0.0f, 1.0f};
    float                                   point_size     {3.0f};
    float                                   line_width     {1.0f};
    uint64_t                                visibility_mask{c_visibility_all};
};

}
