#include "imgui_demo.hpp"

#include "application.hpp"
#include "log.hpp"

#include "erhe/toolkit/window.hpp"
#include "erhe/imgui/imgui_impl_erhe.hpp"
#include "erhe/gl/gl.hpp"

#include "backends/imgui_impl_glfw.h"

#include <GLFW/glfw3.h>

#include <cmath>
#include <memory>

namespace sample {

Imgui_demo::Imgui_demo()
    : erhe::components::Component{"ImGui Demo"}
{
}

void Imgui_demo::connect(std::shared_ptr<Application>                          application,
                         std::shared_ptr<erhe::graphics::OpenGL_state_tracker> pipeline_state_tracker)
{
    m_application            = application;
    m_pipeline_state_tracker = pipeline_state_tracker;

    initialization_depends_on(pipeline_state_tracker);
}

void Imgui_demo::disconnect()
{
    m_application           .reset();
    m_pipeline_state_tracker.reset();
}

void Imgui_demo::initialize_component()
{
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    //ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    // Setup Platform/Renderer backends
    auto* glfw_window = reinterpret_cast<GLFWwindow*>(m_application->get_context_window()->get_glfw_window());
    ImGui_ImplGlfw_InitForOther(glfw_window, true);
    ImGui_ImplErhe_Init(m_pipeline_state_tracker);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != NULL);
}


void Imgui_demo::update()
{
    // Start the Dear ImGui frame
    ImGui_ImplErhe_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
    if (m_show_demo_window)
    {
        ImGui::ShowDemoWindow(&m_show_demo_window);
    }

    // 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
    {
        static float f = 0.0f;
        static int counter = 0;

        ImGui::Begin("Hello, world!");                            // Create a window called "Hello, world!" and append into it.

        ImGui::Text("This is some useful text.");                 // Display some text (you can use a format strings too)
        ImGui::Checkbox("Demo Window", &m_show_demo_window);      // Edit bools storing our window open/close state
        ImGui::Checkbox("Another Window", &m_show_another_window);

        ImGui::SliderFloat("float", &f, 0.0f, 1.0f);              // Edit 1 float using a slider from 0.0f to 1.0f
        ImGui::ColorEdit3("clear color", (float*)&m_clear_color); // Edit 3 floats representing a color

        if (ImGui::Button("Button"))                              // Buttons return true when clicked (most widgets return true when edited/activated)
        {
            counter++;
        }

        ImGui::SameLine();
        ImGui::Text("counter = %d", counter);

        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::End();
    }

    // 3. Show another simple window.
    if (m_show_another_window)
    {
        ImGui::Begin("Another Window", &m_show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
        ImGui::Text("Hello from another window!");
        if (ImGui::Button("Close Me"))
        {
            m_show_another_window = false;
        }
        ImGui::End();
    }

    // Rendering
    ImGui::Render();
    int display_w, display_h;
    auto* glfw_window = reinterpret_cast<GLFWwindow*>(m_application->get_context_window()->get_glfw_window());
    glfwGetFramebufferSize(glfw_window, &display_w, &display_h);
    gl::viewport(0, 0, display_w, display_h);
    gl::clear_color(GLfloat(m_clear_color.x * m_clear_color.w),
                    GLfloat(m_clear_color.y * m_clear_color.w),
                    GLfloat(m_clear_color.z * m_clear_color.w),
                    GLfloat(m_clear_color.w));
    gl::clear(gl::Clear_buffer_mask::color_buffer_bit);
    ImGui_ImplErhe_RenderDrawData(ImGui::GetDrawData());
    m_application->get_context_window()->swap_buffers();
}

void Imgui_demo::on_enter()
{
    gl::clear_color  (0.3f, 0.2f, 0.4f, 0.0f);
    gl::clear_depth_f(1.0f);
    gl::clip_control (gl::Clip_control_origin::upper_left,
                      gl::Clip_control_depth::zero_to_one);
    gl::clear_stencil(0);
    gl::disable(gl::Enable_cap::scissor_test);
    gl::disable(gl::Enable_cap::primitive_restart);
    gl::disable(gl::Enable_cap::primitive_restart_fixed_index);
}

}
