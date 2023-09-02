#include "erhe_file/file.hpp"
#include "erhe_file/file_log.hpp"
#include "erhe_defer/defer.hpp"

#if defined(ERHE_OS_WINDOWS)
#   include <Windows.h>
#   include <shobjidl.h>
#endif

namespace erhe::file
{

auto to_string(const std::filesystem::path& path) -> std::string
{
    auto utf8 = path.u8string();
    auto s = std::string(reinterpret_cast<char const*>(utf8.data()), utf8.size());
    return s;
}

auto from_string(const std::string& path) -> std::filesystem::path
{
    return std::filesystem::u8path(path);
}

[[nodiscard]] auto check_is_existing_non_empty_regular_file(
    const std::string_view       description,
    const std::filesystem::path& path,
    const bool                   silent_if_not_exists
) -> bool
{
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
}

auto read(
    const std::string_view       description,
    const std::filesystem::path& path
) -> std::optional<std::string>
{
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
}

#if defined(ERHE_OS_WINDOWS)
auto select_file() -> std::optional<std::filesystem::path>
{
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
}
#else
auto select_file() -> std::optional<std::filesystem::path>
{
    // TODO
    return {};
}
#endif

} // namespace erhe::file
