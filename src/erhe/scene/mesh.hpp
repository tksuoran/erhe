#pragma once

#include "erhe/scene/node.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/toolkit/unique_id.hpp"

#include <vector>

namespace erhe::geometry {
    class Geometry;
}

namespace erhe::scene
{

using Layer_id = uint64_t;

class Skin;

class Mesh_data
{
public:
    Layer_id                                layer_id{0xff};
    std::vector<erhe::primitive::Primitive> primitives;
    std::shared_ptr<Skin>                   skin;
    float                                   point_size{3.0f};
    float                                   line_width{1.0f};
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
    void handle_item_host_update(Item_host* old_item_host, Item_host* new_item_host) override;

    // Implements Item
    [[nodiscard]] static auto get_static_type     () -> uint64_t;
    [[nodiscard]] static auto get_static_type_name() -> const char*;
    [[nodiscard]] auto get_type     () const -> uint64_t override;
    [[nodiscard]] auto get_type_name() const -> const char* override;

    Mesh_data mesh_data;

    erhe::toolkit::Unique_id<Mesh> m_id;
};

[[nodiscard]] auto operator<(const Mesh& lhs, const Mesh& rhs) -> bool;

[[nodiscard]] auto is_mesh(const Item* scene_item) -> bool;
[[nodiscard]] auto is_mesh(const std::shared_ptr<Item>& scene_item) -> bool;
[[nodiscard]] auto as_mesh(Item* scene_item) -> Mesh*;
[[nodiscard]] auto as_mesh(const std::shared_ptr<Item>& scene_item) -> std::shared_ptr<Mesh>;

[[nodiscard]] auto get_mesh(const erhe::scene::Node* node) -> std::shared_ptr<Mesh>;

} // namespace erhe::scene
