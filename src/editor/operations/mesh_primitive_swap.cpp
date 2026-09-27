#include "operations/mesh_primitive_swap.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

namespace {

[[nodiscard]] auto references_geometry(const erhe::scene::Mesh_primitive& mesh_primitive, const erhe::geometry::Geometry* geometry) -> bool
{
    const std::shared_ptr<erhe::primitive::Primitive>& primitive = mesh_primitive.primitive;
    return primitive && primitive->render_shape && (primitive->render_shape->get_geometry_const().get() == geometry);
}

} // anonymous namespace

void swap_mesh_primitives(
    const std::shared_ptr<erhe::scene::Mesh>&       mesh,
    const std::vector<erhe::scene::Mesh_primitive>& primitives
)
{
    // Every item host a mesh can be in is a Scene_root.
    Scene_root* const scene_root = static_cast<Scene_root*>(mesh->get_item_host());
    ERHE_VERIFY(scene_root != nullptr);
    scene_root->begin_mesh_rt_update(mesh);
    mesh->set_primitives(primitives);
    scene_root->end_mesh_rt_update(mesh);
}

void share_rebuilt_primitive(
    App_context&                                       context,
    const std::shared_ptr<erhe::scene::Mesh>&          mesh,
    const erhe::geometry::Geometry*                    geometry,
    const std::shared_ptr<erhe::primitive::Primitive>& new_primitive,
    const std::function<void(erhe::scene::Mesh&)>&     after_swap
)
{
    Scene_root* const scene_root = static_cast<Scene_root*>(mesh->get_item_host());
    ERHE_VERIFY(scene_root != nullptr);

    // Collect first: set_primitives() notifies the scene host, which must not
    // happen while the mesh-layer vectors are being iterated.
    std::vector<std::shared_ptr<erhe::scene::Mesh>> referers;
    for (const std::shared_ptr<erhe::scene::Mesh_layer>& layer : scene_root->get_scene().get_mesh_layers()) {
        for (const std::shared_ptr<erhe::scene::Mesh>& layer_mesh : layer->meshes) {
            if (!layer_mesh) {
                continue;
            }
            for (const erhe::scene::Mesh_primitive& mesh_primitive : layer_mesh->get_primitives()) {
                if (references_geometry(mesh_primitive, geometry)) {
                    referers.push_back(layer_mesh);
                    break;
                }
            }
        }
    }

    for (const std::shared_ptr<erhe::scene::Mesh>& referer : referers) {
        std::vector<erhe::scene::Mesh_primitive> new_primitives = referer->get_primitives();
        for (erhe::scene::Mesh_primitive& mesh_primitive : new_primitives) {
            if (references_geometry(mesh_primitive, geometry)) {
                mesh_primitive.primitive = new_primitive;
            }
        }
        swap_mesh_primitives(referer, new_primitives);
        if (after_swap) {
            after_swap(*referer);
        }
        context.app_message_bus->mesh_geometry_changed.send_message(
            Mesh_geometry_changed_message{.mesh = referer}
        );
    }
}

} // namespace editor
