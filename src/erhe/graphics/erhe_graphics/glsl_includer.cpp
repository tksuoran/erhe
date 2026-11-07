#include "erhe_graphics/glsl_includer.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_file/file.hpp"

#include <algorithm>

namespace erhe::graphics {

namespace {

// Stored in IncludeResult::userData. Owns the resolved path (for release-time
// cycle tracking) and the file content (when it came from disk). The preamble
// case stores only the resolved path and leaves content empty, since the
// preamble's buffer is owned by the Glsl_includer itself.
class Include_entry
{
public:
    std::filesystem::path resolved_path;
    std::string           content;
};

} // namespace

Glsl_includer::Glsl_includer(
    std::filesystem::path               primary_dir,
    std::vector<std::filesystem::path>  extra_include_paths,
    std::string                         preamble,
    std::string                         preamble_include_name,
    std::vector<std::filesystem::path>* dependencies_out
)
    : m_primary_dir          {std::move(primary_dir)}
    , m_extra_include_paths  {std::move(extra_include_paths)}
    , m_preamble             {std::move(preamble)}
    , m_preamble_include_name{std::move(preamble_include_name)}
    , m_dependencies_out     {dependencies_out}
{
}

Glsl_includer::~Glsl_includer() = default;

void Glsl_includer::add_included(const std::filesystem::path& path)
{
    if (std::find(m_included.begin(), m_included.end(), path) == m_included.end()) {
        m_included.push_back(path);
    }
}

auto Glsl_includer::includeSystem(const char* header_name, const char*, size_t) -> IncludeResult*
{
    return resolve(header_name);
}

auto Glsl_includer::includeLocal(const char* header_name, const char*, size_t) -> IncludeResult*
{
    return resolve(header_name);
}

auto Glsl_includer::resolve(const char* header_name) -> IncludeResult*
{
    if (header_name == nullptr) {
        return nullptr;
    }

    // Virtual preamble include.
    if (m_preamble_include_name == header_name) {
        const std::filesystem::path preamble_path{m_preamble_include_name};
        if (m_active_paths.count(preamble_path) != 0) {
            log_glsl->warn("#include cycle for {}", m_preamble_include_name);
            return nullptr;
        }
        Include_entry* entry = new Include_entry{preamble_path, {}};
        m_active_paths.insert(preamble_path);
        add_included(preamble_path);
        if (m_dependencies_out != nullptr) {
            m_dependencies_out->push_back(preamble_path);
        }
        return new IncludeResult{
            m_preamble_include_name,
            m_preamble.data(),
            m_preamble.size(),
            entry
        };
    }

    // Resolve against primary_dir, then each extra include path. Use the
    // erhe::file existence helper rather than std::filesystem::exists: on
    // Android the helper probes via SDL_IOFromFile so APK-asset includes
    // are visible (std::filesystem::exists cannot see them).
    std::filesystem::path resolved;
    {
        const std::filesystem::path candidate = m_primary_dir / header_name;
        if (erhe::file::check_is_existing_non_empty_regular_file(
                "Glsl_includer::resolve", candidate, /*silent_if_not_exists=*/true)) {
            resolved = candidate;
        }
    }
    if (resolved.empty()) {
        for (const std::filesystem::path& extra : m_extra_include_paths) {
            const std::filesystem::path candidate = extra / header_name;
            if (erhe::file::check_is_existing_non_empty_regular_file(
                    "Glsl_includer::resolve", candidate, /*silent_if_not_exists=*/true)) {
                resolved = candidate;
                break;
            }
        }
    }
    if (resolved.empty()) {
        log_glsl->warn("#include \"{}\" not found (primary dir: {})", header_name, m_primary_dir.string());
        return nullptr;
    }

    if (m_active_paths.count(resolved) != 0) {
        log_glsl->warn("#include cycle for {}", resolved.string());
        return nullptr;
    }

    auto source = erhe::file::read("Glsl_includer::resolve", resolved);
    if (!source.has_value()) {
        log_glsl->warn("Failed to read included file {}", resolved.string());
        return nullptr;
    }

    Include_entry* entry = new Include_entry{resolved, std::move(source.value())};
    m_active_paths.insert(resolved);
    add_included(resolved);
    if (m_dependencies_out != nullptr) {
        const auto i = std::find(m_dependencies_out->begin(), m_dependencies_out->end(), resolved);
        if (i == m_dependencies_out->end()) {
            m_dependencies_out->push_back(resolved);
        }
    }

    // Log the include, indented by active depth.
    std::string indent;
    for (std::size_t i = 1; i < m_active_paths.size(); ++i) {
        indent += "    ";
    }
    log_glsl->trace("{}{} included", indent, resolved.string());

    return new IncludeResult{
        resolved.generic_string(),
        entry->content.data(),
        entry->content.size(),
        entry
    };
}

void Glsl_includer::releaseInclude(IncludeResult* result)
{
    if (result == nullptr) {
        return;
    }
    Include_entry* entry = static_cast<Include_entry*>(result->userData);
    if (entry != nullptr) {
        m_active_paths.erase(entry->resolved_path);
        delete entry;
    }
    delete result;
}

} // namespace erhe::graphics
