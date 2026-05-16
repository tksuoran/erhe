#include "operations/scene_builder_floor_resources_operation.hpp"

#include "scene/scene_builder.hpp"

namespace editor {

Scene_builder_floor_resources_operation::Scene_builder_floor_resources_operation(Parameters parameters)
    : m_parameters{std::move(parameters)}
{
    set_description("Scene_builder floor resources");
}

Scene_builder_floor_resources_operation::~Scene_builder_floor_resources_operation() noexcept = default;

void Scene_builder_floor_resources_operation::execute(App_context&)
{
    if (m_parameters.builder == nullptr) {
        return;
    }
    m_parameters.builder->register_floor_resources(
        m_parameters.brush,
        m_parameters.collision_shape,
        m_parameters.material
    );
}

void Scene_builder_floor_resources_operation::undo(App_context&)
{
    if (m_parameters.builder == nullptr) {
        return;
    }
    m_parameters.builder->unregister_floor_resources(
        m_parameters.brush,
        m_parameters.collision_shape,
        m_parameters.material
    );
}

} // namespace editor
