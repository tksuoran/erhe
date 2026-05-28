#include "operations/async_raytrace_kickoff_operation.hpp"

#include "items.hpp"
#include "operations/mesh_operation.hpp"
#include "scene/scene_root.hpp"

#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace editor {

Async_raytrace_kickoff_operation::Async_raytrace_kickoff_operation(
    std::shared_ptr<Scene_root>                   scene_root,
    std::vector<std::shared_ptr<erhe::Item_base>> mesh_node_items
)
    : m_scene_root     {std::move(scene_root)}
    , m_mesh_node_items{std::move(mesh_node_items)}
{
    set_description(
        fmt::format(
            "[{}] Async_raytrace_kickoff ({} items)",
            get_serial(),
            m_mesh_node_items.size()
        )
    );
}

Async_raytrace_kickoff_operation::~Async_raytrace_kickoff_operation() noexcept = default;

void Async_raytrace_kickoff_operation::execute(App_context& context)
{
    std::shared_ptr<Scene_root> scene_root = m_scene_root;
    async_for_nodes_with_mesh(
        context,
        m_mesh_node_items,
        [scene_root](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root->item_host_mutex};

            for (const std::shared_ptr<erhe::Item_base>& item : mesh_operation_parameters.items) {
                std::shared_ptr<erhe::scene::Mesh> scene_mesh = erhe::scene::get_mesh(item);
                ERHE_VERIFY(scene_mesh);

                scene_root->begin_mesh_rt_update(scene_mesh);
                std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = scene_mesh->get_mutable_primitives();
                for (erhe::scene::Mesh_primitive& mesh_primitive : mesh_primitives) {
                    erhe::primitive::Primitive& primitive = *mesh_primitive.primitive.get();
                    ERHE_VERIFY(primitive.make_raytrace());
                }
                scene_mesh->update_rt_primitives();
                scene_root->end_mesh_rt_update(scene_mesh);
            }
        }
    );
}

void Async_raytrace_kickoff_operation::undo(App_context&)
{
    // In-flight async tasks captured scene_root and the item shared_ptrs at
    // task creation; they complete safely against the captured scene_root
    // even if items have been detached from it by sibling sub-op undos.
}

}
