#pragma once

#include "erhe_item/item.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_item/unique_id.hpp"

#include <memory>
#include <vector>

namespace erhe::raytrace {
    class IGeometry;
    class IInstance;
    class IScene;
    class Hit;
    class Ray;
}

namespace erhe::scene {

using Layer_id = uint64_t;

class Raytrace_primitive;
class Skin;

class Mesh : public erhe::Item<Item_base, Node_attachment, Mesh, erhe::Item_kind::clone_using_custom_clone_constructor>
{
public:
    Mesh(); // default
    explicit Mesh(Mesh&&);
    Mesh& operator=(Mesh&&);
    ~Mesh() noexcept override;

    explicit Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    explicit Mesh(const std::string_view name);
    Mesh(const std::string_view name, const erhe::primitive::Primitive& primitive);
    Mesh(const Mesh&, erhe::for_clone);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Mesh"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type               () const -> uint64_t                             override;
    auto get_type_name          () const -> std::string_view                     override;
    void handle_flag_bits_update(uint64_t old_flag_bits, uint64_t new_flag_bits) override;

    // Implements Node_attachment
    auto clone_attachment            () const -> std::shared_ptr<Node_attachment>                     override;
    void handle_item_host_update     (erhe::Item_host* old_item_host, erhe::Item_host* new_item_host) override;
    void handle_node_transform_update()                                                               override;

    // Public API
    void clear_primitives    ();
    void update_rt_primitives();
    void add_primitive       (erhe::primitive::Primitive primitive, const std::shared_ptr<erhe::primitive::Material>& material = {});
    void set_primitives      (const std::vector<erhe::primitive::Primitive>& primitives);
    void set_rt_mask         (uint32_t rt_mask);
    void attach_rt_to_scene  (erhe::raytrace::IScene* rt_scene);
    void detach_rt_from_scene();
    [[nodiscard]] auto get_mutable_primitives()       ->       std::vector<erhe::primitive::Primitive>&;
    [[nodiscard]] auto get_primitives        () const -> const std::vector<erhe::primitive::Primitive>&;
    [[nodiscard]] auto get_rt_scene          () const -> erhe::raytrace::IScene*;
    [[nodiscard]] auto get_rt_primitives     () const -> const std::vector<std::unique_ptr<Raytrace_primitive>>&;

    Layer_id              layer_id{0xff};
    std::shared_ptr<Skin> skin; // TODO Make this a separate node attachment
    float                 point_size{3.0f};
    float                 line_width{1.0f};

private:
    std::vector<erhe::primitive::Primitive>          m_primitives;
    erhe::raytrace::IScene*                          m_rt_scene{nullptr};
    std::vector<std::unique_ptr<Raytrace_primitive>> m_rt_primitives;
    bool                                             m_rt_primitives_dirty{false};
};

[[nodiscard]] auto operator<(const Mesh& lhs, const Mesh& rhs) -> bool;

[[nodiscard]] auto is_mesh(const erhe::Item_base* item) -> bool;
[[nodiscard]] auto is_mesh(const std::shared_ptr<erhe::Item_base>& item) -> bool;

[[nodiscard]] auto get_mesh(const erhe::scene::Node* node) -> std::shared_ptr<Mesh>;

} // namespace erhe::scene
