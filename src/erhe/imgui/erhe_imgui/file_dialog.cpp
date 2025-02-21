#include "erhe_imgui/file_dialog.hpp"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <filesystem>

using namespace std::chrono_literals;

namespace erhe::imgui {

File_dialog_window::File_dialog_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "File Dialog", {}}
    , m_path{std::filesystem::current_path()}
{
}

void File_dialog_window::scan()
{
    m_folders.clear();
    m_files.clear();
    try {
        for (auto& p : std::filesystem::directory_iterator(m_path)) {
            if (p.is_directory()) {
                m_folders.push_back(p);
            }
            else {
                m_files.push_back(p);
            }
        }
    } catch (...) {
    }
}

void File_dialog_window::imgui()
{
}

} // namespace erhe::imgui