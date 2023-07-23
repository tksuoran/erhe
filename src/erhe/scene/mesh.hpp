#pragma once

#include "erhe/scene/node_attachment.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/toolkit/unique_id.hpp"

#include <memory>
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

    // Implements Item
    static constexpr std::string_view static_type_name{"Mesh"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    [[nodiscard]] auto get_type     () const -> uint64_t         override;
    [[nodiscard]] auto get_type_name() const -> std::string_view override;

    // Implements Node_attachment
    void handle_item_host_update(erhe::Item_host* old_item_host, erhe::Item_host* new_item_host) override;

    Mesh_data mesh_data;
};

[[nodiscard]] auto operator<(const Mesh& lhs, const Mesh& rhs) -> bool;

[[nodiscard]] auto is_mesh(const erhe::Item* scene_item) -> bool;
[[nodiscard]] auto is_mesh(const std::shared_ptr<erhe::Item>& scene_item) -> bool;

[[nodiscard]] auto get_mesh(const erhe::scene::Node* node) -> std::shared_ptr<Mesh>;

} // namespace erhe::scene
