#include "erhe_scene/mesh.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_raytrace/igeometry.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_scene/mesh_raytrace.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/scene_log.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene {

void Mesh::clear_primitives()
{
    if (m_primitives.empty()) {
        return;
    }
    m_primitives.clear();
    m_rt_primitives.clear();
}

void Mesh::update_rt_primitives()
{
    m_rt_primitives.clear();
    for (std::size_t i = 0, end = m_primitives.size(); i < end; ++i) {
        const Mesh_primitive&                                    mesh_primitive = m_primitives[i];
        const erhe::primitive::Primitive&                        primitive      = *mesh_primitive.primitive.get();
        const std::shared_ptr<erhe::primitive::Primitive_shape>& shape          = primitive.get_shape_for_raytrace();
        if (!shape) {
            continue;
        }
        const erhe::primitive::Primitive_raytrace&        primitive_raytrace = shape->get_raytrace();
        const std::shared_ptr<erhe::raytrace::IGeometry>& rt_geometry        = primitive_raytrace.get_raytrace_geometry();
        if (rt_geometry) {
            m_rt_primitives.emplace_back(
                new Raytrace_primitive(this, i, rt_geometry.get())
            );
        }
    }
}

void Mesh::add_primitive(
    const std::shared_ptr<erhe::primitive::Primitive>& primitive,
    const std::shared_ptr<erhe::primitive::Material>&  material
)
{
    m_primitives.emplace_back(primitive, material);
    update_rt_primitives();
}

void Mesh::set_primitives(const std::vector<Mesh_primitive>& primitives)
{
    m_primitives = primitives;
    update_rt_primitives();
}

auto Mesh::get_mutable_primitives() -> std::vector<Mesh_primitive>&
{
    return m_primitives;
}

auto Mesh::get_primitives() const -> const std::vector<Mesh_primitive>&
{
    return m_primitives;
}

Mesh::Mesh()                  = default;
Mesh::Mesh(Mesh&&)            = default;
Mesh& Mesh::operator=(Mesh&&) = default;

Mesh::Mesh(const std::string_view name)
    : Item{name}
{
}

Mesh::Mesh(
    const std::string_view                             name,
    const std::shared_ptr<erhe::primitive::Primitive>& primitive
)
    : Item{name}
{
    add_primitive(primitive, {});
}

Mesh::Mesh(const Mesh& src, erhe::for_clone)
    : Item      {src, for_clone{}}
    , layer_id  {src.layer_id}
    , skin      {src.skin}
    , point_size{src.point_size}
    , line_width{src.line_width}
{
    set_primitives(src.get_primitives());
}

auto Mesh::clone_attachment() const -> std::shared_ptr<Node_attachment>
{
    return std::make_shared<Mesh>(*this, for_clone{});
}

Mesh::~Mesh() noexcept
{
    if (m_rt_scene != nullptr) {
        detach_rt_from_scene();
    }
}

auto Mesh::get_static_type() -> uint64_t
{
    return Item_type::node_attachment | erhe::Item_type::mesh;
}

auto Mesh::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Mesh::get_type_name() const -> std::string_view
{
    return static_type_name;
}


auto Mesh::get_rt_scene() const -> erhe::raytrace::IScene*
{
    return m_rt_scene;
}

auto Mesh::get_rt_primitives() const -> const std::vector<std::unique_ptr<Raytrace_primitive>>&
{
    return m_rt_primitives;
}

void Mesh::set_rt_mask(const uint32_t mask)
{
    for (const auto& rt_primitive : m_rt_primitives) {
        rt_primitive->rt_instance->set_mask(mask);
    }
}

void Mesh::attach_rt_to_scene(erhe::raytrace::IScene* rt_scene)
{
    ERHE_VERIFY(rt_scene != nullptr);
    ERHE_VERIFY(m_rt_scene == nullptr);
    for (const auto& rt_primitive : m_rt_primitives) {
        rt_scene->attach(rt_primitive->rt_instance.get());
    }
    m_rt_scene = rt_scene;
}

void Mesh::detach_rt_from_scene() // erhe::raytrace::IScene* rt_scene)
{
    //ERHE_VERIFY((rt_scene == m_rt_scene) || (m_rt_scene == nullptr));
    if (m_rt_scene == nullptr) { // not attached
        return;
    }
    //ERHE_VERIFY(m_rt_scene != nullptr);
    for (const auto& rt_primitive : m_rt_primitives) {
        m_rt_scene->detach(rt_primitive->rt_instance.get());
    }
    m_rt_scene = nullptr;
}

void Mesh::handle_item_host_update(erhe::Item_host* const old_item_host, erhe::Item_host* const new_item_host)
{
    log->info("Mesh '{}' host update", get_name());
    const auto shared_this = std::static_pointer_cast<Mesh>(shared_from_this()); // keep alive

    Scene_host* old_scene_host = static_cast<Scene_host*>(old_item_host);
    Scene_host* new_scene_host = static_cast<Scene_host*>(new_item_host);

    if (old_scene_host != nullptr) {
        old_scene_host->unregister_mesh(shared_this);
    }
    if (new_scene_host != nullptr) {
        new_scene_host->register_mesh(shared_this);
    }
}

void Mesh::handle_flag_bits_update(uint64_t old_flag_bits, uint64_t new_flag_bits)
{
    log->info("Mesh '{}' handle_flag_bits_update()", get_name());
    const bool visibility_changed = erhe::utility::test_bit_set(old_flag_bits ^ new_flag_bits, erhe::Item_flags::visible);
    if (!visibility_changed) {
        return;
    }

    for (const auto& rt_primitive : m_rt_primitives) {
        const bool visible = erhe::utility::test_bit_set(new_flag_bits, erhe::Item_flags::visible);
        if (visible && !rt_primitive->rt_instance->is_enabled()) {
            rt_primitive->rt_instance->enable();
        } else if (!visible && rt_primitive->rt_instance->is_enabled()) {
            rt_primitive->rt_instance->disable();
        }
    }
}

void Mesh::handle_node_transform_update()
{
    const glm::mat4& world_from_node = (get_node() != nullptr) ? get_node()->world_from_node() : glm::mat4{1.0f};
    const float determinant = glm::determinant(world_from_node);
    if (determinant < 0.0f) {
        enable_flag_bits(Item_flags::negative_determinant);
    } else {
        disable_flag_bits(Item_flags::negative_determinant);
    }
    for (const auto& rt_primitive : m_rt_primitives) {
        rt_primitive->rt_instance->set_transform(world_from_node);
        rt_primitive->rt_instance->commit();
    }
}

auto Mesh::get_aabb_world() const -> erhe::math::Aabb
{
    const erhe::scene::Node* node = get_node();
    const glm::mat4 world_from_local = (node != nullptr) ? node->world_from_node() : glm::mat4{1.0f};
    erhe::math::Aabb aabb;
    for (const Mesh_primitive& mesh_primitive : m_primitives) {
        const erhe::math::Aabb primitive_aabb_local = mesh_primitive.primitive->get_bounding_box();
        const erhe::math::Aabb primitive_aabb_world = primitive_aabb_local.transformed_by(world_from_local);
        aabb.include(primitive_aabb_world);
    }
    return aabb;
}


auto operator<(const Mesh& lhs, const Mesh& rhs) -> bool
{
    return lhs.get_id() < rhs.get_id();
}

auto is_mesh(const Item_base* const item) -> bool
{
    if (item == nullptr) {
        return false;
    }
    return erhe::utility::test_bit_set(item->get_type(), erhe::Item_type::mesh);
}

auto is_mesh(const std::shared_ptr<erhe::Item_base>& item) -> bool
{
    return is_mesh(item.get());
}

auto get_mesh(const erhe::scene::Node* const node) -> std::shared_ptr<Mesh>
{
    if (node == nullptr) {
        return {};
    }
    for (const auto& attachment : node->get_attachments()) {
        auto mesh = std::dynamic_pointer_cast<Mesh>(attachment);
        if (mesh) {
            return mesh;
        }
    }
    return {};
}

} // namespace erhe::scene

