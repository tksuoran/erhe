#include "tools/weight_paint_tool.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "graphics/icon_set.hpp"
#include "operations/operation_stack.hpp"
#include "operations/paint_weights_operation.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_view.hpp"
#include "tools/tools.hpp"
#include "tools/weight_display.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_verify/verify.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe_xr/xr_action.hpp"
#   include "erhe_xr/headset.hpp"
#endif

#include <geogram/mesh/mesh.h>

#include <glm/gtc/constants.hpp>

#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace editor {

using erhe::geometry::get_pointf;
using erhe::geometry::to_glm_vec3;

namespace {

constexpr float c_weight_zero_snap = 0.0001f; // Blender: paint below this snaps to exactly 0

[[nodiscard]] auto smooth_falloff(const float distance, const float radius) -> float
{
    if ((radius <= 0.0f) || (distance >= radius)) {
        return 0.0f;
    }
    const float p = 1.0f - distance / radius;
    return p * p * (3.0f - 2.0f * p); // Blender's SMOOTH curve
}

} // anonymous namespace

#pragma region Commands
Weight_paint_command::Weight_paint_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Weight_paint_tool.paint"}
    , m_context{context}
{
}

void Weight_paint_command::try_ready()
{
    if (!m_context.weight_paint_tool->is_enabled()) {
        return;
    }
    if (m_context.weight_paint_tool->try_ready()) {
        set_ready();
    }
}

auto Weight_paint_command::try_call() -> bool
{
    if (!m_context.weight_paint_tool->is_enabled()) {
        return false;
    }
    if (get_command_state() == erhe::commands::State::Ready) {
        set_active();
    }
    if (get_command_state() != erhe::commands::State::Active) {
        return false;
    }
    if (m_context.weight_paint_tool->get_hover_scene_view() == nullptr) {
        set_inactive();
        return false;
    }
    m_context.weight_paint_tool->paint();
    return true;
}

void Weight_paint_command::on_inactive()
{
    m_context.weight_paint_tool->end_stroke();
}
#pragma endregion Commands

Weight_paint_tool::Weight_paint_tool(
    erhe::commands::Commands& commands,
    App_context&              context,
    App_message_bus&          app_message_bus,
    Headset_view&             headset_view,
    Icon_set&                 icon_set,
    Tools&                    tools
)
    : Tool                          {context, tools, Tool_flags::toolbox}
    , m_paint_command               {commands, context}
    , m_drag_redirect_update_command{commands, m_paint_command}
    , m_drag_enable_command         {commands, m_drag_redirect_update_command}
{
    ERHE_PROFILE_FUNCTION();

    set_base_priority(c_priority);
    set_description  ("Weight Paint Tool");
    set_icon         (icon_set.custom_icons, icon_set.icons.bone);

    m_paint_command               .set_host(this);
    m_drag_redirect_update_command.set_host(this);
    m_drag_enable_command         .set_host(this);

    commands.register_command(&m_paint_command);
    commands.register_command(&m_drag_redirect_update_command);
    commands.register_command(&m_drag_enable_command);
    commands.bind_command_to_mouse_drag(&m_paint_command, erhe::window::Mouse_button_left, true);
#if defined(ERHE_XR_LIBRARY_OPENXR)
    erhe::xr::Headset*    headset  = headset_view.get_headset();
    erhe::xr::Xr_actions* xr_right = (headset != nullptr) ? headset->get_actions_right() : nullptr;
    if (xr_right != nullptr) {
        commands.bind_command_to_xr_boolean_action(&m_drag_enable_command, xr_right->a_click,       erhe::commands::Button_trigger::Any);
        commands.bind_command_to_xr_boolean_action(&m_drag_enable_command, xr_right->trigger_click, erhe::commands::Button_trigger::Any);
        commands.bind_command_to_update           (&m_drag_redirect_update_command);
    }
#else
    static_cast<void>(headset_view);
#endif

    m_hover_scene_view_subscription = app_message_bus.hover_scene_view.subscribe(
        [&](Hover_scene_view_message& message) {
            Tool::on_message(message);
        }
    );
}

void Weight_paint_tool::handle_priority_update(const int old_priority, const int new_priority)
{
    if (new_priority < old_priority) {
        disable_command_host();
        end_stroke();
    }
    if (new_priority > old_priority) {
        enable_command_host();
    }
}

void Weight_paint_tool::tool_render(const Render_context& context)
{
    if (!is_enabled()) {
        return;
    }
    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return;
    }
    const Hover_entry& content = scene_view->get_hover(Hover_entry::content_slot);
    if (!content.valid || !content.position.has_value() || !content.normal.has_value()) {
        return;
    }

    // Brush circle at the hover point, in the surface plane.
    const glm::vec3 center = content.position.value();
    const glm::vec3 n      = glm::normalize(content.normal.value());
    const glm::vec3 ref    = (std::abs(n.y) < 0.9f) ? glm::vec3{0.0f, 1.0f, 0.0f} : glm::vec3{1.0f, 0.0f, 0.0f};
    const glm::vec3 u      = glm::normalize(glm::cross(n, ref));
    const glm::vec3 v      = glm::cross(n, u);

    erhe::renderer::Primitive_renderer line_renderer = context.get({erhe::graphics::Primitive_type::line, 2, true, true});
    line_renderer.set_thickness(2.0f);
    constexpr int segment_count = 32;
    glm::vec3 previous = center + m_radius * u;
    for (int i = 1; i <= segment_count; ++i) {
        const float     angle = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(segment_count);
        const glm::vec3 point = center + m_radius * (std::cos(angle) * u + std::sin(angle) * v);
        line_renderer.add_lines(glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}, {{previous, point}});
        previous = point;
    }
}

auto Weight_paint_tool::try_ready() -> bool
{
    if (!Command_host::is_enabled()) {
        return false;
    }
    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return false;
    }
    const Hover_entry* hover = scene_view->get_nearest_hover(Hover_entry::all_bits);
    if ((hover == nullptr) || (hover->mask != Hover_entry::content_bit) || !hover->valid) {
        return false;
    }
    return begin_stroke();
}

auto Weight_paint_tool::begin_stroke() -> bool
{
    m_stroke_active = false;
    m_stroke_vertices.clear();

    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return false;
    }
    const Hover_entry& content = scene_view->get_hover(Hover_entry::content_slot);
    std::shared_ptr<erhe::scene::Mesh> scene_mesh = content.scene_mesh_weak.lock();
    if (!content.valid || !scene_mesh || !content.geometry) {
        return false;
    }

    if (m_context.weight_display == nullptr) {
        return false;
    }
    const std::shared_ptr<erhe::scene::Node> joint = m_context.weight_display->get_active_joint();
    if (!joint) {
        m_status = "No active joint - select a bone first";
        return false;
    }
    const std::shared_ptr<erhe::scene::Skin>& skin = scene_mesh->skin;
    if (!skin) {
        m_status = "Hovered mesh is not skinned";
        return false;
    }
    const std::vector<std::shared_ptr<erhe::scene::Node>>& joints = skin->skin_data.joints;
    uint32_t joint_local_index = std::numeric_limits<uint32_t>::max();
    for (std::size_t i = 0, end = joints.size(); i < end; ++i) {
        if (joints[i] == joint) {
            joint_local_index = static_cast<uint32_t>(i);
            break;
        }
    }
    if (joint_local_index == std::numeric_limits<uint32_t>::max()) {
        m_status = "Active joint is not part of the hovered mesh's skin";
        return false;
    }

    erhe::geometry::Mesh_attributes& attributes = content.geometry->get_attributes();
    const GEO::Mesh&                 geo_mesh   = content.geometry->get_mesh();
    if (
        (geo_mesh.vertices.nb() == 0) ||
        !attributes.vertex_joint_indices_0.try_get(0).has_value() ||
        !attributes.vertex_joint_weights_0.try_get(0).has_value()
    ) {
        m_status = "Mesh geometry has no joint attributes";
        return false;
    }

    ERHE_VERIFY(content.scene_mesh_primitive_index != std::numeric_limits<std::size_t>::max());

    m_stroke_mesh              = scene_mesh;
    m_stroke_primitive_index   = content.scene_mesh_primitive_index;
    m_stroke_geometry          = content.geometry;
    m_stroke_skin              = skin;
    m_stroke_joint_local_index = joint_local_index;
    m_stroke_active            = true;
    m_status.clear();
    return true;
}

auto Weight_paint_tool::blend_weight(const float base, const float alpha) const -> float
{
    const float a = std::min(alpha, 1.0f);
    float result = base;
    switch (m_blend) {
        case Weight_paint_blend::mix:      result = m_weight * a + base * (1.0f - a); break;
        case Weight_paint_blend::add:      result = base + m_weight * a;              break;
        case Weight_paint_blend::subtract: result = base - m_weight * a;              break;
    }
    result = std::clamp(result, 0.0f, 1.0f);
    if (result < c_weight_zero_snap) {
        result = 0.0f;
    }
    return result;
}

void Weight_paint_tool::paint()
{
    if (!m_stroke_active) {
        return;
    }
    apply_dab();
}

void Weight_paint_tool::apply_dab()
{
    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return;
    }
    const Hover_entry& content = scene_view->get_hover(Hover_entry::content_slot);
    std::shared_ptr<erhe::scene::Mesh> scene_mesh = content.scene_mesh_weak.lock();
    std::shared_ptr<erhe::scene::Mesh> stroke_mesh = m_stroke_mesh.lock();
    if (!content.valid || !content.position.has_value() || !scene_mesh || !stroke_mesh) {
        return;
    }
    // Stroke lock: dabs landing on another mesh or another primitive of the
    // same mesh are ignored until the stroke ends.
    if ((scene_mesh != stroke_mesh) || (content.scene_mesh_primitive_index != m_stroke_primitive_index)) {
        return;
    }

    const glm::vec3 brush_center = content.position.value();

    glm::vec3 camera_position{0.0f};
    bool      have_camera{false};
    const std::shared_ptr<erhe::scene::Camera> camera = scene_view->get_camera();
    if (camera && (camera->get_node() != nullptr)) {
        camera_position = glm::vec3{camera->get_node()->position_in_world()};
        have_camera = true;
    }

    // World-from-bind matrix per skin joint for CPU skinning of candidate
    // vertices; the mesh may be posed and the user paints on the posed
    // surface. nullopt (expired joint) is treated as identity, matching
    // get_world_from_bind's own fallback for missing inverse-bind matrices.
    const erhe::scene::Skin_data& skin_data = m_stroke_skin->skin_data;
    std::vector<glm::mat4> world_from_bind;
    world_from_bind.reserve(skin_data.joints.size());
    for (std::size_t i = 0, end = skin_data.joints.size(); i < end; ++i) {
        world_from_bind.push_back(skin_data.get_world_from_bind(i).value_or(glm::mat4{1.0f}));
    }

    erhe::geometry::Mesh_attributes& attributes = m_stroke_geometry->get_attributes();
    const GEO::Mesh&                 geo_mesh   = m_stroke_geometry->get_mesh();

    const erhe::scene::Node* node = stroke_mesh->get_node();

    for (GEO::index_t vertex = 0, end = geo_mesh.vertices.nb(); vertex < end; ++vertex) {
        const std::optional<GEO::vec4u> ji_opt = attributes.vertex_joint_indices_0.try_get(vertex);
        const std::optional<GEO::vec4f> jw_opt = attributes.vertex_joint_weights_0.try_get(vertex);
        if (!ji_opt.has_value() || !jw_opt.has_value()) {
            continue;
        }
        const GEO::vec4u ji = ji_opt.value();
        const GEO::vec4f jw = jw_opt.value();

        // CPU-skin the bind-pose position (linear blend, same sum the GPU
        // does). Weight sum ~0 leaves the vertex unposed; fall back to the
        // node transform so it sits at its rest position.
        const glm::vec3 p_local = to_glm_vec3(get_pointf(geo_mesh.vertices, vertex));
        const float weight_sum = jw.x + jw.y + jw.z + jw.w;
        glm::vec3 p_world;
        glm::mat4 skin_matrix{0.0f};
        if (weight_sum > 1e-6f) {
            for (int k = 0; k < 4; ++k) {
                const uint32_t j = ji[k];
                if ((jw[k] > 0.0f) && (j < world_from_bind.size())) {
                    skin_matrix += jw[k] * world_from_bind[j];
                }
            }
            p_world = glm::vec3{skin_matrix * glm::vec4{p_local, 1.0f}};
        } else if (node != nullptr) {
            p_world = node->transform_point_from_local_to_world(p_local);
        } else {
            continue;
        }

        const float distance = glm::distance(p_world, brush_center);
        if (distance > m_radius) {
            continue;
        }

        if (m_front_face_only && have_camera && (weight_sum > 1e-6f)) {
            const std::optional<GEO::vec3f> n_opt = attributes.vertex_normal.try_get(vertex);
            const std::optional<GEO::vec3f> ns_opt = n_opt.has_value() ? n_opt : attributes.vertex_normal_smooth.try_get(vertex);
            if (ns_opt.has_value()) {
                // mat3 of the blended skin matrix approximates the normal
                // transform (exact under rotation + uniform scale).
                const glm::vec3 n_world  = glm::mat3{skin_matrix} * to_glm_vec3(ns_opt.value());
                const glm::vec3 view_dir = p_world - camera_position; // camera -> surface
                if (glm::dot(view_dir, n_world) > 0.0f) {
                    continue; // facing away
                }
            }
        }

        const float alpha = smooth_falloff(distance, m_radius) * m_strength;
        if (alpha <= 0.0f) {
            continue;
        }

        // First touch this stroke: record the undo snapshot and the
        // stroke-start weight of the active joint.
        auto [it, inserted] = m_stroke_vertices.try_emplace(vertex);
        Stroke_vertex& stroke_vertex = it->second;
        if (inserted) {
            stroke_vertex.before_joint_indices = glm::uvec4{ji.x, ji.y, ji.z, ji.w};
            stroke_vertex.before_joint_weights = glm::vec4{jw.x, jw.y, jw.z, jw.w};
            float start_weight = 0.0f;
            for (int k = 0; k < 4; ++k) {
                if (ji[k] == m_stroke_joint_local_index) {
                    start_weight = std::max(start_weight, jw[k]);
                }
            }
            stroke_vertex.stroke_start_weight = start_weight;
        }

        glm::uvec4 indices{ji.x, ji.y, ji.z, ji.w};
        glm::vec4  weights{jw.x, jw.y, jw.z, jw.w};

        float base_weight;
        if (m_accumulate) {
            base_weight = 0.0f;
            for (int k = 0; k < 4; ++k) {
                if (indices[k] == m_stroke_joint_local_index) {
                    base_weight = std::max(base_weight, weights[k]);
                }
            }
        } else {
            // Non-accumulate: a dab applies only where its alpha exceeds the
            // strongest alpha seen this stroke, and blends from the
            // stroke-start weight - overlapping dabs converge to the target
            // instead of compounding.
            if (alpha <= stroke_vertex.alpha_max) {
                continue;
            }
            stroke_vertex.alpha_max = alpha;
            base_weight = stroke_vertex.stroke_start_weight;
        }

        const float new_weight = blend_weight(base_weight, alpha);

        // Find the active joint's slot: prefer the matching-index slot with
        // the highest weight (unused slots are typically index 0 weight 0).
        int slot = -1;
        for (int k = 0; k < 4; ++k) {
            if ((indices[k] == m_stroke_joint_local_index) && ((slot < 0) || (weights[k] > weights[slot]))) {
                slot = k;
            }
        }
        if (slot < 0) {
            if (new_weight <= 0.0f) {
                continue; // not influenced and painting zero: nothing to do
            }
            // Insert by evicting the smallest influence - but only if the new
            // weight actually exceeds it (4-influence limit of JOINTS_0).
            int min_slot = 0;
            for (int k = 1; k < 4; ++k) {
                if (weights[k] < weights[min_slot]) {
                    min_slot = k;
                }
            }
            if (weights[min_slot] >= new_weight) {
                continue;
            }
            slot = min_slot;
            indices[slot] = m_stroke_joint_local_index;
        }
        weights[slot] = new_weight;

        if (m_auto_normalize) {
            // Rescale the other influences so the total is 1, keeping the
            // painted value. All-zero others are left alone (total < 1);
            // erhe's skinning shader does not renormalize, so this matches
            // the "weights may sum < 1 when painting a single influence"
            // note in doc/weight-paint-plan.md.
            float others_sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                if (k != slot) {
                    others_sum += weights[k];
                }
            }
            if (others_sum > 0.0f) {
                const float scale = (1.0f - new_weight) / others_sum;
                for (int k = 0; k < 4; ++k) {
                    if (k != slot) {
                        weights[k] = std::max(0.0f, weights[k] * scale);
                        if (weights[k] < c_weight_zero_snap) {
                            weights[k] = 0.0f;
                        }
                    }
                }
            }
        }

        write_vertex_joints(vertex, indices, weights);
    }
}

void Weight_paint_tool::write_vertex_joints(const GEO::index_t vertex, const glm::uvec4& joint_indices, const glm::vec4& joint_weights)
{
    erhe::geometry::Mesh_attributes& attributes = m_stroke_geometry->get_attributes();
    attributes.vertex_joint_indices_0.set(vertex, GEO::vec4u{joint_indices.x, joint_indices.y, joint_indices.z, joint_indices.w});
    attributes.vertex_joint_weights_0.set(vertex, GEO::vec4f{joint_weights.x, joint_weights.y, joint_weights.z, joint_weights.w});

    // Patch the fill mesh's GPU attributes for every GPU vertex spawned from
    // this geometry vertex (corner fan -> corner-to-vertex-buffer mapping).
    std::shared_ptr<erhe::scene::Mesh> stroke_mesh = m_stroke_mesh.lock();
    if (!stroke_mesh) {
        return;
    }
    const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = stroke_mesh->get_primitives();
    if (m_stroke_primitive_index >= mesh_primitives.size()) {
        return;
    }
    const erhe::primitive::Primitive& primitive = *mesh_primitives.at(m_stroke_primitive_index).primitive.get();
    if (!primitive.render_shape) {
        return;
    }
    const erhe::primitive::Element_mappings& element_mappings = primitive.render_shape->get_element_mappings();
    const std::vector<GEO::index_t>& vertex_corners = m_stroke_geometry->get_vertex_corners(vertex);
    for (const GEO::index_t corner : vertex_corners) {
        if (corner >= element_mappings.mesh_corner_to_vertex_buffer_index.size()) {
            continue;
        }
        enqueue_gpu_joint_data(element_mappings.mesh_corner_to_vertex_buffer_index[corner], joint_indices, joint_weights);
    }
}

void Weight_paint_tool::enqueue_gpu_joint_data(const uint32_t vertex_buffer_index, const glm::uvec4& joint_indices, const glm::vec4& joint_weights)
{
    std::shared_ptr<erhe::scene::Mesh> stroke_mesh = m_stroke_mesh.lock();
    if (!stroke_mesh) {
        return;
    }
    erhe::scene_renderer::Mesh_memory& mesh_memory = *m_context.mesh_memory;

    const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = stroke_mesh->get_primitives();
    const erhe::primitive::Primitive& primitive = *mesh_primitives.at(m_stroke_primitive_index).primitive.get();
    const erhe::primitive::Buffer_mesh&             buffer_mesh   = primitive.render_shape->get_renderable_mesh();
    const erhe::scene_renderer::Vertex_input_entry& vertex_input  = mesh_memory.get_vertex_input(buffer_mesh.vertex_input_key);
    const erhe::dataformat::Vertex_format&          vertex_format = vertex_input.vertex_format;

    struct Write {
        erhe::dataformat::Vertex_attribute_usage usage;
        bool                                     is_weights;
    };
    const Write writes[2] = {
        { erhe::dataformat::Vertex_attribute_usage::joint_indices, false },
        { erhe::dataformat::Vertex_attribute_usage::joint_weights, true  }
    };
    for (const Write& write : writes) {
        const erhe::dataformat::Attribute_stream attribute_stream = vertex_format.find_attribute(write.usage, 0);
        if (attribute_stream.attribute == nullptr) {
            continue;
        }
        const std::size_t stream_index      = attribute_stream.stream - vertex_format.streams.data();
        const std::size_t vertex_offset     = vertex_buffer_index * attribute_stream.stream->stride + attribute_stream.attribute->offset;
        const erhe::primitive::Buffer_range stream_vertex_buffer_range = buffer_mesh.vertex_buffer_ranges.at(stream_index);
        const erhe::primitive::Buffer_range vertex_buffer_update_range{
            .count        = 1,
            .element_size = erhe::dataformat::get_format_size_bytes(attribute_stream.attribute->format),
            .byte_offset  = stream_vertex_buffer_range.byte_offset + vertex_offset,
            .pool_id      = stream_vertex_buffer_range.pool_id,
            .buffer_id    = stream_vertex_buffer_range.buffer_id
        };

        std::vector<std::uint8_t> buffer;
        if (write.is_weights) {
            if (attribute_stream.attribute->format == erhe::dataformat::Format::format_8_vec4_unorm) {
                buffer.resize(4);
                for (int k = 0; k < 4; ++k) {
                    buffer[k] = erhe::dataformat::float_to_unorm8(joint_weights[k]);
                }
            } else if (attribute_stream.attribute->format == erhe::dataformat::Format::format_32_vec4_float) {
                buffer.resize(4 * sizeof(float));
                auto* const ptr = reinterpret_cast<float*>(buffer.data());
                for (int k = 0; k < 4; ++k) {
                    ptr[k] = joint_weights[k];
                }
            } else {
                continue;
            }
        } else {
            if (attribute_stream.attribute->format == erhe::dataformat::Format::format_8_vec4_uint) {
                buffer.resize(4);
                for (int k = 0; k < 4; ++k) {
                    buffer[k] = static_cast<std::uint8_t>(std::min(joint_indices[k], 255u));
                }
            } else if (attribute_stream.attribute->format == erhe::dataformat::Format::format_32_vec4_uint) {
                buffer.resize(4 * sizeof(uint32_t));
                auto* const ptr = reinterpret_cast<uint32_t*>(buffer.data());
                for (int k = 0; k < 4; ++k) {
                    ptr[k] = joint_indices[k];
                }
            } else {
                continue;
            }
        }
        mesh_memory.enqueue_vertex_data(vertex_buffer_update_range, std::move(buffer));
    }
    // Flush happens once per frame (Mesh_memory::flush), as in Paint_tool.
}

void Weight_paint_tool::end_stroke()
{
    if (!m_stroke_active) {
        return;
    }
    m_stroke_active = false;

    std::shared_ptr<erhe::scene::Mesh> stroke_mesh = m_stroke_mesh.lock();
    if (!stroke_mesh || !m_stroke_geometry || m_stroke_vertices.empty()) {
        m_stroke_vertices.clear();
        return;
    }
    const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = stroke_mesh->get_primitives();
    if (m_stroke_primitive_index >= mesh_primitives.size() || !mesh_primitives.at(m_stroke_primitive_index).primitive) {
        m_stroke_vertices.clear();
        return;
    }
    const erhe::primitive::Primitive& primitive = *mesh_primitives.at(m_stroke_primitive_index).primitive.get();
    if (!primitive.render_shape) {
        m_stroke_vertices.clear();
        return;
    }

    // One undoable operation per stroke. The stroke already wrote the final
    // ("after") values into the geometry and the fill mesh; queueing executes
    // the operation, whose primitive rebuild refreshes the wireframe /
    // edge-line joint streams (the attribute rewrite is idempotent).
    Paint_weights_operation::Parameters parameters{
        .mesh            = stroke_mesh,
        .primitive_index = m_stroke_primitive_index,
        .geometry        = m_stroke_geometry,
        .build_info      = erhe::primitive::Build_info{
            .primitive_types = {
                .fill_triangles          = true,
                .fill_triangles_expanded = true,
                .edge_lines              = true,
                .corner_points           = true,
                .centroid_points         = true
            },
            .buffer_info = m_context.mesh_memory->make_primitive_buffer_info()
        },
        .normal_style    = primitive.render_shape->get_normal_style()
    };
    const erhe::geometry::Mesh_attributes& attributes = m_stroke_geometry->get_attributes();
    parameters.vertices            .reserve(m_stroke_vertices.size());
    parameters.before_joint_indices.reserve(m_stroke_vertices.size());
    parameters.before_joint_weights.reserve(m_stroke_vertices.size());
    parameters.after_joint_indices .reserve(m_stroke_vertices.size());
    parameters.after_joint_weights .reserve(m_stroke_vertices.size());
    for (const auto& [vertex, stroke_vertex] : m_stroke_vertices) {
        const std::optional<GEO::vec4u> ji = attributes.vertex_joint_indices_0.try_get(vertex);
        const std::optional<GEO::vec4f> jw = attributes.vertex_joint_weights_0.try_get(vertex);
        if (!ji.has_value() || !jw.has_value()) {
            continue;
        }
        parameters.vertices            .push_back(vertex);
        parameters.before_joint_indices.push_back(stroke_vertex.before_joint_indices);
        parameters.before_joint_weights.push_back(stroke_vertex.before_joint_weights);
        parameters.after_joint_indices .push_back(glm::uvec4{ji->x, ji->y, ji->z, ji->w});
        parameters.after_joint_weights .push_back(glm::vec4{jw->x, jw->y, jw->z, jw->w});
    }
    m_stroke_vertices.clear();

    if (parameters.vertices.empty()) {
        return;
    }
    m_context.operation_stack->queue(
        std::make_shared<Paint_weights_operation>(std::move(parameters))
    );
}

void Weight_paint_tool::tool_properties(erhe::imgui::Imgui_window&)
{
    if (m_context.weight_display != nullptr) {
        m_context.weight_display->imgui();
    }
    ImGui::SliderFloat("Weight",   &m_weight,   0.0f, 1.0f);
    ImGui::SliderFloat("Strength", &m_strength, 0.0f, 1.0f);
    ImGui::SliderFloat("Radius",   &m_radius,   0.01f, 2.0f);
    erhe::imgui::make_combo("Blend", m_blend, c_weight_paint_blend_strings, IM_ARRAYSIZE(c_weight_paint_blend_strings));
    ImGui::Checkbox("Accumulate",      &m_accumulate);
    ImGui::Checkbox("Auto Normalize",  &m_auto_normalize);
    ImGui::Checkbox("Front Faces Only", &m_front_face_only);
    if (!m_status.empty()) {
        ImGui::TextUnformatted(m_status.c_str());
    }
}

}
