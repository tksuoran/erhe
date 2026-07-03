#include "texture_graph/nodes/texture_output_node.hpp"
#include "texture_graph/texture_graph_compose.hpp"
#include "texture_graph/texture_payload.hpp"
#include "texture_graph/texture_renderer.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_texgen/compose_node.hpp"
#include "erhe_texgen/shader_code.hpp"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cfloat>

namespace editor {

namespace {

constexpr const char* c_size_names[] = { "256", "512", "1024", "2048" };
constexpr int         c_size_values[] = { 256, 512, 1024, 2048 };

}

Texture_output_node::Texture_output_node(App_context& context)
    : Texture_graph_node{"Output"}
    , m_context         {context}
{
    // One pin per value type so any MVP generator can connect (pin keys are
    // per-type). f / rgb / rgba, keyed to match each type's source pin.
    make_input_pin(Texture_pin_key::grayscale, "f");
    make_input_pin(Texture_pin_key::rgb,       "rgb");
    make_input_pin(Texture_pin_key::rgba,      "rgba");
}

Texture_output_node::~Texture_output_node() noexcept
{
    unregister_texture();
}

void Texture_output_node::on_removed_from_graph()
{
    unregister_texture();
    if (m_assign_to_material && m_material) {
        m_material->data.texture_samplers.base_color.texture.reset();
        m_material->data.texture_samplers.base_color.sampler.reset();
    }
}

auto Texture_output_node::preview_display_size() const -> float
{
    return 160.0f;
}

auto Texture_output_node::render_target_size() const -> int
{
    const int index = std::clamp(m_size_index, 0, 3);
    return c_size_values[index];
}

auto Texture_output_node::connected_input_index() const -> int
{
    // Prefer the highest channel count when several inputs are connected.
    for (int i = 2; i >= 0; --i) {
        if (input_from_links(static_cast<std::size_t>(i)).source_node != nullptr) {
            return i;
        }
    }
    return -1;
}

void Texture_output_node::evaluate(Texture_graph&)
{
    // No outputs; refresh input payloads so a downstream query (none in the MVP)
    // and the UI's connectivity read are consistent.
    pull_inputs();
}

void Texture_output_node::render_products(App_context& context, Texture_renderer& renderer)
{
    if (context.current_command_buffer == nullptr) {
        return;
    }
    const int input_index = connected_input_index();
    if (input_index < 0) {
        return; // disconnected - keep the last baked texture
    }
    const Texture_payload source = input_from_links(static_cast<std::size_t>(input_index));
    if (source.source_node == nullptr) {
        return;
    }

    const Texture_compose_dag dag = build_texture_compose_dag(*source.source_node, source.output_index);
    const bool ok = render_dag(context, renderer, dag, get_preview_texture_ref(), render_target_size());
    if (!ok) {
        return; // compose error / buffer texture not ready - keep the previous texture
    }

    register_texture();
    if (m_assign_to_material) {
        assign_to_material();
    }
}

void Texture_output_node::register_texture()
{
    const std::shared_ptr<erhe::graphics::Texture>& texture = get_preview_texture();
    if (!texture) {
        return;
    }
    texture->set_name(m_texture_name);

    if (!m_scene_root) {
        m_scene_root = m_context.app_scenes->get_single_scene_root();
    }
    if (!m_scene_root) {
        return;
    }
    const std::shared_ptr<Content_library> library = m_scene_root->get_content_library();
    if (!library || !library->textures) {
        return;
    }
    if (m_registered_texture == texture) {
        return; // already registered (same object, only its contents were re-rendered)
    }
    if (m_registered_texture) {
        library->textures->remove(m_registered_texture);
    }
    library->textures->add(texture);
    m_registered_texture = texture;
}

void Texture_output_node::unregister_texture()
{
    if (m_registered_texture && m_scene_root) {
        const std::shared_ptr<Content_library> library = m_scene_root->get_content_library();
        if (library && library->textures) {
            library->textures->remove(m_registered_texture);
        }
    }
    m_registered_texture.reset();
}

void Texture_output_node::assign_to_material()
{
    if (!m_material) {
        return;
    }
    const std::shared_ptr<erhe::graphics::Texture>& texture = get_preview_texture();
    if (!texture) {
        return;
    }
    if (!m_sampler && (m_context.graphics_device != nullptr)) {
        m_sampler = std::make_shared<erhe::graphics::Sampler>(
            *m_context.graphics_device,
            erhe::graphics::Sampler_create_info{
                .min_filter  = erhe::graphics::Filter::linear,
                .mag_filter  = erhe::graphics::Filter::linear,
                .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
                .debug_label = erhe::utility::Debug_label{"Texture graph output sampler"}
            }
        );
    }
    m_material->data.texture_samplers.base_color.texture = texture;
    m_material->data.texture_samplers.base_color.sampler = m_sampler;
}

void Texture_output_node::imgui()
{
    // Texture name (committed on defocus, like the geometry output node).
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##texture_name", &m_texture_name);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        mark_dirty();
    }

    // Output size (power-of-two stepper).
    if (texture_enum_stepper("size", m_size_index, c_size_names, 4)) {
        mark_dirty();
    }

    // Scene selection (only shown when several scenes exist).
    const std::vector<std::shared_ptr<Scene_root>>& scene_roots = m_context.app_scenes->get_scene_roots();
    if (scene_roots.size() > 1) {
        int scene_index = 0;
        for (std::size_t i = 0, end = scene_roots.size(); i < end; ++i) {
            if (scene_roots[i] == m_scene_root) {
                scene_index = static_cast<int>(i);
                break;
            }
        }
        if (texture_index_stepper("scene", scene_index, static_cast<int>(scene_roots.size()))) {
            unregister_texture();
            m_scene_root = scene_roots.at(static_cast<std::size_t>(scene_index));
            m_material.reset();
            mark_dirty();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(m_scene_root ? m_scene_root->get_name().c_str() : "(no scene)");
    }

    // Material selection.
    std::shared_ptr<Scene_root> selection_root = m_scene_root ? m_scene_root : m_context.app_scenes->get_single_scene_root();
    if (selection_root) {
        const std::shared_ptr<Content_library> library = selection_root->get_content_library();
        if (library && library->materials) {
            const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials = library->materials->get_all<erhe::primitive::Material>();
            if (!materials.empty()) {
                int material_index = 0;
                for (std::size_t i = 0, end = materials.size(); i < end; ++i) {
                    if (materials[i] == m_material) {
                        material_index = static_cast<int>(i);
                        break;
                    }
                }
                if (texture_index_stepper("material", material_index, static_cast<int>(materials.size()))) {
                    m_material = materials.at(static_cast<std::size_t>(material_index));
                    mark_dirty();
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(m_material ? m_material->get_name().c_str() : "(no material)");
            }
        }
    }

    if (ImGui::Checkbox("Assign to base color", &m_assign_to_material)) {
        mark_dirty();
    }
}

void Texture_output_node::write_parameters(nlohmann::json& out) const
{
    out["name"]   = m_texture_name;
    out["size"]   = render_target_size();
    out["assign"] = m_assign_to_material;
    if (m_scene_root) {
        out["scene"] = m_scene_root->get_name();
    }
    if (m_material) {
        out["material"] = m_material->get_name();
    }
}

void Texture_output_node::read_parameters(const nlohmann::json& in)
{
    m_texture_name = in.value("name", m_texture_name);
    if (in.contains("size")) {
        const int size = in["size"].get<int>();
        for (int i = 0; i < 4; ++i) {
            if (c_size_values[i] == size) {
                m_size_index = i;
                break;
            }
        }
    }
    m_assign_to_material = in.value("assign", m_assign_to_material);

    const std::string scene_name = in.value("scene", "");
    if (!scene_name.empty()) {
        for (const std::shared_ptr<Scene_root>& scene_root : m_context.app_scenes->get_scene_roots()) {
            if (scene_root->get_name() == scene_name) {
                m_scene_root = scene_root;
                break;
            }
        }
    }
    const std::string material_name = in.value("material", "");
    if (!material_name.empty()) {
        std::shared_ptr<Scene_root> selection_root = m_scene_root ? m_scene_root : m_context.app_scenes->get_single_scene_root();
        if (selection_root) {
            const std::shared_ptr<Content_library> library = selection_root->get_content_library();
            if (library && library->materials) {
                for (const std::shared_ptr<erhe::primitive::Material>& material : library->materials->get_all<erhe::primitive::Material>()) {
                    if (material->get_name() == material_name) {
                        m_material = material;
                        break;
                    }
                }
            }
        }
    }
    mark_dirty();
}

} // namespace editor
