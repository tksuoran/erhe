#include "operations/mesh_material_assign_operation.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "assets/asset_manager.hpp"
#include "operations/operation_stack.hpp"

#include "erhe_primitive/material.hpp"
#include "erhe_scene/mesh.hpp"

#include <fmt/format.h>

#include <algorithm>

namespace editor {

namespace {

[[nodiscard]] auto material_name(const std::shared_ptr<erhe::primitive::Material>& material) -> std::string
{
    return material ? material->get_name() : std::string{"(none)"};
}

void queue_operation(App_context& context, const std::shared_ptr<Mesh_material_assign_operation>& operation)
{
    if (!operation || (context.operation_stack == nullptr)) {
        return;
    }
    context.operation_stack->queue(operation);
}

} // anonymous namespace

Mesh_material_assign_operation::Mesh_material_assign_operation(std::vector<Entry>&& entries)
    : m_entries{std::move(entries)}
{
    if (m_entries.empty()) {
        set_description("Assign material (nothing to assign)");
        return;
    }
    const Entry& first = m_entries.front();
    set_description(
        (m_entries.size() == 1)
            ? fmt::format(
                "Assign material {} to {} primitive {}",
                material_name(first.after), first.mesh->get_name(), first.primitive_index
            )
            : fmt::format(
                "Assign material {} to {} primitives of {}",
                material_name(first.after), m_entries.size(), first.mesh->get_name()
            )
    );
}

Mesh_material_assign_operation::~Mesh_material_assign_operation() noexcept = default;

void Mesh_material_assign_operation::adopt_userships(App_context& context)
{
    if (m_userships_adopted || (context.asset_manager == nullptr)) {
        return;
    }
    m_userships_adopted = true;
    std::vector<const erhe::primitive::Material*> adopted;
    const auto adopt = [this, &context, &adopted](const std::shared_ptr<erhe::primitive::Material>& material) {
        if (!material || (std::find(adopted.begin(), adopted.end(), material.get()) != adopted.end())) {
            return;
        }
        adopted.push_back(material.get());
        Asset_reference& usership = m_userships.emplace_back();
        usership.set_user_label("undo stack: material assign");
        usership.adopt(*context.asset_manager, material);
    };
    for (const Entry& entry : m_entries) {
        adopt(entry.before);
        adopt(entry.after);
    }
}

void Mesh_material_assign_operation::execute(App_context& context)
{
    adopt_userships(context);
    for (const Entry& entry : m_entries) {
        entry.mesh->set_primitive_material(entry.primitive_index, entry.after);
    }
    announce(context);
}

void Mesh_material_assign_operation::undo(App_context& context)
{
    for (const Entry& entry : m_entries) {
        entry.mesh->set_primitive_material(entry.primitive_index, entry.before);
    }
    announce(context);
}

void Mesh_material_assign_operation::announce(App_context& context)
{
    if (context.app_message_bus == nullptr) {
        return;
    }
    for (std::size_t i = 0, end = m_entries.size(); i < end; ++i) {
        const bool first = std::none_of(m_entries.begin(), m_entries.begin() + i, [this, i](const Entry& entry) { return entry.mesh == m_entries[i].mesh; });
        if (first) {
            context.app_message_bus->mesh_materials_changed.send_message(Mesh_materials_changed_message{.mesh = m_entries[i].mesh});
        }
    }
}

auto make_mesh_material_assign_operation(
    const std::shared_ptr<erhe::scene::Mesh>&         mesh,
    const std::size_t                                 primitive_index,
    const std::shared_ptr<erhe::primitive::Material>& material
) -> std::shared_ptr<Mesh_material_assign_operation>
{
    if (!mesh) {
        return {};
    }
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    if (primitive_index >= primitives.size()) {
        return {};
    }
    const std::shared_ptr<erhe::primitive::Material>& before = primitives[primitive_index].material;
    if (before == material) {
        return {};
    }
    std::vector<Mesh_material_assign_operation::Entry> entries;
    entries.push_back(Mesh_material_assign_operation::Entry{mesh, primitive_index, before, material});
    return std::make_shared<Mesh_material_assign_operation>(std::move(entries));
}

auto make_mesh_material_assign_operation_for_all_primitives(
    const std::shared_ptr<erhe::scene::Mesh>&         mesh,
    const std::shared_ptr<erhe::primitive::Material>& material
) -> std::shared_ptr<Mesh_material_assign_operation>
{
    if (!mesh) {
        return {};
    }
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    std::vector<Mesh_material_assign_operation::Entry> entries;
    for (std::size_t i = 0, end = primitives.size(); i < end; ++i) {
        if (primitives[i].material == material) {
            continue;
        }
        entries.push_back(Mesh_material_assign_operation::Entry{mesh, i, primitives[i].material, material});
    }
    if (entries.empty()) {
        return {};
    }
    return std::make_shared<Mesh_material_assign_operation>(std::move(entries));
}

void queue_mesh_material_assign(
    App_context&                                      context,
    const std::shared_ptr<erhe::scene::Mesh>&         mesh,
    const std::size_t                                 primitive_index,
    const std::shared_ptr<erhe::primitive::Material>& material
)
{
    queue_operation(context, make_mesh_material_assign_operation(mesh, primitive_index, material));
}

void queue_mesh_material_assign_to_all_primitives(
    App_context&                                      context,
    const std::shared_ptr<erhe::scene::Mesh>&         mesh,
    const std::shared_ptr<erhe::primitive::Material>& material
)
{
    queue_operation(context, make_mesh_material_assign_operation_for_all_primitives(mesh, material));
}

}
