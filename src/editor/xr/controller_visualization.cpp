#include "xr/controller_visualization.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "scene/scene_root.hpp"

#include "erhe_gltf/gltf.hpp"
#include "erhe_gltf/image_transfer.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_geometry/shapes/torus.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_xr/xr_action.hpp"
#include "erhe_xr/xr_log.hpp"
#include "erhe_xr/xr_session.hpp"

using erhe::geometry::transform_mesh;
using erhe::geometry::to_geo_mat4f;

namespace editor {

Controller_visualization::Controller_visualization(
    erhe::scene::Node*                 view_root,
    erhe::scene_renderer::Mesh_memory& mesh_memory,
    Scene_root&                        scene_root
)
    : m_mesh_memory{mesh_memory}
{
    ERHE_PROFILE_FUNCTION();

    m_material_library = scene_root.get_content_library()->materials;
    m_content_layer_id = scene_root.layers().content()->id;

    auto controller_material = m_material_library->make<erhe::primitive::Material>(
        erhe::primitive::Material_create_info{
            .name = "Controller",
            .data = {
                .base_color = glm::vec3{0.1f, 0.1f, 0.2f}
            }
        }
    );

    GEO::Mesh controller_geo_mesh{3, true};
    erhe::geometry::shapes::make_torus(controller_geo_mesh, 0.05f, 0.0025f, 40, 14);
    transform_mesh(controller_geo_mesh, to_geo_mat4f(erhe::math::mat4_swap_yz));

    erhe::primitive::Element_mappings dummy{};
    erhe::primitive::Buffer_mesh buffer_mesh{};
    const bool buffer_mesh_ok = erhe::primitive::build_buffer_mesh(
        buffer_mesh,
        controller_geo_mesh,
        erhe::primitive::Build_info{
            .primitive_types = {.fill_triangles = true },
            .buffer_info = mesh_memory.make_primitive_buffer_info()
        },
        dummy, // TODO make element mappings optional
        erhe::primitive::Normal_style::corner_normals
    );
    ERHE_VERIFY(buffer_mesh_ok); // TODO handle possible error (out of memory)

    std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(std::move(buffer_mesh));

    for (const bool right_hand : { false, true }) {
        Hand& hand = get_hand(right_hand);
        const char* const node_name = right_hand ? "Controller node right" : "Controller node left";
        const char* const mesh_name = right_hand ? "Controller right"      : "Controller left";
        hand.node             = std::make_shared<erhe::scene::Node>(node_name);
        hand.placeholder_mesh = std::make_shared<erhe::scene::Mesh>(mesh_name);
        hand.placeholder_mesh->add_primitive(primitive, controller_material);
        hand.node->enable_flag_bits(erhe::Item_flags::visible);
        hand.placeholder_mesh->enable_flag_bits(erhe::Item_flags::controller);
        hand.placeholder_mesh->layer_id = m_content_layer_id;
        hand.node->attach(hand.placeholder_mesh);
        hand.node->set_parent(view_root);
    }
}

auto Controller_visualization::get_hand(const bool right_hand) -> Hand&
{
    return right_hand ? m_right_hand : m_left_hand;
}

void Controller_visualization::load_render_models(App_context& context, erhe::xr::Xr_session& xr_session)
{
    load_render_model(context, xr_session, false);
    load_render_model(context, xr_session, true);
}

void Controller_visualization::load_render_model(App_context& context, erhe::xr::Xr_session& xr_session, const bool right_hand)
{
    ERHE_PROFILE_FUNCTION();

    Hand& hand = get_hand(right_hand);
    if (hand.has_render_model) {
        return;
    }
    if ((context.graphics_device == nullptr) || (context.current_command_buffer == nullptr) || (context.executor == nullptr)) {
        return;
    }

    erhe::xr::Render_model_data model = xr_session.load_controller_render_model(right_hand);
    if (model.data.empty()) {
        log_xr->info("No controller render model for {} hand - keeping torus placeholder", right_hand ? "right" : "left");
        return;
    }

    // Parse the GLB under a detached node; attach only on success.
    // Textures are currently NOT decoded: the runtime models use
    // KHR_texture_basisu (KTX2) images which the image loader does not
    // handle yet, so materials render with their factors only.
    std::shared_ptr<erhe::scene::Node> model_root = std::make_shared<erhe::scene::Node>(
        right_hand ? "Controller model right" : "Controller model left"
    );
    model_root->enable_flag_bits(erhe::Item_flags::visible);

    erhe::gltf::Image_transfer image_transfer{*context.graphics_device, *context.current_command_buffer};
    const erhe::gltf::Gltf_parse_arguments parse_arguments{
        .graphics_device = *context.graphics_device,
        .executor        = *context.executor,
        .image_transfer  = image_transfer,
        .root_node       = model_root,
        .mesh_layer_id   = m_content_layer_id,
        .path            = std::filesystem::path{model.name + ".glb"},
        .parallel        = false,
        .glb_data        = std::span<const std::byte>{reinterpret_cast<const std::byte*>(model.data.data()), model.data.size()}
    };
    const erhe::gltf::Gltf_data gltf_data = erhe::gltf::parse_gltf(parse_arguments);

    const erhe::primitive::Build_info build_info{
        .primitive_types = { .fill_triangles = true },
        .buffer_info     = m_mesh_memory.make_primitive_buffer_info()
    };
    std::size_t renderable_primitive_count = 0;
    for (const std::shared_ptr<erhe::scene::Node>& node : gltf_data.nodes) {
        if (!node) {
            continue;
        }
        node->enable_flag_bits(erhe::Item_flags::visible);
        const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
        if (!mesh) {
            continue;
        }
        mesh->enable_flag_bits(erhe::Item_flags::controller);
        mesh->layer_id = m_content_layer_id;
        // Renderable fill triangles only: no edges, and deliberately no
        // raytrace - the controller ray originates next to the model and
        // must never hover-pick the controller itself.
        for (erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_mutable_primitives()) {
            const bool renderable_ok = mesh_primitive.primitive->make_renderable_mesh(build_info, erhe::primitive::Normal_style::corner_normals);
            if (renderable_ok) {
                ++renderable_primitive_count;
            } else {
                log_xr->warn("Controller render model '{}': failed to build renderable mesh", model.name);
            }
        }
    }
    if (renderable_primitive_count == 0) {
        log_xr->warn("Controller render model '{}': no renderable meshes - keeping torus placeholder", model.name);
        return;
    }

    for (const std::shared_ptr<erhe::primitive::Material>& material : gltf_data.materials) {
        if (material) {
            m_material_library->add(material);
        }
    }

    model_root->set_parent(hand.node.get());
    hand.node->detach(hand.placeholder_mesh.get());
    hand.has_render_model = true;
    log_xr->info(
        "Controller render model '{}' loaded for {} hand: {} renderable primitives",
        model.name,
        right_hand ? "right" : "left",
        renderable_primitive_count
    );
}

void Controller_visualization::update_hand(
    const bool                      right_hand,
    const erhe::xr::Xr_action_pose* grip_pose,
    const erhe::xr::Xr_action_pose* aim_pose,
    const glm::vec3&                camera_offset
)
{
    Hand& hand = get_hand(right_hand);
    if (!hand.node) {
        return;
    }
    // The render model origin is defined at the grip pose; the torus
    // placeholder follows the aim pose (its historical behavior).
    const erhe::xr::Xr_action_pose* pose = hand.has_render_model ? grip_pose : aim_pose;
    const bool tracked = (pose != nullptr) && (pose->location.locationFlags != 0);
    if (!tracked) {
        hand.node->disable_flag_bits(erhe::Item_flags::visible);
        return;
    }
    hand.node->enable_flag_bits(erhe::Item_flags::visible);
    const glm::mat4 orientation = glm::mat4_cast(pose->orientation);
    const glm::mat4 translation = glm::translate(glm::mat4{1}, pose->position + camera_offset);
    const glm::mat4 m           = translation * orientation;
    hand.node->set_parent_from_node(m);
}

}
