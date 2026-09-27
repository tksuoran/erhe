#include "operations/set_geometry_attribute_operation.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "operations/async_raytrace_kickoff_operation.hpp"
#include "operations/move_mesh_vertices_operation.hpp"
#include "scene/scene_root.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_item/item_host.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <geogram/mesh/mesh.h>

#include <type_traits>

namespace editor {

namespace {

// Calls f(attribute_present, element_count) for the Mesh_attributes attribute
// named `name`; false when no attribute has that name.
template <typename Geometry_type, typename F>
auto visit_attribute(Geometry_type& geometry, const std::string_view name, F&& f) -> bool
{
    auto&            attributes = geometry.get_attributes(); // Mesh_attributes, const when the Geometry is
    const GEO::Mesh& mesh       = geometry.get_mesh();
    bool found = false;
    const auto visit_domain = [&](const std::size_t element_count) {
        return [&, element_count](const char* attribute_name, auto& attribute_present) {
            if (!found && (name == attribute_name)) {
                found = true;
                f(attribute_present, element_count);
            }
        };
    };
    attributes.for_each_vertex_attribute(visit_domain(mesh.vertices.nb()));
    attributes.for_each_corner_attribute(visit_domain(mesh.facet_corners.nb()));
    attributes.for_each_facet_attribute (visit_domain(mesh.facets.nb()));
    attributes.for_each_edge_attribute  (visit_domain(mesh.edges.nb()));
    return found;
}

template <typename T>
auto to_attribute_value(const Geometry_attribute_value& value) -> T
{
    if constexpr (std::is_arithmetic_v<T>) {
        return static_cast<T>(value.components[0]);
    } else {
        T result{};
        using Component = std::remove_cvref_t<decltype(result[0])>;
        for (GEO::index_t i = 0; i < T::dim; ++i) {
            result[i] = static_cast<Component>(value.components[i]);
        }
        return result;
    }
}

template <typename T>
auto from_attribute(const erhe::geometry::Attribute_present<T>& attribute_present, const GEO::index_t element) -> Geometry_attribute_value
{
    Geometry_attribute_value result{};
    if (!attribute_present.has(element)) {
        return result;
    }
    result.present = true;
    const T value = attribute_present.get(element);
    if constexpr (std::is_arithmetic_v<T>) {
        result.components[0] = static_cast<double>(value);
    } else {
        for (GEO::index_t i = 0; i < T::dim; ++i) {
            result.components[i] = static_cast<double>(value[i]);
        }
    }
    return result;
}

} // anonymous namespace

auto is_editable_geometry_attribute(const std::string_view attribute) -> bool
{
    return
        (attribute != "facet_id")             &&
        (attribute != "facet_centroid")       &&
        (attribute != "vertex_normal_smooth") &&
        (attribute != "vertex_valency_edge_count");
}

auto read_geometry_attribute(
    const erhe::geometry::Geometry& geometry,
    const std::string_view          attribute,
    const GEO::index_t              element
) -> std::optional<Geometry_attribute_value>
{
    if (attribute == c_position_attribute) {
        const GEO::Mesh& mesh = geometry.get_mesh();
        if (element >= mesh.vertices.nb()) {
            return {};
        }
        const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, element);
        return Geometry_attribute_value{.components = {p.x, p.y, p.z, 0.0}, .present = true};
    }
    std::optional<Geometry_attribute_value> result{};
    visit_attribute(geometry, attribute, [&](const auto& attribute_present, const std::size_t element_count) {
        if (element < element_count) {
            result = from_attribute(attribute_present, element);
        }
    });
    return result;
}

auto make_geometry_attribute_operation(
    App_context&                                 context,
    const std::shared_ptr<erhe::scene::Mesh>&    mesh,
    const std::size_t                            primitive_index,
    const std::string_view                       attribute,
    const std::vector<GEO::index_t>&             elements,
    const std::vector<Geometry_attribute_value>& after,
    std::string&                                 out_error
) -> std::shared_ptr<Operation>
{
    if (!mesh) {
        out_error = "no mesh";
        return {};
    }
    if (!is_editable_geometry_attribute(attribute)) {
        out_error = fmt::format("attribute '{}' is derived by the geometry pipeline and is not editable", attribute);
        return {};
    }
    if (elements.empty()) {
        out_error = "no elements";
        return {};
    }
    if ((after.size() != 1) && (after.size() != elements.size())) {
        out_error = fmt::format("{} values for {} elements (give one value, or one per element)", after.size(), elements.size());
        return {};
    }
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    if ((primitive_index >= primitives.size()) || !primitives[primitive_index].primitive || !primitives[primitive_index].primitive->render_shape) {
        out_error = fmt::format("mesh '{}' has no primitive {}", mesh->get_name(), primitive_index);
        return {};
    }
    const erhe::primitive::Primitive&               primitive = *primitives[primitive_index].primitive.get();
    const std::shared_ptr<erhe::geometry::Geometry>& geometry  = primitive.render_shape->get_geometry_const();
    if (!geometry) {
        out_error = fmt::format("mesh '{}' primitive {} has no geometry yet", mesh->get_name(), primitive_index);
        return {};
    }

    std::vector<Geometry_attribute_value> before_values;
    std::vector<Geometry_attribute_value> after_values;
    before_values.reserve(elements.size());
    after_values .reserve(elements.size());
    for (std::size_t i = 0, end = elements.size(); i < end; ++i) {
        const std::optional<Geometry_attribute_value> before = read_geometry_attribute(*geometry.get(), attribute, elements[i]);
        if (!before.has_value()) {
            out_error = fmt::format("unknown attribute '{}' or element {} out of range", attribute, elements[i]);
            return {};
        }
        before_values.push_back(before.value());
        after_values.push_back((after.size() == 1) ? after.front() : after[i]);
    }

    const erhe::primitive::Build_info build_info{
        .primitive_types = {
            .fill_triangles          = true,
            .fill_triangles_expanded = true,
            .edge_lines              = true,
            .corner_points           = true,
            .centroid_points         = true
        },
        // Skinned meshes must rebuild into the skinned vertex format or the
        // GPU streams silently lose their joints.
        .buffer_info = mesh->skin
            ? context.mesh_memory->make_skinned_primitive_buffer_info()
            : context.mesh_memory->make_primitive_buffer_info()
    };
    const erhe::primitive::Normal_style normal_style = primitive.render_shape->get_normal_style();

    if (attribute == c_position_attribute) {
        std::vector<glm::vec3> before_positions;
        std::vector<glm::vec3> after_positions;
        before_positions.reserve(elements.size());
        after_positions .reserve(elements.size());
        for (std::size_t i = 0, end = elements.size(); i < end; ++i) {
            if (!after_values[i].present) {
                out_error = "a vertex position cannot be cleared";
                return {};
            }
            const std::array<double, 4>& b = before_values[i].components;
            const std::array<double, 4>& a = after_values[i].components;
            before_positions.emplace_back(static_cast<float>(b[0]), static_cast<float>(b[1]), static_cast<float>(b[2]));
            after_positions .emplace_back(static_cast<float>(a[0]), static_cast<float>(a[1]), static_cast<float>(a[2]));
        }
        return std::make_shared<Move_mesh_vertices_operation>(
            Move_mesh_vertices_operation::Parameters{
                .mesh             = mesh,
                .primitive_index  = primitive_index,
                .geometry         = geometry,
                .vertices         = elements,
                .before_positions = std::move(before_positions),
                .after_positions  = std::move(after_positions),
                .build_info       = build_info,
                .normal_style     = normal_style
            }
        );
    }

    return std::make_shared<Set_geometry_attribute_operation>(
        Set_geometry_attribute_operation::Parameters{
            .mesh            = mesh,
            .primitive_index = primitive_index,
            .geometry        = geometry,
            .attribute       = std::string{attribute},
            .elements        = elements,
            .before_values   = std::move(before_values),
            .after_values    = std::move(after_values),
            .build_info      = build_info,
            .normal_style    = normal_style
        }
    );
}

Set_geometry_attribute_operation::Set_geometry_attribute_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
    set_description(fmt::format("Set {} on {} elements", m_parameters.attribute, m_parameters.elements.size()));
}

void Set_geometry_attribute_operation::execute(App_context& context)
{
    apply(context, m_parameters.after_values);
}

void Set_geometry_attribute_operation::undo(App_context& context)
{
    apply(context, m_parameters.before_values);
}

void Set_geometry_attribute_operation::apply(App_context& context, const std::vector<Geometry_attribute_value>& values)
{
    if (!m_parameters.mesh || !m_parameters.geometry) {
        set_error("Set_geometry_attribute_operation: mesh or geometry is null");
        return;
    }
    if (values.size() != m_parameters.elements.size()) {
        set_error("Set_geometry_attribute_operation: value count mismatch");
        return;
    }
    erhe::scene::Node* node      = m_parameters.mesh.get();
    erhe::Item_host*   item_host = (node != nullptr) ? node->get_item_host() : nullptr;
    if (item_host == nullptr) {
        set_error("Set_geometry_attribute_operation: mesh is not in a scene");
        return;
    }
    // unique_lock: the background-optimize kickoff at the end takes this same
    // mutex itself and must run after an explicit unlock
    // (Paint_colors_operation::apply).
    std::unique_lock<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};

    const bool found = visit_attribute(
        *m_parameters.geometry.get(),
        m_parameters.attribute,
        [&]<typename T>(erhe::geometry::Attribute_present<T>& attribute_present, const std::size_t element_count) {
            for (std::size_t i = 0, end = m_parameters.elements.size(); i < end; ++i) {
                const GEO::index_t element = m_parameters.elements[i];
                ERHE_VERIFY(element < element_count);
                if (values[i].present) {
                    attribute_present.set(element, to_attribute_value<T>(values[i]));
                } else {
                    attribute_present.unset(element);
                }
            }
        }
    );
    if (!found) {
        set_error(fmt::format("Set_geometry_attribute_operation: unknown attribute '{}'", m_parameters.attribute));
        return;
    }

    // The crease sharpness feeds no render stream: nothing to rebuild, only
    // announce the in-place change.
    if (m_parameters.attribute == erhe::geometry::c_edge_sharpness) {
        scene_lock.unlock();
        context.app_message_bus->mesh_geometry_changed.send_message(
            Mesh_geometry_changed_message{.mesh = m_parameters.mesh}
        );
        return;
    }

    // Rebuild one Primitive for the (unchanged) Geometry and share it across
    // every mesh that references the Geometry, as Paint_colors_operation does.
    erhe::primitive::Build_info build_info = m_parameters.build_info;
    const bool background_optimize =
        build_info.buffer_info.optimize_meshes &&
        (context.graphics_device != nullptr) &&
        context.graphics_device->supports_worker_contexts();
    if (background_optimize) {
        build_info.buffer_info.optimize_meshes = false;
    }
    std::shared_ptr<erhe::primitive::Primitive> new_primitive = std::make_shared<erhe::primitive::Primitive>(m_parameters.geometry);
    const bool renderable_ok = new_primitive->make_renderable_mesh(build_info, m_parameters.normal_style);
    const bool raytrace_ok   = new_primitive->make_raytrace();
    ERHE_VERIFY(renderable_ok && raytrace_ok);

    // Collect-then-rebuild: the re-parent dance below mutates the scene's
    // mesh-layer vectors.
    auto* const                                     scene_root = static_cast<Scene_root*>(item_host);
    erhe::scene::Scene&                             scene      = scene_root->get_scene();
    std::vector<std::shared_ptr<erhe::scene::Mesh>> referers;
    for (const std::shared_ptr<erhe::scene::Mesh_layer>& layer : scene.get_mesh_layers()) {
        for (const std::shared_ptr<erhe::scene::Mesh>& mesh : layer->meshes) {
            if (!mesh) {
                continue;
            }
            for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
                const std::shared_ptr<erhe::primitive::Primitive>& primitive = mesh_primitive.primitive;
                if (primitive && primitive->render_shape && (primitive->render_shape->get_geometry_const() == m_parameters.geometry)) {
                    referers.push_back(mesh);
                    break;
                }
            }
        }
    }

    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : referers) {
        erhe::scene::Node* mesh_node = mesh.get();
        std::vector<erhe::scene::Mesh_primitive> new_primitives = mesh->get_primitives();
        for (erhe::scene::Mesh_primitive& mesh_primitive : new_primitives) {
            if (mesh_primitive.primitive && mesh_primitive.primitive->render_shape &&
                (mesh_primitive.primitive->render_shape->get_geometry_const() == m_parameters.geometry)) {
                mesh_primitive.primitive = new_primitive;
            }
        }
        // Re-attach raytrace via the node re-parent dance. No physics rebuild:
        // positions are unchanged (position edits are Move_mesh_vertices_operation).
        std::shared_ptr<erhe::Hierarchy> parent = mesh_node->get_parent().lock();
        mesh_node->set_parent(std::shared_ptr<erhe::Hierarchy>{});
        mesh->set_primitives(new_primitives);
        mesh_node->set_parent(parent);

        context.app_message_bus->mesh_geometry_changed.send_message(
            Mesh_geometry_changed_message{.mesh = mesh}
        );
    }

    scene_lock.unlock();
    if (background_optimize) {
        kickoff_deferred_finalize(context, m_parameters.mesh);
    }
}

} // namespace editor
