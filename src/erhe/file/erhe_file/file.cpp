#include "erhe_file/file.hpp"
#include "erhe_file/file_log.hpp"
#include "erhe_defer/defer.hpp"

#include <fmt/std.h>

#if defined(ERHE_OS_WINDOWS)
#   include <Windows.h>
#   include <shobjidl.h>
#endif

namespace erhe::file {

auto to_string(const std::filesystem::path& path) -> std::string
{
    try {
        auto utf8 = path.u8string();
        // TODO This is undefined behavior.
        auto s = std::string(reinterpret_cast<char const*>(utf8.data()), utf8.size());
        return s;
    } catch (...) {
        log_file->error("Exception was thrown in erhe::file::to_string()");
        return {};
    }
}

auto from_string(const std::string& path) -> std::filesystem::path
{
    try {
        return std::filesystem::path((const char8_t*)&*path.c_str());
    } catch (...) {
        log_file->error("Exception was thrown in erhe::file::from_string()");
        return {};
    }
}

[[nodiscard]] auto check_is_existing_non_empty_regular_file(
    const std::string_view       description,
    const std::filesystem::path& path,
    const bool                   silent_if_not_exists
) -> bool
{
    try {
        std::error_code error_code;
        const bool exists = std::filesystem::exists(path, error_code);
        if (error_code) {
            log_file->warn(
                "{}: std::filesystem::exists('{}') returned error code {}: {}",
                description,
                to_string(path),
                error_code.value(),
                error_code.message()
            );
            return false;
        }
        if (!exists) {
            if (!silent_if_not_exists) {
                log_file->warn("{}: File '{}' not found", description, to_string(path));
            }
            return false;
        }
        const bool is_regular_file = std::filesystem::is_regular_file(path, error_code);
        if (error_code) {
            log_file->warn(
                "{}: std::filesystem::is_regular_file('{}') returned error code {}: {}",
                description,
                to_string(path),
                error_code.value(),
                error_code.message()
            );
            return false;
        }
        if (!is_regular_file) {
            log_file->warn("{}: File '{}' is not regular file", description, to_string(path));
            return false;
        }
        const bool is_empty = std::filesystem::is_empty(path, error_code);
        if (error_code) {
            log_file->warn(
                "{}: std::filesystem::is_empty('{}') returned error code {}",
                to_string(path),
                error_code.value(),
                error_code.message()
            );
            return {};
        }
        if (is_empty) {
            return false;
        }
        return true;
    } catch (...) {
        log_file->error("Exception was thrown in erhe::file::check_is_existing_non_empty_regular_file()");
        return false;
    }
}

auto read(const std::string_view description, const std::filesystem::path& path) -> std::optional<std::string>
{
    try {
        const bool file_is_ok = check_is_existing_non_empty_regular_file(description, path);
        if (!file_is_ok) {
            return {};
        }

        const std::size_t file_length = std::filesystem::file_size(path);
        std::FILE* file =
    #if defined(_WIN32) // _MSC_VER
            _wfopen(path.c_str(), L"rb");
    #else
            std::fopen(path.c_str(), "rb");
    #endif
        if (file == nullptr) {
            log_file->error("{}: Could not open file '{}' for reading", description, to_string(path));
            return {};
        }

        std::size_t bytes_to_read = file_length;
        std::size_t bytes_read = 0;
        std::string result(file_length, '\0');
        do {
            const auto read_byte_count = std::fread(result.data() + bytes_read, 1, bytes_to_read, file);
            if (read_byte_count == 0) {
                log_file->error("{}: Error reading file '{}'", description, to_string(path));
                return {};
            }
            bytes_read += read_byte_count;
            bytes_to_read -= read_byte_count;
        } while (bytes_to_read > 0);

        std::fclose(file);

        return std::optional<std::string>(result);
    } catch (...) {
        log_file->error("Exception was thrown in erhe::file::read()");
        return {};
    }
}

#if defined(ERHE_OS_WINDOWS)
auto write_file(std::filesystem::path path, const std::string& text) -> bool
{
    try {
        if (path.empty()) {
            log_file->error("write_file(): path is empty");
            return false;
        }

        FILE* const file = _wfopen(path.wstring().c_str(), L"wb");
        if (file == nullptr) {
            log_file->error("Failed to open '{}' for writing", path);
            return false;
        }
        const size_t res = std::fwrite(text.data(), 1, text.size(), file);
        std::fclose(file);
        if (res != text.size()) {
            log_file->error("Failed to write '{}'", path);
            return false;
        }
        return true;
    } catch (...) {
        log_file->error("Exception was thrown in erhe::file::write_file()");
        return false;
    }
}

auto select_file_for_read() -> std::optional<std::filesystem::path>
{
    try {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (!SUCCEEDED(hr)) {
            return {};
        }
        ERHE_DEFER( CoUninitialize(); );

        IFileOpenDialog* file_open_dialog{nullptr};
        hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&file_open_dialog));
        if (!SUCCEEDED(hr) || (file_open_dialog == nullptr)) {
            return {};
        }
        ERHE_DEFER( file_open_dialog->Release(); );

        FILEOPENDIALOGOPTIONS options{0};
        hr = file_open_dialog->GetOptions(&options);
        if (!SUCCEEDED(hr)) {
            return {};
        }
        options = options | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM;

        hr = file_open_dialog->SetOptions(options);
        if (!SUCCEEDED(hr)) {
            return {};
        }

        hr = file_open_dialog->Show(nullptr);
        if (!SUCCEEDED(hr)) {
            return {};
        }

        IShellItem* item{nullptr};
        hr = file_open_dialog->GetResult(&item);
        if (!SUCCEEDED(hr) || (item == nullptr)) {
            return {};
        }
        ERHE_DEFER( item->Release(); );

        PWSTR path{nullptr};
        hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
        if (!SUCCEEDED(hr) || (path == nullptr)) {
            return {};
        }
        ERHE_DEFER( CoTaskMemFree(path); );

        return std::filesystem::path(path);
    } catch (...) {
        log_file->error("Exception was thrown in erhe::file::select_file_for_read()");
        return {};
    }
}

auto select_file_for_write() -> std::optional<std::filesystem::path>
{
    try {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (!SUCCEEDED(hr)) {
            return {};
        }
        ERHE_DEFER( CoUninitialize(); );

        IFileSaveDialog* file_save_dialog{nullptr};
        hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL, IID_IFileSaveDialog, reinterpret_cast<void**>(&file_save_dialog));
        if (!SUCCEEDED(hr) || (file_save_dialog == nullptr)) {
            return {};
        }
        ERHE_DEFER( file_save_dialog->Release(); );

        FILEOPENDIALOGOPTIONS options{0};
        hr = file_save_dialog->GetOptions(&options);
        if (!SUCCEEDED(hr)) {
            return {};
        }
        options |= FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT;

        hr = file_save_dialog->SetOptions(options);
        if (!SUCCEEDED(hr)) {
            return {};
        }

        hr = file_save_dialog->Show(nullptr);
        if (!SUCCEEDED(hr)) {
            return {};
        }

        IShellItem* item{nullptr};
        hr = file_save_dialog->GetResult(&item);
        if (!SUCCEEDED(hr) || (item == nullptr)) {
            return {};
        }
        ERHE_DEFER( item->Release(); );

        PWSTR path{nullptr};
        hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
        if (!SUCCEEDED(hr) || (path == nullptr)) {
            return {};
        }
        ERHE_DEFER( CoTaskMemFree(path); );

        return std::filesystem::path(path);
    } catch (...) {
        log_file->error("Exception was thrown in erhe::file::select_file_for_write()");
        return {};
    }
}

#else

auto write_file(std::filesystem::path path, const std::string& text) -> bool
{
    if (path.empty()) {
        log_file->error("write_file(): path is empty");
        return false;
    }

    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        log_file->error("Failed to open '{}' for writing", path.string());
        return false;
    }

    const size_t res = std::fwrite(text.data(), 1, text.size(), file);
    std::fclose(file);

    if (res != text.size()) {
        log_file->error("Failed to write '{}'", path.string());
        return false;
    }

    return true;
}

auto select_file_for_read() -> std::optional<std::filesystem::path>
{
    // TODO
    return {};
}

auto select_file_for_write() -> std::optional<std::filesystem::path>
{
    // TODO
    return {};
}
#endif

void ensure_working_directory_contains(const char* target)
{
    // Workaround for
    // https://intellij-support.jetbrains.com/hc/en-us/community/posts/27792220824466-CMake-C-git-project-How-to-share-working-directory-in-git
    std::string path_string{};
    std::filesystem::path path{};
    {
        std::error_code error_code{};
        bool found = std::filesystem::exists(target, error_code);
        if (!found) {
            path = std::filesystem::current_path();
            path_string = path.string();
            fprintf(stdout, "%s not found.\nCurrent working directory is %s\n", target, path_string.c_str());
#if defined(ERHE_OS_LINUX)
            char self_path[PATH_MAX];
            ssize_t length = readlink("/proc/self/exe", self_path, PATH_MAX - 1);
            if (length > 0) {
                self_path[length] = '\0';
                fprintf(stdout, "Executable is %s\n", self_path);
            }
#endif
            for (int i = 0; i < 4; ++i) {
                std::filesystem::current_path(path, error_code);
                path = std::filesystem::current_path();
                path_string = path.string();
                fprintf(stdout, "Current working directory is %s\n", path_string.c_str());
                const std::filesystem::path try_path = path / std::filesystem::path("src/hello_swap");
                bool exists_directory = std::filesystem::exists(try_path, error_code);
                if (exists_directory) {
                    const std::filesystem::path erhe_ini_path = try_path / std::filesystem::path{target};
                    const bool exists_erhe_ini = std::filesystem::exists(erhe_ini_path, error_code);
                    if (exists_erhe_ini) {
                        break;
                    }
                } else {
                    path = path.parent_path();
                }
            }
        }
    }

    path = std::filesystem::current_path();
    path_string = path.string();
    fprintf(stdout, "Current working directory is %s\n", path_string.c_str());
}

} // namespace erhe::file
