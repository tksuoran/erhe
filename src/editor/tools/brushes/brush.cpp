#include "tools/brushes/brush.hpp"

#include "editor_settings.hpp"
#include "scene/content_library.hpp"
#include "scene/node_physics.hpp"
#include "editor_log.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/clone.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"

namespace editor {

using erhe::geometry::Polygon; // Resolve conflict with wingdi.h BOOL Polygon(HDC,const POINT *,int)
using erhe::geometry::c_polygon_centroids;
using erhe::geometry::c_polygon_normals;
using erhe::geometry::c_point_locations;
using erhe::geometry::Corner_id;
using erhe::geometry::Point_id;
using erhe::geometry::Polygon_id;
using glm::mat4;
using glm::vec3;
using glm::vec4;

auto Brush_data::get_name() const -> const std::string&
{
    if (name.empty() && geometry) {
        return geometry->name;
    }
    return name;
}

auto Brush::get_static_type() -> uint64_t
{
    return erhe::Item_type::brush;
}

auto Brush::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Brush::get_type_name() const -> std::string_view
{
    return static_type_name;
}

Brush::Brush(const Brush_data& create_info)
    : Item  {create_info.get_name()}
    , m_data{create_info}
{
    enable_flag_bits(erhe::Item_flags::brush | erhe::Item_flags::show_in_ui);
    if (m_data.geometry) {
        update_polygon_statistics();
    }
}

auto Brush::get_max_corner_count() const -> std::size_t
{
    return m_max_corner_count;
}

void Brush::update_polygon_statistics()
{
    const auto geometry = get_geometry();
    ERHE_VERIFY(geometry);

    m_corner_count_to_polygons.clear();
    m_max_corner_count = 0;
    geometry->for_each_polygon_const(
        [this](erhe::geometry::Geometry::Polygon_context_const& i){
            const uint32_t corner_count = i.polygon.corner_count;
            m_max_corner_count = std::max(m_max_corner_count, static_cast<std::size_t>(corner_count));
            auto j = m_corner_count_to_polygons.find(corner_count);
            if (j == m_corner_count_to_polygons.end()) {
                m_corner_count_to_polygons.insert({corner_count, {i.polygon_id}});
                return;
            }
            j->second.push_back(i.polygon_id);
        }
    );
}

auto Brush::get_corner_count_to_polygons() -> const std::map<std::size_t, std::vector<erhe::geometry::Polygon_id>>&
{
    if (!m_data.geometry) {
        late_initialize();
    }
    return m_corner_count_to_polygons;
}

void Brush::late_initialize()
{
    const auto geometry = get_geometry();
    ERHE_VERIFY(geometry);
    m_primitive = erhe::primitive::Primitive{geometry};
    if (!m_primitive.has_renderable_triangles()) {
        ERHE_VERIFY(m_primitive.make_renderable_mesh(m_data.build_info, m_data.normal_style));
    }
    ERHE_VERIFY(m_primitive.render_shape);
    const std::shared_ptr<erhe::primitive::Primitive_render_shape>& render_shape = m_primitive.render_shape;

    if (!render_shape->has_raytrace_triangles()) {
        render_shape->make_raytrace();
    }

    if (
        m_data.editor_settings.physics.static_enable &&
        !m_data.collision_shape &&
        !m_data.collision_shape_generator
    ) {
        ERHE_PROFILE_SCOPE("make brush convex hull collision shape");

        m_data.collision_shape = erhe::physics::ICollision_shape::create_convex_hull_shape_shared(
            reinterpret_cast<const float*>(
                geometry->point_attributes().find<vec3>(c_point_locations)->values.data()
            ),
            static_cast<int>(geometry->get_point_count()),
            static_cast<int>(sizeof(vec3))
        );
    }

    if ((m_data.volume == 0.0f) && m_data.collision_volume_calculator) {
        ERHE_PROFILE_SCOPE("calculate brush volume");

        m_data.volume = m_data.collision_volume_calculator(1.0f);
    }
}

Brush::Brush(Brush&& old) noexcept = default;

auto Brush::get_reference_frame(const uint32_t corner_count, const uint32_t in_face_offset, const uint32_t corner_offset) -> Reference_frame
{
    for (const auto& reference_frame : m_reference_frames) {
        if (
            (reference_frame.corner_count  == corner_count  ) &&
            (reference_frame.face_offset   == in_face_offset) &&
            (reference_frame.corner_offset == corner_offset )
        ) {
            return reference_frame;
        }
    }

    const auto geometry = get_geometry();

    uint32_t face_offset = 0;
    Polygon_id selected_polygon = 0;
    for (Polygon_id polygon_id = 0, end = geometry->get_polygon_count(); polygon_id < end; ++polygon_id) {
        const auto& polygon = geometry->polygons[polygon_id];
        if ((corner_count == 0) || (polygon.corner_count == corner_count) || (polygon_id + 1 == end)) {
            selected_polygon = polygon_id;
            if (face_offset == in_face_offset) {
                break;
            }
            ++face_offset;
        }
    }
    return m_reference_frames.emplace_back(*geometry.get(), selected_polygon, in_face_offset, corner_offset);
}

auto Brush::get_scaled(const float scale) -> const Scaled&
{
    late_initialize();
    const int scale_key = static_cast<int>(scale * c_scale_factor);
    for (const auto& scaled : m_scaled_entries) {
        if (scaled.scale_key == scale_key) {
            return scaled;
        }
    }
    Scaled& scaled = m_scaled_entries.emplace_back(create_scaled(scale_key));

    ERHE_VERIFY(m_primitive.render_shape);
    const std::shared_ptr<erhe::primitive::Primitive_render_shape>& scaled_render_shape = scaled.primitive.render_shape;
    if (!scaled_render_shape->has_raytrace_triangles()) {
        scaled_render_shape->make_raytrace();
    }
    return scaled;
}

auto Brush::create_scaled(const int scale_key) -> Scaled
{
    ERHE_PROFILE_FUNCTION();

    const float scale = static_cast<float>(scale_key) / c_scale_factor;
    if (scale == 1.0f) {
        glm::mat4 local_inertia{0.0f};
        if (m_data.editor_settings.physics.static_enable) {
            if (m_data.collision_shape) {
                ERHE_VERIFY(m_data.collision_shape->is_convex());
                const auto mass = m_data.density * m_data.volume;
                m_data.collision_shape->calculate_local_inertia(mass, local_inertia);
                return Scaled{
                    .scale_key          = scale_key,
                    .primitive          = m_primitive,
                    .collision_shape    = m_data.collision_shape,
                    .volume             = m_data.volume,
                    .local_inertia      = local_inertia
                };
            } else if (m_data.collision_shape_generator) {
                const auto generated_collision_shape = m_data.collision_shape_generator(scale);
                const auto mass = m_data.density * m_data.volume;
                generated_collision_shape->calculate_local_inertia(mass, local_inertia);
                return Scaled{
                    .scale_key       = scale_key,
                    .primitive       = m_primitive,
                    .collision_shape = generated_collision_shape,
                    .volume          = m_data.volume,
                    .local_inertia   = local_inertia
                };
            }
        }
        return Scaled{
            .scale_key       = scale_key,
            .primitive       = m_primitive,
            .collision_shape = {},
            .volume          = m_data.volume,
            .local_inertia   = local_inertia
        };
    }

    log_brush->trace("create_scaled() scale = {}", scale);

    auto scaled_geometry = std::make_shared<erhe::geometry::Geometry>(
        erhe::geometry::operation::clone(
            *m_primitive.render_shape->get_geometry().get(),
            erhe::math::create_scale(scale)
        )
    );
    scaled_geometry->name = fmt::format("{} scaled by {}", m_primitive.get_name(), scale);

    erhe::primitive::Primitive scaled_primitive{
        scaled_geometry,
        std::shared_ptr<erhe::primitive::Material>{},
        m_data.build_info,
        m_data.normal_style
    };

    glm::mat4 local_inertia{0.0f};
    if (m_data.editor_settings.physics.static_enable) {
        if (m_data.collision_shape) {
            ERHE_VERIFY(m_data.collision_shape->is_convex());
            const auto scaled_volume          = m_data.volume * scale * scale * scale;
            const auto mass                   = m_data.density * scaled_volume;
            auto       scaled_collision_shape = erhe::physics::ICollision_shape::create_uniform_scaling_shape_shared(
                m_data.collision_shape.get(),
                scale
            );
            scaled_collision_shape->calculate_local_inertia(mass, local_inertia);
            return Scaled{
                .scale_key       = scale_key,
                .primitive       = scaled_primitive,
                .collision_shape = scaled_collision_shape,
                .volume          = scaled_volume,
                .local_inertia   = local_inertia
            };
        } else if (m_data.collision_shape_generator) {
            auto       scaled_collision_shape = m_data.collision_shape_generator(scale);
            const auto scaled_volume          = m_data.collision_volume_calculator
                ? m_data.collision_volume_calculator(scale)
                : m_data.volume * scale * scale * scale;
            const auto mass                   = m_data.density * scaled_volume;
            scaled_collision_shape->calculate_local_inertia(mass, local_inertia);
            return Scaled{
                .scale_key       = scale_key,
                .primitive       = scaled_primitive,
                .collision_shape = scaled_collision_shape,
                .volume          = scaled_volume,
                .local_inertia   = local_inertia
            };
        }
    }
    return Scaled{
        .scale_key       = scale_key,
        .primitive       = scaled_primitive,
        .collision_shape = {},
        .volume          = m_data.volume * scale * scale * scale,
        .local_inertia   = local_inertia
    };
}

const std::string empty_string = {};

auto Brush::get_geometry() -> std::shared_ptr<erhe::geometry::Geometry>
{
    if (!m_data.geometry) {
        m_data.geometry = m_data.geometry_generator();
        m_data.geometry_generator = {};
        update_polygon_statistics();
    }
    return m_data.geometry;
}

auto Brush::make_instance(const Instance_create_info& instance_create_info) -> std::shared_ptr<erhe::scene::Node>
{
    ERHE_PROFILE_FUNCTION();

    late_initialize();

    const auto& scaled = get_scaled(instance_create_info.scale);

    const std::string_view name = this->get_name();

    log_scene->trace(
        "creating {} with material {} (material buffer index {})",
        name,
        instance_create_info.material->get_name(),
        instance_create_info.material->material_buffer_index
    );

    auto node = std::make_shared<erhe::scene::Node>(name);
    auto mesh = std::make_shared<erhe::scene::Mesh>(name);
    mesh->add_primitive(scaled.primitive, instance_create_info.material);

    ERHE_VERIFY(instance_create_info.scene_root != nullptr);

    mesh->layer_id = instance_create_info.scene_root->layers().content()->id;
    mesh->enable_flag_bits   (instance_create_info.mesh_flags);
    node->set_world_from_node(instance_create_info.world_from_node);
    node->attach             (mesh);
    node->enable_flag_bits   (instance_create_info.node_flags);

    if (m_data.editor_settings.physics.static_enable) {
        if (m_data.collision_shape || m_data.collision_shape_generator) {
            ERHE_PROFILE_SCOPE("make brush node physics");

            const erhe::physics::IRigid_body_create_info rigid_body_create_info{
                .collision_shape  = scaled.collision_shape,
                .mass             = m_data.density * scaled.volume,
                .inertia_override = scaled.local_inertia,
                .debug_label      = std::string{name},
                .motion_mode      = instance_create_info.motion_mode,
            };
            auto node_physics = std::make_shared<Node_physics>(rigid_body_create_info); // TODO use content library?
            node->attach(node_physics);
        }
    }

    return node;
}

auto Brush::get_bounding_box() -> erhe::math::Bounding_box
{
    if (
        !m_primitive.has_renderable_triangles() ||
        !m_primitive.render_shape->get_renderable_mesh().bounding_box.is_valid() // TODO is this condition needed?
    ) {
        late_initialize();
    }
    return m_primitive.get_bounding_box();
}

} // namespace editor
