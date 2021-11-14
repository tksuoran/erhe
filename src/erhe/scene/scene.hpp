#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <string_view>
#include <vector>

namespace erhe::scene
{

class Node;
class Mesh;
class Camera;
class Light;

class Mesh_layer
{
public:
    explicit Mesh_layer(const std::string_view name);

    std::vector<std::shared_ptr<Mesh>>  meshes;
    std::string                         name;
};

class Light_layer
{
public:
    explicit Light_layer(const std::string_view name);

    std::vector<std::shared_ptr<Light>> lights;
    glm::vec4                           ambient_light{0.0f, 0.0f, 0.0f, 0.0f};
    std::string                         name;
};

class Scene
{
public:
    void sanity_check          () const;
    void sort_transform_nodes  ();
    void update_node_transforms();

    std::vector<std::shared_ptr<Node>>        nodes;
    std::vector<std::shared_ptr<Mesh_layer>>  mesh_layers;
    std::vector<std::shared_ptr<Light_layer>> light_layers;
    std::vector<std::shared_ptr<Camera>>      cameras;

    bool nodes_sorted{false};

    auto transform_update_serial() -> uint64_t;

private:
    uint64_t m_transform_update_serial{0};
};

} // namespace erhe::scene
