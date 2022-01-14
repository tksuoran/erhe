#include "editor_imgui_windows.hpp"

#include "configuration.hpp"
#include "rendering.hpp"
#include "window.hpp"

#include "renderers/mesh_memory.hpp"
#include "graphics/gl_context_provider.hpp"
#include "scene/scene_root.hpp"
#include "tools/tool.hpp"
#include "windows/imgui_window.hpp"

#include "erhe/geometry/shapes/regular_polygon.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/state/color_blend_state.hpp"
#include "erhe/imgui/imgui_impl_erhe.hpp"

#include <backends/imgui_impl_glfw.h>

namespace editor {

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

Editor_imgui_windows::Editor_imgui_windows()
    : erhe::components::Component{c_name}
{
}

Editor_imgui_windows::~Editor_imgui_windows() = default;

void Editor_imgui_windows::connect()
{
    m_editor_rendering       = get    <Editor_rendering>();
    m_pipeline_state_tracker = require<erhe::graphics::OpenGL_state_tracker>();

    require<Configuration      >();
    require<Gl_context_provider>();
    require<Mesh_memory        >();
    require<Programs           >();
    require<Scene_root         >();
    require<Window             >();
}

void Editor_imgui_windows::initialize_component()
{
    const Scoped_gl_context gl_context{Component::get<Gl_context_provider>()};

    IMGUI_CHECKVERSION();
    m_imgui_context = ImGui::CreateContext();
    ImGuiIO& io     = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    io.Fonts->AddFontFromFileTTF(
        "res/fonts/SourceSansPro-Regular.otf",
        get<Configuration>()->viewports_hosted_in_imgui_windows
            ? 17.0f
            : 22.0f
    );

    ImFontGlyphRangesBuilder builder;

    //ImGui::Text(u8"Hiragana: 要らない.txt");
    //ImGui::Text(u8"Greek kosme κόσμε");

    // %u 8981      4E00 — 9FFF  	CJK Unified Ideographs
    // %u 3089      3040 — 309F  	Hiragana
    // %u 306A      3040 — 309F  	Hiragana
    // %u 3044.txt  3040 — 309F  	Hiragana
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());                // Basic Latin, Extended Latin
    //builder.AddRanges(io.Fonts->GetGlyphRangesKorean());                 // Default + Korean characters
    //builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());               // Default + Hiragana, Katakana, Half-Width, Selection of 2999 Ideographs
    //builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());            // Default + Half-Width + Japanese Hiragana/Katakana + full set of about 21000 CJK Unified Ideographs
    //builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());// Default + Half-Width + Japanese Hiragana/Katakana + set of 2500 CJK Unified Ideographs for common simplified Chinese
    //builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());               // Default + about 400 Cyrillic characters
    //builder.AddRanges(io.Fonts->GetGlyphRangesThai());                   // Default + Thai characters
    //builder.AddRanges(io.Fonts->GetGlyphRangesVietnamese());             // Default + Vietnamese characters

    builder.BuildRanges(&m_glyph_ranges);

    auto* const glfw_window = reinterpret_cast<GLFWwindow*>(
        get<Window>()->get_context_window()->get_glfw_window()
    );
    ImGui_ImplGlfw_InitForOther(glfw_window, true);
    ImGui_ImplErhe_Init(get<erhe::graphics::OpenGL_state_tracker>().get());

    if (!get<Configuration>()->viewports_hosted_in_imgui_windows)
    {
        m_texture = std::make_shared<Texture>(
            Texture::Create_info{
                .target          = gl::Texture_target::texture_2d,
                .internal_format = gl::Internal_format::rgba8,
                .sample_count    = 0,
                .width           = 1024,
                .height          = 1024
            }
        );
        m_texture->set_debug_label("ImGui Rendertarget");
        const float clear_value[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
        gl::clear_tex_image(
            m_texture->gl_name(),
            0,
            gl::Pixel_format::rgba,
            gl::Pixel_type::float_,
            &clear_value[0]
        );

        Framebuffer::Create_info create_info;
        create_info.attach(gl::Framebuffer_attachment::color_attachment0, m_texture.get());
        m_framebuffer = std::make_unique<Framebuffer>(create_info);
        m_framebuffer->set_debug_label("ImGui Rendertarget");

        add_scene_node();
    }
}

void Editor_imgui_windows::add_scene_node()
{
    auto& mesh_memory = *get<Mesh_memory>().get();
    auto& scene_root  = *get<Scene_root>().get();
    auto gui_material = scene_root.make_material("GUI Quad", glm::vec4{0.1f, 0.1f, 0.2f, 1.0f});
    auto gui_geometry = erhe::geometry::shapes::make_quad(1.0f);

    erhe::graphics::Buffer_transfer_queue buffer_transfer_queue;
    auto gui_primitive = erhe::primitive::make_primitive(
        gui_geometry,
        mesh_memory.build_info
    );

    erhe::primitive::Primitive primitive{
        .material              = gui_material,
        .gl_primitive_geometry = gui_primitive
    };
    m_gui_mesh = std::make_shared<erhe::scene::Mesh>("GUI Quad", primitive);
    m_gui_mesh->visibility_mask() = erhe::scene::Node::c_visibility_gui;

    glm::mat4 m = erhe::toolkit::create_translation(0.0f, 1.0f, 1.0f);
    m_gui_mesh->set_parent_from_node(m);

    scene_root.add(
        m_gui_mesh,
        scene_root.gui_layer()
    );
}

void Editor_imgui_windows::begin_imgui_frame()
{
    ImGui_ImplErhe_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
}

void Editor_imgui_windows::end_and_render_imgui_frame()
{
    ImGui::EndFrame();
    ImGui::Render();

    m_pipeline_state_tracker->shader_stages.reset();
    m_pipeline_state_tracker->color_blend.execute(
        &erhe::graphics::Color_blend_state::color_blend_disabled
    );

    if (m_framebuffer)
    {
        gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer->gl_name());
        gl::viewport        (0, 0, m_texture->width(), m_texture->height());
        gl::clear_color     (0.0f, 0.2f, 0.0f, 1.0f);
        gl::clear           (gl::Clear_buffer_mask::color_buffer_bit);
    }
    else
    {
        m_editor_rendering->bind_default_framebuffer();
        m_editor_rendering->clear();
    }

    ImGui_ImplErhe_RenderDrawData(ImGui::GetDrawData());
    gl::generate_texture_mipmap(m_texture->gl_name());
}

auto Editor_imgui_windows::texture() const -> std::shared_ptr<erhe::graphics::Texture>
{
    return m_texture;
}

void Editor_imgui_windows::menu()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Undo"      )) {}
            if (ImGui::MenuItem("Redo"      )) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Select All")) {}
            if (ImGui::MenuItem("Deselect"  )) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Translate" )) {}
            if (ImGui::MenuItem("Rotate"    )) {}
            if (ImGui::MenuItem("Scale"     )) {}
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Create"))
        {
            if (ImGui::MenuItem("Cube"  )) {}
            if (ImGui::MenuItem("Box"   )) {}
            if (ImGui::MenuItem("Sphere")) {}
            if (ImGui::MenuItem("Torus" )) {}
            if (ImGui::MenuItem("Cone"  )) {}
            if (ImGui::BeginMenu("Brush"))
            {
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Modify"))
        {
            if (ImGui::MenuItem("Ambo"    )) {}
            if (ImGui::MenuItem("Dual"    )) {}
            if (ImGui::MenuItem("Truncate")) {}
            ImGui::EndMenu();
        }
        window_menu();
        ImGui::EndMainMenuBar();
    }

    //ImGui::End();
}

void Editor_imgui_windows::window_menu()
{
    //m_frame_log_window->log("menu");

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));

    if (ImGui::BeginMenu("Window"))
    {
        for (auto window : m_imgui_windows)
        {
            bool enabled = window->is_visibile();
            if (ImGui::MenuItem(window->title().data(), "", &enabled))
            {
                if (enabled) {
                    window->show();
                }
                else
                {
                    window->hide();
                }
            }
        }
        ImGui::MenuItem("Tool Properties", "", &m_show_tool_properties);
        ImGui::MenuItem("ImGui Style Editor", "", &m_show_style_editor);

        ImGui::Separator();
        if (ImGui::MenuItem("Close All"))
        {
            for (auto window : m_imgui_windows)
            {
                window->hide();
            }
            m_show_tool_properties = false;
        }
        if (ImGui::MenuItem("Open All"))
        {
            for (auto window : m_imgui_windows)
            {
                window->show();
            }
            m_show_tool_properties = true;
        }
        ImGui::EndMenu();
    }

    ImGui::PopStyleVar();
}

void Editor_imgui_windows::register_imgui_window(Imgui_window* window)
{
    const std::lock_guard<std::mutex> lock{m_mutex};

    m_imgui_windows.emplace_back(window);
}

void Editor_imgui_windows::imgui_windows()
{
    menu();

    for (auto imgui_window : m_imgui_windows)
    {
        if (imgui_window->is_visibile())
        {
            if (imgui_window->begin())
            {
                imgui_window->imgui();
            }
            imgui_window->end();
        }
    }

    //auto priority_action_tool = get_action_tool(m_pointer_context->priority_action());
    //if (
    //    m_show_tool_properties &&
    //    (priority_action_tool != nullptr)
    //)
    //{
    //    if (ImGui::Begin("Tool Properties", &m_show_tool_properties))
    //    {
    //        priority_action_tool->tool_properties();
    //    }
    //    ImGui::End();
    //}

    if (m_show_style_editor)
    {
        if (ImGui::Begin("Dear ImGui Style Editor", &m_show_style_editor))
        {
            ImGui::ShowStyleEditor();
        }
        ImGui::End();
    }
}

}  // namespace editor
