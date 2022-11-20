#pragma once

#include "erhe/scene/message_bus.hpp"
#include "erhe/toolkit/unique_id.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <string>
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
    Mesh_layer(
        const std::string_view name,
        const uint64_t         flags
    );

    [[nodiscard]] auto get_mesh_by_id(
        const erhe::toolkit::Unique_id<Node>::id_type id
    ) const -> std::shared_ptr<Mesh>;

    std::vector<std::shared_ptr<Mesh>> meshes;
    std::string                        name;
    uint64_t                           flags{0};
};

class Light_layer
{
public:
    explicit Light_layer(const std::string_view name);

    [[nodiscard]] auto get_light_by_id(
        const erhe::toolkit::Unique_id<Node>::id_type id
    ) const -> std::shared_ptr<Light>;

    std::vector<std::shared_ptr<Light>> lights;
    glm::vec4                           ambient_light{0.0f, 0.0f, 0.0f, 0.0f};
    std::string                         name;
};

class Scene
    : Message_bus_node
{
public:
    Scene(Message_bus* message_bus, void* host = nullptr);

    void sanity_check          () const;
    void sort_transform_nodes  ();
    void update_node_transforms();

    //[[nodiscard]] auto get_node_by_id         (const erhe::toolkit::Unique_id<Node>::id_type id) const -> std::shared_ptr<Node>;
    [[nodiscard]] auto get_mesh_by_id  (const erhe::toolkit::Unique_id<Node>::id_type id) const -> std::shared_ptr<Mesh>;
    [[nodiscard]] auto get_light_by_id (const erhe::toolkit::Unique_id<Node>::id_type id) const -> std::shared_ptr<Light>;
    [[nodiscard]] auto get_camera_by_id(const erhe::toolkit::Unique_id<Node>::id_type id) const -> std::shared_ptr<Camera>;

    void add_node(
        const std::shared_ptr<erhe::scene::Node>& node
    );

    void add_to_mesh_layer(
        erhe::scene::Mesh_layer&                  layer,
        const std::shared_ptr<erhe::scene::Mesh>& mesh_node
    );

    void add_to_light_layer(
        erhe::scene::Light_layer&                  layer,
        const std::shared_ptr<erhe::scene::Light>& light_node
    );

    void add(
        const std::shared_ptr<erhe::scene::Camera>& camera
    );

    void remove_from_mesh_layer(
        erhe::scene::Mesh_layer&                  layer,
        const std::shared_ptr<erhe::scene::Mesh>& mesh
    );

    void remove_node(
        const std::shared_ptr<erhe::scene::Node>& node
    );

    void remove(
        const std::shared_ptr<erhe::scene::Mesh>& mesh
    );

    void remove_from_light_layer(
        erhe::scene::Light_layer&                  layer,
        const std::shared_ptr<erhe::scene::Light>& light
    );

    void remove(
        const std::shared_ptr<erhe::scene::Light>& light
    );

    void remove(
        const std::shared_ptr<erhe::scene::Camera>& camera
    );

    void*                                     host{nullptr};
    std::shared_ptr<erhe::scene::Node>        root_node;
    std::vector<std::shared_ptr<Node>>        flat_node_vector;
    std::vector<std::shared_ptr<Mesh_layer>>  mesh_layers;
    std::vector<std::shared_ptr<Light_layer>> light_layers;
    std::vector<std::shared_ptr<Camera>>      cameras;

    bool nodes_sorted{false};
};

} // namespace erhe::scene
