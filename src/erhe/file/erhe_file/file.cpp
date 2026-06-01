#include "erhe_file/file.hpp"
#include "erhe_file/file_log.hpp"
#include "erhe_defer/defer.hpp"

#include <fmt/std.h>

#if defined(ERHE_OS_WINDOWS)
#   include <Windows.h>
#   include <shobjidl.h>
#endif

#if defined(ERHE_OS_ANDROID)
#   include <SDL3/SDL_iostream.h>
#   include <SDL3/SDL_error.h>
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
        if (log_file) log_file->error("Exception was thrown in erhe::file::to_string()");
        return {};
    }
}

auto from_string(const std::string& path) -> std::filesystem::path
{
    try {
        return std::filesystem::path((const char8_t*)&*path.c_str());
    } catch (...) {
        if (log_file) log_file->error("Exception was thrown in erhe::file::from_string()");
        return {};
    }
}

[[nodiscard]] auto check_is_existing_non_empty_regular_file(
    const std::string_view       description,
    const std::filesystem::path& path,
    const bool                   silent_if_not_exists
) -> bool
{
#if defined(ERHE_OS_ANDROID)
    // Filesystem-first for migrated paths (config/ and res/editor/assets/):
    // prefer a writable copy migrated from the APK on first launch (see
    // migrate_android_assets_to_writable). For everything else (res/
    // shaders, etc.) keep the existing direct-to-APK behavior. If the
    // filesystem probe is inconclusive, fall back to SDL3's IO layer,
    // which routes relative paths to AAssetManager and absolute paths to
    // the filesystem.
    const std::string path_str = to_string(path);
    const bool is_migrated =
        (path_str.rfind("config/",            0) == 0) ||
        (path_str.rfind("config\\",           0) == 0) ||
        (path_str.rfind("res/editor/assets/",  0) == 0) ||
        (path_str.rfind("res\\editor\\assets\\", 0) == 0);
    if (is_migrated) {
        std::error_code error_code;
        if (std::filesystem::is_regular_file(path, error_code)) {
            const std::uintmax_t size = std::filesystem::file_size(path, error_code);
            if (!error_code && size > 0) {
                return true;
            }
        }
    }
    SDL_IOStream* io = SDL_IOFromFile(path_str.c_str(), "rb");
    if (io == nullptr) {
        if (!silent_if_not_exists && log_file) {
            log_file->warn("{}: SDL_IOFromFile('{}') failed: {}", description, to_string(path), SDL_GetError());
        }
        return false;
    }
    const Sint64 size = SDL_GetIOSize(io);
    SDL_CloseIO(io);
    if (size <= 0) {
        if (log_file) log_file->warn("{}: '{}' is empty or size unknown", description, to_string(path));
        return false;
    }
    return true;
#else
    try {
        std::error_code error_code;
        const bool exists = std::filesystem::exists(path, error_code);
        if (error_code) {
            if (log_file) log_file->warn(
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
                if (log_file) log_file->warn("{}: File '{}' not found", description, to_string(path));
            }
            return false;
        }
        const bool is_regular_file = std::filesystem::is_regular_file(path, error_code);
        if (error_code) {
            if (log_file) log_file->warn(
                "{}: std::filesystem::is_regular_file('{}') returned error code {}: {}",
                description,
                to_string(path),
                error_code.value(),
                error_code.message()
            );
            return false;
        }
        if (!is_regular_file) {
            if (log_file) log_file->warn("{}: File '{}' is not regular file", description, to_string(path));
            return false;
        }
        const bool is_empty = std::filesystem::is_empty(path, error_code);
        if (error_code) {
            if (log_file) log_file->warn(
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
        if (log_file) log_file->error("Exception was thrown in erhe::file::check_is_existing_non_empty_regular_file()");
        return false;
    }
#endif
}

auto read(const std::string_view description, const std::filesystem::path& path) -> std::optional<std::string>
{
#if defined(ERHE_OS_ANDROID)
    // Filesystem-first for migrated paths (config/ and res/editor/assets/):
    // prefer the writable copy migrated from the APK on first launch. This
    // is what makes save_config -> load_config round-trip on Android, and
    // what makes editor asset edits persist: migrated files live in writable
    // storage after migrate_android_assets_to_writable, and subsequent saves
    // overwrite them in place. For all other paths (res/ shaders, anything we
    // deliberately did not migrate) skip the filesystem probe and go straight
    // to SDL_IOFromFile, which on Android routes relative paths to
    // AAssetManager (the bundled APK assets) and absolute paths to the
    // filesystem.
    const std::string path_str = to_string(path);
    const bool is_migrated =
        (path_str.rfind("config/",            0) == 0) ||
        (path_str.rfind("config\\",           0) == 0) ||
        (path_str.rfind("res/editor/assets/",  0) == 0) ||
        (path_str.rfind("res\\editor\\assets\\", 0) == 0);
    if (is_migrated) {
        std::FILE* fs = std::fopen(path_str.c_str(), "rb");
        if (fs != nullptr) {
            std::fseek(fs, 0, SEEK_END);
            const long size = std::ftell(fs);
            std::fseek(fs, 0, SEEK_SET);
            if (size > 0) {
                std::string result(static_cast<std::size_t>(size), '\0');
                const std::size_t got = std::fread(result.data(), 1, static_cast<std::size_t>(size), fs);
                std::fclose(fs);
                if (got == static_cast<std::size_t>(size)) {
                    return result;
                }
                if (log_file) log_file->warn("{}: short read from filesystem '{}' ({}/{}); falling back to APK assets", description, path_str, static_cast<long long>(got), size);
            } else {
                std::fclose(fs);
            }
        }
    }
    SDL_IOStream* io = SDL_IOFromFile(path_str.c_str(), "rb");
    if (io == nullptr) {
        if (log_file) log_file->error("{}: SDL_IOFromFile('{}') failed: {}", description, to_string(path), SDL_GetError());
        return {};
    }
    const Sint64 size = SDL_GetIOSize(io);
    if (size <= 0) {
        SDL_CloseIO(io);
        if (log_file) log_file->error("{}: SDL_GetIOSize('{}') returned {}", description, to_string(path), static_cast<long long>(size));
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    const std::size_t got = SDL_ReadIO(io, result.data(), static_cast<std::size_t>(size));
    SDL_CloseIO(io);
    if (got != static_cast<std::size_t>(size)) {
        if (log_file) log_file->error("{}: SDL_ReadIO('{}') got {} of {} bytes", description, to_string(path), static_cast<long long>(got), static_cast<long long>(size));
        return {};
    }
    return result;
#else
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
            if (log_file) log_file->error("{}: Could not open file '{}' for reading", description, to_string(path));
            return {};
        }

        std::size_t bytes_to_read = file_length;
        std::size_t bytes_read = 0;
        std::string result(file_length, '\0');
        do {
            const auto read_byte_count = std::fread(result.data() + bytes_read, 1, bytes_to_read, file);
            if (read_byte_count == 0) {
                if (log_file) log_file->error("{}: Error reading file '{}'", description, to_string(path));
                return {};
            }
            bytes_read += read_byte_count;
            bytes_to_read -= read_byte_count;
        } while (bytes_to_read > 0);

        std::fclose(file);

        return std::optional<std::string>(result);
    } catch (...) {
        if (log_file) log_file->error("Exception was thrown in erhe::file::read()");
        return {};
    }
#endif
}

#if defined(ERHE_OS_WINDOWS)
auto write_file(std::filesystem::path path, const std::string& text) -> bool
{
    try {
        if (path.empty()) {
            if (log_file) log_file->error("write_file(): path is empty");
            return false;
        }

        FILE* const file = _wfopen(path.wstring().c_str(), L"wb");
        if (file == nullptr) {
            if (log_file) log_file->error("Failed to open '{}' for writing", path);
            return false;
        }
        const size_t res = std::fwrite(text.data(), 1, text.size(), file);
        std::fclose(file);
        if (res != text.size()) {
            if (log_file) log_file->error("Failed to write '{}'", path);
            return false;
        }
        return true;
    } catch (...) {
        if (log_file) log_file->error("Exception was thrown in erhe::file::write_file()");
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
        if (log_file) log_file->error("Exception was thrown in erhe::file::select_file_for_read()");
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
        if (log_file) log_file->error("Exception was thrown in erhe::file::select_file_for_write()");
        return {};
    }
}

#else

auto write_file(std::filesystem::path path, const std::string& text) -> bool
{
    if (path.empty()) {
        if (log_file) log_file->error("write_file(): path is empty");
        return false;
    }

    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        if (log_file) log_file->error("Failed to open '{}' for writing", path.string());
        return false;
    }

    const size_t res = std::fwrite(text.data(), 1, text.size(), file);
    std::fclose(file);

    if (res != text.size()) {
        if (log_file) log_file->error("Failed to write '{}'", path.string());
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
#if defined(ERHE_OS_ANDROID)
    // On Android the working directory is internal storage and the target
    // lives inside the APK; the parent-directory walk does not apply.
    (void)target;
    return;
#else
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
            static const int max_recursion_depth = 32;
            for (int i = 0; i < max_recursion_depth; ++i) {
                std::filesystem::current_path(path, error_code);
                path = std::filesystem::current_path();
                path_string = path.string();
                fprintf(stdout, "Current working directory is %s\n", path_string.c_str());
                const std::filesystem::path target_candidate_path = path / std::filesystem::path{target};
                const bool target_exists = std::filesystem::exists(target_candidate_path, error_code);
                if (target_exists) {
                    break;
                }
                if (path == path.parent_path()) {
                    break;
                }
                path = path.parent_path();
            }
        }
    }

    path = std::filesystem::current_path();
    path_string = path.string();
    fprintf(stdout, "Current working directory is %s\n", path_string.c_str());
#endif
}

auto ensure_directory_exists(std::filesystem::path path) -> bool
{
    std::error_code error_code{};
    const bool exists = std::filesystem::exists(path, error_code);
    if (error_code) {
        if (log_file) log_file->warn(
            "std::filesystem::exists('{}') returned error code {}: {}",
            to_string(path),
            error_code.value(),
            error_code.message()
        );
        return false;
    }
    if (exists) {
        return std::filesystem::is_directory(path);
    }

    std::filesystem::create_directories(path, error_code);
    if (error_code) {
        if (log_file) log_file->warn(
            "std::filesystem::create_directories('{}') returned error code {}: {}",
            to_string(path),
            error_code.value(),
            error_code.message()
        );
        return false;
    }
    return std::filesystem::is_directory(path);
}

#if defined(ERHE_OS_ANDROID)
auto migrate_android_assets_to_writable(const std::string& manifest_asset_path) -> int
{
    // Read the manifest directly from APK assets via SDL_IOFromFile.
    // Bypassing erhe::file::read here is deliberate: the migrator must not
    // pick up a stale, previously-migrated copy of the manifest from
    // writable storage. Each launch reads the bundled manifest fresh, so
    // a build that adds new config files will migrate them on next launch
    // (without re-touching anything already present at the destination).
    SDL_IOStream* manifest_io = SDL_IOFromFile(manifest_asset_path.c_str(), "rb");
    if (manifest_io == nullptr) {
        if (log_file) log_file->error(
            "migrate_android_assets_to_writable: manifest '{}' not found in APK assets: {}",
            manifest_asset_path, SDL_GetError()
        );
        return -1;
    }
    const Sint64 manifest_size = SDL_GetIOSize(manifest_io);
    if (manifest_size <= 0) {
        SDL_CloseIO(manifest_io);
        if (log_file) log_file->error(
            "migrate_android_assets_to_writable: manifest '{}' empty or unreadable",
            manifest_asset_path
        );
        return -1;
    }
    std::string manifest_text(static_cast<std::size_t>(manifest_size), '\0');
    const std::size_t manifest_got = SDL_ReadIO(manifest_io, manifest_text.data(), static_cast<std::size_t>(manifest_size));
    SDL_CloseIO(manifest_io);
    if (manifest_got != static_cast<std::size_t>(manifest_size)) {
        if (log_file) log_file->error(
            "migrate_android_assets_to_writable: short read on manifest '{}' ({}/{} bytes)",
            manifest_asset_path, static_cast<long long>(manifest_got), static_cast<long long>(manifest_size)
        );
        return -1;
    }

    int copied = 0;
    int already_present = 0;
    int failed = 0;

    std::size_t pos = 0;
    while (pos < manifest_text.size()) {
        std::size_t newline = manifest_text.find('\n', pos);
        if (newline == std::string::npos) {
            newline = manifest_text.size();
        }
        std::string line = manifest_text.substr(pos, newline - pos);
        pos = newline + 1;

        // Trim CR (CRLF endings) and surrounding whitespace.
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        std::size_t start = 0;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
            ++start;
        }
        if (start > 0) {
            line.erase(0, start);
        }
        if (line.empty()) {
            continue;
        }

        const std::filesystem::path dest{line};

        // No-overwrite policy: any existing entry at the destination wins,
        // whether it is the prior-migration copy, a user save, or a stale
        // file the user never wanted touched.
        std::error_code error_code;
        if (std::filesystem::exists(dest, error_code)) {
            ++already_present;
            continue;
        }

        // Read source from APK assets via SDL.
        SDL_IOStream* src = SDL_IOFromFile(line.c_str(), "rb");
        if (src == nullptr) {
            if (log_file) log_file->warn(
                "migrate_android_assets_to_writable: source '{}' missing in APK assets: {}",
                line, SDL_GetError()
            );
            ++failed;
            continue;
        }
        const Sint64 src_size = SDL_GetIOSize(src);
        if (src_size < 0) {
            SDL_CloseIO(src);
            ++failed;
            continue;
        }
        std::string content(static_cast<std::size_t>(src_size), '\0');
        if (src_size > 0) {
            const std::size_t got = SDL_ReadIO(src, content.data(), static_cast<std::size_t>(src_size));
            if (got != static_cast<std::size_t>(src_size)) {
                SDL_CloseIO(src);
                if (log_file) log_file->warn(
                    "migrate_android_assets_to_writable: short read on '{}' ({}/{} bytes)",
                    line, static_cast<long long>(got), static_cast<long long>(src_size)
                );
                ++failed;
                continue;
            }
        }
        SDL_CloseIO(src);

        // Make sure the parent directory exists in writable storage.
        const std::filesystem::path parent = dest.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, error_code);
            if (error_code) {
                if (log_file) log_file->warn(
                    "migrate_android_assets_to_writable: create_directories('{}') failed: {} ({})",
                    to_string(parent), error_code.value(), error_code.message()
                );
                ++failed;
                continue;
            }
        }

        if (!write_file(dest, content)) {
            ++failed;
            continue;
        }
        ++copied;
    }

    if (log_file) log_file->info(
        "migrate_android_assets_to_writable('{}'): {} copied, {} already present, {} failed",
        manifest_asset_path, copied, already_present, failed
    );
    return copied;
}
#endif

} // namespace erhe::file
