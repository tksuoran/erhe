#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace erhe::imgui {

class Imgui_windows;

class File_dialog_window : public erhe::imgui::Imgui_window
{
public:
    File_dialog_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows
    );

    // Implements Imgui_window
    void imgui() override;

    void open();
    auto get_path() -> std::optional<std::filesystem::path>;

private:
    void scan();

    std::filesystem::path                         m_path;
    std::vector<std::filesystem::directory_entry> m_files;
    std::vector<std::filesystem::directory_entry> m_folders;
};

} // namespace erhe::imgui
