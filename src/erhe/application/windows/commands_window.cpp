#include "erhe/application/windows/commands_window.hpp"

#include "erhe/application/imgui/imgui_viewport.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/commands/key_binding.hpp"
#include "erhe/application/commands/mouse_click_binding.hpp"
#include "erhe/application/commands/mouse_drag_binding.hpp"
#include "erhe/application/commands/mouse_motion_binding.hpp"
#include "erhe/application/commands/mouse_wheel_binding.hpp"
#include "erhe/application/commands/update_binding.hpp"
#include "erhe/toolkit/view.hpp"

#include <gsl/gsl>

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace erhe::application
{

Commands_window::Commands_window()
    : erhe::components::Component{c_type_name}
    , Imgui_window               {c_title, c_type_name}
{
}

Commands_window::~Commands_window() noexcept
{
}

void Commands_window::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
}

void Commands_window::initialize_component()
{
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Commands_window::post_initialize()
{
    m_commands = get<erhe::application::Commands>();
}

void Commands_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)

    const auto* viewport = get_viewport();
    if (viewport->want_capture_keyboard())
    {
        ImGui::TextUnformatted("ImGui Want Capture Keyboard");
    }
    if (viewport->want_capture_mouse())
    {
        ImGui::TextUnformatted("ImGui Want Capture Mouse");
    }

    const auto& commands = m_commands->get_commands();

    ImGui::TextUnformatted("Commands");
    for (const auto& command : commands)
    {
        ImGui::BulletText("%s", command->name());
    }

    {
        ImGui::TextUnformatted("Key Bindings");
        const auto& bindings = m_commands->get_key_bindings();
        for (const auto& binding : bindings)
        {
            ImGui::BulletText(
                "%zu %s %s -> %s",
                binding.get_id(),
                erhe::toolkit::c_str(binding.get_keycode()),
                binding.get_pressed()
                    ? "pressed"
                    : "released",
                binding.get_command()->name()
            );
        }
    }

    {
        ImGui::TextUnformatted("Update Bindings");
        const auto& bindings = m_commands->get_update_bindings();
        for (const auto& binding : bindings)
        {
            ImGui::BulletText(
                "%zu -> %s",
                binding.get_id(),
                binding.get_command()->name()
            );
        }
    }

    {
        ImGui::TextUnformatted("Mouse Bindings");
        {
            const auto& bindings = m_commands->get_mouse_bindings();
            for (const auto& binding : bindings)
            {
                const auto type = binding->get_type();
                switch (type)
                {
                    default:
                    {
                        break;
                    }
                    case erhe::application::Command_binding::Type::Mouse_click:
                    {
                        const auto click_binding = reinterpret_cast<erhe::application::Mouse_click_binding*>(binding.get());
                        ImGui::BulletText(
                            "%zu click button %d -> %s",
                            binding->get_id(),
                            click_binding->get_button(),
                            binding->get_command()->name()
                        );
                        break;
                    }
                    case erhe::application::Command_binding::Type::Mouse_drag:
                    {
                        const auto drag_binding = reinterpret_cast<erhe::application::Mouse_drag_binding*>(binding.get());
                        ImGui::BulletText(
                            "%zu drag button %d -> %s",
                            binding->get_id(),
                            drag_binding->get_button(),
                            binding->get_command()->name()
                        );
                        break;
                    }
                    case erhe::application::Command_binding::Type::Mouse_motion:
                    {
                        ImGui::BulletText(
                            "%zu motion -> %s",
                            binding->get_id(),
                            binding->get_command()->name()
                        );
                        break;
                    }
                }

            }
        }
        {
            const auto& bindings = m_commands->get_mouse_wheel_bindings();
            for (const auto& binding : bindings)
            {
                ImGui::BulletText(
                    "%zu wheel -> %s",
                    binding->get_id(),
                    binding->get_command()->name()
                );
            }
        }
    }
#endif
}

} // namespace erhe::application
