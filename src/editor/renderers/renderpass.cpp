#include "renderers/renderpass.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_rendering.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/render_context.hpp"
#include "renderers/render_style.hpp"
#include "renderers/viewport_config.hpp"
#include "scene/content_library.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"

#include "erhe/graphics/debug.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/scene_renderer/forward_renderer.hpp"
#include "erhe/toolkit/profile.hpp"

namespace editor
{

Renderpass::Renderpass(const std::string_view name)
    : erhe::scene::Item{name}
{
}

[[nodiscard]] auto Renderpass::static_type() -> uint64_t
{
    return erhe::scene::Item_type::renderpass;
}

[[nodiscard]] auto Renderpass::static_type_name() -> const char*
{
    return "renderpass";
}

[[nodiscard]] auto Renderpass::get_type() const -> uint64_t
{
    return static_type();
}

[[nodiscard]] auto Renderpass::type_name() const -> const char*
{
    return static_type_name();
}

void Renderpass::render(const Render_context& context) const
{
    ERHE_PROFILE_FUNCTION();

    const auto scene_root = this->override_scene_root
        ? override_scene_root
        : context.scene_view.get_scene_root();
    if (!scene_root) {
        log_frame->error("Missing scene root - cannot render");
        return;
    }

    const Render_style_data* render_style = this->get_render_style
        ? &this->get_render_style(context)
        : nullptr;

    const auto& layers           = scene_root->layers();
    const auto& material_library = scene_root->content_library()->materials;
    const auto& materials        = material_library.entries();

    using namespace erhe::primitive;

    if (
        (render_style != nullptr) &&
        !render_style->is_primitive_mode_enabled(this->primitive_mode)
    ) {
        //// XXX TODO
        log_frame->trace("primitive mode is not enabled - skipping");
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
        log_frame->trace("render_fullscreen");
        context.editor_context.forward_renderer->render_fullscreen(
            erhe::scene_renderer::Forward_renderer::Render_parameters{
                .camera                 = &context.camera,
                .light_projections      = nullptr,
                .lights                 = {},
                .skins                  = {},
                .materials              = {},
                .mesh_spans             = {},
                .passes                 = this->passes,
                .primitive_mode         = this->primitive_mode,
                .primitive_settings     = (render_style != nullptr)
                    ? render_style->get_primitive_settings(this->primitive_mode)
                    : erhe::scene_renderer::Primitive_interface_settings{},
                .shadow_texture         = nullptr,
                .viewport               = context.viewport,
                .override_shader_stages = this->allow_shader_stages_override ? context.override_shader_stages : nullptr
            },
            nullptr
        );
    } else {
        erhe::scene::Scene* scene = scene_root->get_hosted_scene();
        std::vector<gsl::span<const std::shared_ptr<erhe::scene::Mesh>>> mesh_spans;
        for (const auto id : this->mesh_layers) {
            const auto mesh_layer = scene->get_mesh_layer_by_id(id);
            if (mesh_layer) {
                mesh_spans.push_back(mesh_layer->meshes);
                log_frame->trace("adding mesh layer {} with {} meshes", mesh_layer->name, mesh_layer->meshes.size());
            } else {
                log_frame->warn("mesh layer not found for id {}", id);
            }
        }
        if (mesh_spans.empty()) {
            return;
        }

        log_frame->trace("calling render with {} passes", passes.size());
        for (const auto& pass : passes) {
            log_frame->trace("pass using pipeline = {}", pass->pipeline.data.name);
        }
        log_frame->trace("primitive_mode = {}", c_str(primitive_mode));
        log_frame->trace("filter = {}", filter.describe());

        context.editor_context.forward_renderer->render(
            erhe::scene_renderer::Forward_renderer::Render_parameters{
                .index_type             = context.editor_context.mesh_memory->buffer_info.index_type,

                .ambient_light          = layers.light()->ambient_light,
                .camera                 = &context.camera,
                .light_projections      = context.scene_view.get_light_projections(),
                .lights                 = layers.light()->lights,
                .skins                  = scene->get_skins(),
                .materials              = materials,
                .mesh_spans             = mesh_spans,
                .passes                 = this->passes,
                .primitive_mode         = this->primitive_mode,
                .primitive_settings     = (render_style != nullptr)
                    ? render_style->get_primitive_settings(this->primitive_mode)
                    : erhe::scene_renderer::Primitive_interface_settings{},
                .shadow_texture         = context.scene_view.get_shadow_texture(),
                .viewport               = context.viewport,
                .filter                 = this->filter,
                .override_shader_stages = this->allow_shader_stages_override ? context.override_shader_stages : nullptr,
                .debug_joint_indices    = context.editor_context.editor_rendering->debug_joint_indices,
                .debug_joint_colors     = context.editor_context.editor_rendering->debug_joint_colors
            }
        );
    }
}

} // namespace editor
