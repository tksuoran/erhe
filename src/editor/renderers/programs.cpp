#include "renderers/programs.hpp"
#include "init_status_display.hpp"

#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_profile/profile.hpp"

#include <fmt/format.h>
#include <taskflow/taskflow.hpp>

namespace editor {

Programs::Shader_stages_builder::Shader_stages_builder(
    erhe::graphics::Reloadable_shader_stages& reloadable_shader_stages,
    erhe::scene_renderer::Program_interface&  program_interface
)
    : reloadable_shader_stages{reloadable_shader_stages}
    , prototype{
        program_interface.make_prototype(reloadable_shader_stages.create_info)
    }
{
}

Programs::Shader_stages_builder::Shader_stages_builder(Shader_stages_builder&& other) noexcept
    : reloadable_shader_stages{other.reloadable_shader_stages}
    , prototype               {std::move(other.prototype)}
{
}

Programs::Programs(erhe::graphics::Device& graphics_device, erhe::scene_renderer::Program_interface& /*program_interface*/)
    : shader_paths{
        std::filesystem::path{"res"} / std::filesystem::path{"shaders"},
        std::filesystem::path{"res"} / std::filesystem::path{"editor"} / std::filesystem::path{"shaders"},
    }
    //, error                  {graphics_device, "error-not_loaded"}
    , brdf_slice             {graphics_device, "brdf_slice-not_loaded"}
    //, brush                  {graphics_device, "brush-not_loaded"}
    //, textured               {graphics_device, "textured-not_loaded"}
    , sky                    {graphics_device, "sky-not_loaded"}
    , grid                   {graphics_device, "grid-not_loaded"}
    , shadow_debug           {graphics_device, "shadow_debug-not_loaded"}
    , depth_to_color         {graphics_device, "depth_to_color-not_loaded"}
    //, id                     {graphics_device, "id-not_loaded"}
    , tool                   {graphics_device, "tool-not_loaded"}
{
}

void Programs::load_programs(
    tf::Executor&                            executor,
    erhe::graphics::Device&                  graphics_device,
    erhe::scene_renderer::Mesh_memory&       mesh_memory,
    erhe::scene_renderer::Program_interface& program_interface,
    Init_status_display&                     init_status_display
)
{
    std::vector<Shader_stages_builder> prototypes;

    using CI = erhe::graphics::Shader_stages_create_info;

    auto add_shader = [this, &program_interface, &prototypes, &init_status_display](
        erhe::graphics::Reloadable_shader_stages&   reloadable_shader_stages,
        erhe::graphics::Shader_stages_create_info&& create_info
    ) {
        init_status_display.set_line(
            2,
            fmt::format(
                "Loading shader stages: {}",
                create_info.debug_label.empty()
                    ? create_info.name.c_str()
                    : create_info.debug_label.data()
            )
        );

        reloadable_shader_stages.create_info = std::move(create_info);
        prototypes.emplace_back(reloadable_shader_stages, program_interface);
    };

    // Sky and grid are fullscreen / composition-pass shaders run inside
    // the same render pass as the rest of the scene. On the OpenXR
    // multiview path that render pass has viewMask = (1 << view_count)
    // - 1; the shaders read camera.cameras[c_view_index] in their
    // vertex stage. Compile with view_count = the session view count so
    // ERHE_MULTIVIEW is defined and c_view_index = uint(gl_ViewIndex)
    // (per-eye projection). Single-view sessions keep view_count = 1
    // and c_view_index = 0u (no behaviour change).
    add_shader(sky           , CI{ .name = "sky" , .view_count = static_cast<uint32_t>(program_interface.config.view_count) } );
    add_shader(grid          , CI{ .name = "grid", .view_count = static_cast<uint32_t>(program_interface.config.view_count) } );
    // shadow_debug reads camera.cameras[c_view_index] in its vertex stage, so it
    // is compiled with the session view_count exactly like sky/grid above.
    add_shader(shadow_debug  , CI{ .name = "shadow_debug", .view_count = static_cast<uint32_t>(program_interface.config.view_count) } );
    add_shader(brdf_slice    , CI{ .name = "brdf_slice"    } );
    add_shader(depth_to_color, CI{ .name = "depth_to_color"} );
    add_shader(tool          , CI{ .name = "tool", .vertex_format = &mesh_memory.vertex_format_not_skinned } );
    //add_shader(error     , CI{ .name = "error"     } );
    //add_shader(brush     , CI{ .name = "brush"     } );
    //add_shader(textured  , CI{ .name = "textured"  } );

    // Compile shaders

    static_cast<void>(executor);
    {
        ERHE_PROFILE_SCOPE("compile shaders");

        /// tf::Taskflow compile_taskflow;
        for (auto& entry : prototypes) {
            init_status_display.set_line(
                2,
                fmt::format(
                    "Compiling shader stages: {}",
                    entry.reloadable_shader_stages.create_info.debug_label.empty()
                        ? entry.reloadable_shader_stages.create_info.name.c_str()
                        : entry.reloadable_shader_stages.create_info.debug_label.data()
                )
            );
            entry.prototype.compile_shaders();
            // std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
        //// executor.run(compile_taskflow).wait();
    }

    // Link programs
    {
        ERHE_PROFILE_SCOPE("link programs");

        //// tf::Taskflow link_taskflow;
        for (auto& entry : prototypes) {
            init_status_display.set_line(
                2,
                fmt::format(
                    "Linking program: {}",
                    entry.reloadable_shader_stages.create_info.debug_label.empty()
                        ? entry.reloadable_shader_stages.create_info.name.c_str()
                        : entry.reloadable_shader_stages.create_info.debug_label.data()
                )
            );
            entry.prototype.link_program();
            // std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
    }

    {
        ERHE_PROFILE_SCOPE("post link");

        for (auto& entry : prototypes) {
            entry.reloadable_shader_stages.shader_stages.reload(std::move(entry.prototype));
            graphics_device.get_shader_monitor().add(entry.reloadable_shader_stages);
        }
    }

    init_status_display.set_line(2, "");
}

}
