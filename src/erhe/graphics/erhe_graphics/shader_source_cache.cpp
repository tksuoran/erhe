#include "erhe_graphics/shader_source_cache.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_file/file.hpp"
#include "erhe_profile/profile.hpp"

namespace erhe::graphics {

void Shader_source_cache::preload(const std::filesystem::path& directory)
{
    ERHE_PROFILE_FUNCTION();

    // APK assets are not filesystem entries (Android): the iteration finds
    // nothing there and every file is read through find_or_read() instead.
    std::error_code error_code{};
    std::filesystem::directory_iterator directory_iterator{directory, error_code};
    if (error_code) {
        log_glsl->info("Shader_source_cache: '{}' is not a readable directory", erhe::file::to_string(directory));
        return;
    }
    std::size_t file_count = 0;
    for (const std::filesystem::directory_entry& entry : directory_iterator) {
        if (!entry.is_regular_file(error_code)) {
            continue;
        }
        std::optional<std::string> source = erhe::file::read("Shader_source_cache::preload", entry.path());
        if (!source.has_value()) {
            continue;
        }
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_sources.insert_or_assign(entry.path(), std::move(source.value()));
        ++file_count;
    }
    log_glsl->info("Shader_source_cache: preloaded {} files from '{}'", file_count, erhe::file::to_string(directory));
}

auto Shader_source_cache::find_or_read(const std::filesystem::path& path) -> std::optional<std::string>
{
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        const auto i = m_sources.find(path);
        if (i != m_sources.end()) {
            return i->second;
        }
    }
    // Use erhe::file's existence helper rather than std::filesystem::exists
    // directly: on Android the helper probes via SDL_IOFromFile so APK
    // assets are visible (std::filesystem cannot see them).
    if (!erhe::file::check_is_existing_non_empty_regular_file("Shader_source_cache::find_or_read", path, /*silent_if_not_exists=*/true)) {
        return std::nullopt;
    }
    std::optional<std::string> source = erhe::file::read("Shader_source_cache::find_or_read", path);
    if (source.has_value()) {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_sources.insert_or_assign(path, source.value());
    }
    return source;
}

void Shader_source_cache::erase(const std::filesystem::path& path)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_sources.erase(path);
}

} // namespace erhe::graphics
