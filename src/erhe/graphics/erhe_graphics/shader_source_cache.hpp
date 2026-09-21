#pragma once

#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace erhe::graphics {

// Text of shader source files, by path. One variant of a shader program is
// built from a few dozen source files, and every variant and every stage
// reads the same ones again, so Glsl_file_loader reads through this cache.
// Owned by the Device; shader stages are built on worker threads, so every
// access takes the mutex.
//
// A cached text stays valid until the file changes on disk: Shader_monitor
// erases the entry of a changed file before it rebuilds the stages that use
// it, which is the only place shader sources are re-read at run time.
class Shader_source_cache
{
public:
    // Reads every regular file directly in the directory. Optional: a file
    // that is not in the cache is read from disk by find_or_read().
    void preload(const std::filesystem::path& directory);

    // The cached text for the path, reading the file on a miss. nullopt
    // when the path is not an existing, non-empty regular file.
    [[nodiscard]] auto find_or_read(const std::filesystem::path& path) -> std::optional<std::string>;

    void erase(const std::filesystem::path& path);

private:
    std::mutex                                   m_mutex;
    std::map<std::filesystem::path, std::string> m_sources;
};

} // namespace erhe::graphics
