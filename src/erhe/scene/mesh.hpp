#pragma once

#include "erhe/scene/node.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/toolkit/unique_id.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::scene
{

class Mesh_data
{
public:
    erhe::toolkit::Unique_id<Mesh_layer>::id_type layer_id;
    std::vector<erhe::primitive::Primitive>       primitives;
    float                                         point_size{3.0f};
    float                                         line_width{1.0f};
};

class Mesh
    : public Node_attachment
{
public:
    Mesh         ();
    explicit Mesh(const std::string_view name);
    Mesh         (const std::string_view name, const erhe::primitive::Primitive primitive);
    ~Mesh        () noexcept override;

    // Implements Node_attachment
    [[nodiscard]] auto type_name() const -> const char* override;
    void handle_node_scene_host_update(Scene_host* old_scene_host, Scene_host* new_scene_host) override;

    Mesh_data mesh_data;

    erhe::toolkit::Unique_id<Mesh> m_id;
};

[[nodiscard]] auto operator<(const Mesh& lhs, const Mesh& rhs) -> bool;

[[nodiscard]] auto is_mesh(const Scene_item* const scene_item) -> bool;
[[nodiscard]] auto is_mesh(const std::shared_ptr<Scene_item>& scene_item) -> bool;
[[nodiscard]] auto as_mesh(Scene_item* const scene_item) -> Mesh*;
[[nodiscard]] auto as_mesh(const std::shared_ptr<Scene_item>& scene_item) -> std::shared_ptr<Mesh>;

[[nodiscard]] auto get_mesh(const erhe::scene::Node* node) -> std::shared_ptr<Mesh>;

} // namespace erhe::scene
