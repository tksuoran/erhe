#pragma once

#include "erhe/scene/node.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/toolkit/unique_id.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace erhe::scene
{

class Mesh
    : public INode_attachment
{
public:
    Mesh         ();
    explicit Mesh(const std::string_view name);
    Mesh         (const std::string_view name, const erhe::primitive::Primitive primitive);
    ~Mesh        () override;

    // Implements INode_attachment
    auto name     () const -> const std::string&;
    auto node     () const -> const std::shared_ptr<Node>&;

    void on_attach(Node& node);
    void on_detach();

    std::vector<erhe::primitive::Primitive> primitives;
    glm::vec4                               wireframe_color{0.0f, 0.0f, 0.0f, 1.0f};
    float                                   point_size     {3.0f};
    float                                   line_width     {1.0f};

    std::string                    m_name;
    erhe::toolkit::Unique_id<Mesh> m_id;
    std::shared_ptr<Node>          m_node;
};

auto operator<(const Mesh& lhs, const Mesh& rhs) -> bool;

}
