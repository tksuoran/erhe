#pragma once

#include "erhe/scene/node.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/toolkit/unique_id.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace erhe::scene
{

class Mesh
    : public Node
{
public:
    Mesh         ();
    explicit Mesh(const std::string_view name);
    Mesh         (const std::string_view name, const erhe::primitive::Primitive primitive);
    ~Mesh        () override;

    auto node_type() const -> const char* override;

    std::vector<erhe::primitive::Primitive> primitives;
    glm::vec4                               wireframe_color{0.0f, 0.0f, 0.0f, 1.0f};
    float                                   point_size     {3.0f};
    float                                   line_width     {1.0f};

    erhe::toolkit::Unique_id<Mesh> m_id;
};

auto operator<(const Mesh& lhs, const Mesh& rhs) -> bool;

auto is_mesh(const Node* const node) -> bool;
auto is_mesh(const std::shared_ptr<Node>& node) -> bool;
auto as_mesh(Node* const node) -> Mesh*;
auto as_mesh(const std::shared_ptr<Node>& node) -> std::shared_ptr<Mesh>;

}
