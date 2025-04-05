#include "renderers/renderpass.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_rendering.hpp"
#include "time.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/render_context.hpp"
#include "renderers/render_style.hpp"
#include "scene/content_library.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"

#include "erhe_imgui/windows/pipelines.hpp"
#include "erhe_scene_renderer/forward_renderer.hpp"
#include "erhe_profile/profile.hpp"

#include <imgui/imgui.h>

namespace editor {

Renderpass::Renderpass(const Renderpass&)            = default;
Renderpass& Renderpass::operator=(const Renderpass&) = default;
Renderpass::~Renderpass() noexcept                   = default;

Renderpass::Renderpass(const std::string_view name)
    : Item{name}
{
}

auto Renderpass::get_static_type() -> uint64_t
{
    return erhe::Item_type::renderpass;
}

auto Renderpass::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Renderpass::get_type_name() const -> std::string_view
{
    return static_type_name;
}

auto mix(float x, float y, float a) -> float
{
    return x * (1.0f - a) + y * a;
}

auto triangle_wave(float t, float p) -> float
{
    return 2.0f * std::abs(2.0f * (t / p - std::floor(t / p + 0.5f))) - 1.0f;
}

void Renderpass::render(const Render_context& context) const
{
    ERHE_PROFILE_FUNCTION();

    // TODO This is a bit hacky, route this better.
    const int64_t t0_ns  = context.editor_context.time->get_host_system_time_ns();
    const double  t0     = static_cast<double>(t0_ns) / 1'000'000'000.0;
    const float   period = 1.0f / context.viewport_config.selection_highlight_frequency;
    const float   t1     = static_cast<float>(::fmod(t0, period));
    const float   t2     = static_cast<float>(0.5f + triangle_wave(t1, period) * 0.5f);
    context.editor_context.editor_rendering->selection_outline->primitive_settings = erhe::scene_renderer::Primitive_interface_settings{
        .color_source   = erhe::scene_renderer::Primitive_color_source::constant_color,
        .constant_color = glm::mix(
            context.viewport_config.selection_highlight_low,
            context.viewport_config.selection_highlight_high,
            t2
        ),
        .size_source    = erhe::scene_renderer::Primitive_size_source::constant_size,
        .constant_size  = mix(
            context.viewport_config.selection_highlight_width_low,
            context.viewport_config.selection_highlight_width_high,
            t2
        )
    };

    const auto scene_root = this->override_scene_root ? override_scene_root : context.scene_view.get_scene_root();
    if (!scene_root) {
        log_composer->error("Missing scene root - cannot render");
        return;
    }

    const Render_style_data* render_style = this->get_render_style
        ? &this->get_render_style(context)
        : nullptr;

    const auto& layers           = scene_root->layers();
    const auto& material_library = scene_root->content_library()->materials;

    // TODO: Keep this vector in content library and update when needed.
    //       Make content library item host for content library nodes.
    std::vector<std::shared_ptr<erhe::primitive::Material>> materials = material_library->get_all<erhe::primitive::Material>();

    using namespace erhe::primitive;

    if (
        (render_style != nullptr) &&
        !render_style->is_primitive_mode_enabled(this->primitive_mode)
    ) {
        log_composer->trace("primitive mode is not enabled - skipping");
        return;
    }

    //if (render_style.polygon_offset_enable) {
    //    gl::enable(gl::Enable_cap::polygon_offset_fill);
    //    gl::polygon_offset_clamp(render_style.polygon_offset_factor,
    //                             render_style.polygon_offset_units,
    //                             render_style.polygon_offset_clamp);
    //}

    //if (context.override_shader_stages != nullptr) {
    //    renderpass.pipeline.data.shader_stages = context.override_shader_stages;
    //}

    if (this->mesh_layers.empty()) {
        log_composer->debug("render_fullscreen");
        context.editor_context.forward_renderer->draw_primitives(
            erhe::scene_renderer::Forward_renderer::Render_parameters{
                .index_buffer           = &context.editor_context.mesh_memory->index_buffer,
                .vertex_buffer0         = &context.editor_context.mesh_memory->position_vertex_buffer,
                .vertex_buffer1         = &context.editor_context.mesh_memory->non_position_vertex_buffer,
                .camera                 = context.camera,
                .light_projections      = nullptr,
                .lights                 = {},
                .skins                  = {},
                .materials              = {},
                .mesh_spans             = {},
                .non_mesh_vertex_count  = this->non_mesh_vertex_count,
                .passes                 = this->passes,
                .primitive_mode         = this->primitive_mode,
                .primitive_settings     = 
                    primitive_settings.has_value()
                        ? primitive_settings.value()
                        : (render_style != nullptr)
                            ? render_style->get_primitive_settings(this->primitive_mode)
                            : erhe::scene_renderer::Primitive_interface_settings{},
                .shadow_texture         = nullptr,
                .viewport               = context.viewport,
                .override_shader_stages = this->allow_shader_stages_override ? context.override_shader_stages : nullptr,
                .error_shader_stages    = &context.editor_context.programs->error.shader_stages,
                .debug_label            = get_name()
            },
            nullptr
        );
    } else {
        erhe::scene::Scene* scene = scene_root->get_hosted_scene();
        std::vector<std::span<const std::shared_ptr<erhe::scene::Mesh>>> mesh_spans;
        for (const auto id : this->mesh_layers) {
            const auto mesh_layer = scene->get_mesh_layer_by_id(id);
            if (mesh_layer) {
                mesh_spans.push_back(mesh_layer->meshes);
                log_composer->trace("adding mesh layer {} with {} meshes", mesh_layer->name, mesh_layer->meshes.size());
            } else {
                log_composer->warn("mesh layer not found for id {}", id);
            }
        }
        if (mesh_spans.empty()) {
            return;
        }

        log_composer->debug("calling render with {} passes", passes.size());
        for (const auto& pass : passes) {
            log_composer->trace("pass using pipeline = {}", pass->pipeline.data.name);
        }
        log_composer->trace("primitive_mode = {}", c_str(primitive_mode));
        log_composer->trace("filter = {}", filter.describe());

        context.editor_context.forward_renderer->render(
            erhe::scene_renderer::Forward_renderer::Render_parameters{
                .index_type             = context.editor_context.mesh_memory->buffer_info.index_type,
                .index_buffer           = &context.editor_context.mesh_memory->index_buffer,
                .vertex_buffer0         = &context.editor_context.mesh_memory->position_vertex_buffer,
                .vertex_buffer1         = &context.editor_context.mesh_memory->non_position_vertex_buffer,
                .ambient_light          = layers.light()->ambient_light,
                .camera                 = context.camera,
                .light_projections      = context.scene_view.get_light_projections(),
                .lights                 = layers.light()->lights,
                .skins                  = scene->get_skins(),
                .materials              = materials,
                .mesh_spans             = mesh_spans,
                .passes                 = this->passes,
                .primitive_mode         = this->primitive_mode,
                .primitive_settings     = 
                    primitive_settings.has_value()
                        ? primitive_settings.value()
                        : (render_style != nullptr)
                            ? render_style->get_primitive_settings(this->primitive_mode)
                            : erhe::scene_renderer::Primitive_interface_settings{},
                .shadow_texture         = context.scene_view.get_shadow_texture(),
                .viewport               = context.viewport,
                .filter                 = this->filter,
                .override_shader_stages = this->allow_shader_stages_override ? context.override_shader_stages : nullptr,
                .error_shader_stages    = &context.editor_context.programs->error.shader_stages,
                .debug_joint_indices    = context.editor_context.editor_rendering->debug_joint_indices,
                .debug_joint_colors     = context.editor_context.editor_rendering->debug_joint_colors,
                .debug_label            = get_name()
            }
        );
    }
}

void Renderpass::imgui()
{
    if (ImGui::TreeNodeEx("Pipeline passes", ImGuiTreeNodeFlags_Framed)) {
        int pipeline_pass_index = 0;
        for (erhe::renderer::Pipeline_renderpass* pass : passes) {
            ImGui::PushID(pipeline_pass_index++);
            erhe::imgui::pipeline_imgui(pass->pipeline);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }   
    ImGui::Text("Primitive Mode: %s", erhe::primitive::c_str(primitive_mode));
    ImGui::Checkbox("Allow shader stages override", &allow_shader_stages_override);
    if (primitive_settings.has_value()) {
        //static void render_style_ui(Render_style_data& render_style);
    }
}

} // namespace editor
