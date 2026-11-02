#include "assets/asset_fetch.hpp"

#include "app_context.hpp"
#include "asset_browser/asset_browser.hpp"
#include "editor_log.hpp"
#include "scene/scene_commit_queue.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_file/file.hpp"
#include "erhe_imgui/imgui_windows.hpp"

#include <fmt/format.h>
#include <imgui/imgui.h>
#include <httplib.h>
#include <miniz.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <set>
#include <regex>
#include <system_error>
#include <vector>

namespace editor {

namespace {

// The same archive scripts/bistro_frame_bench.py fetches.
constexpr const char* c_bistro_google_drive_file_id = "1GlmOPAURKOgwAshlP8hhiUwg-US-l9JI";

constexpr std::size_t c_max_html_page_size = 1024 * 1024;

// Directory, glTF and license inside the Sponza base scene archive.
constexpr const char* c_sponza_base_entry_prefix = "main_sponza";
constexpr const char* c_sponza_base_gltf         = "NewSponza_Main_glTF_003.gltf";
constexpr const char* c_sponza_base_license      = "credits_license.txt";

auto percent_encode(const std::string& text) -> std::string
{
    static constexpr const char* hex = "0123456789ABCDEF";
    std::string result;
    for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool unreserved =
            ((u >= 'a') && (u <= 'z')) ||
            ((u >= 'A') && (u <= 'Z')) ||
            ((u >= '0') && (u <= '9')) ||
            (u == '-') || (u == '_') || (u == '.') || (u == '~');
        if (unreserved) {
            result.push_back(c);
        } else {
            result.push_back('%');
            result.push_back(hex[(u >> 4) & 0xfu]);
            result.push_back(hex[u & 0xfu]);
        }
    }
    return result;
}

#if defined(__ANDROID__)
// httplib's CA search covers desktop Linux paths; Android keeps its own.
auto get_android_ca_directory() -> std::string
{
    std::error_code error_code;
    if (std::filesystem::is_directory("/apex/com.android.conscrypt/cacerts", error_code)) {
        return "/apex/com.android.conscrypt/cacerts";
    }
    return "/system/etc/security/cacerts";
}
#endif

// Empty for an entry that would land outside the extraction directory.
auto to_safe_relative_path(const char* entry_name) -> std::filesystem::path
{
    const std::u8string u8_name{reinterpret_cast<const char8_t*>(entry_name)};
    const std::filesystem::path path = std::filesystem::path{u8_name}.lexically_normal();
    if (path.empty() || path.has_root_path() || path.is_absolute()) {
        return {};
    }
    const std::filesystem::path::iterator first = path.begin();
    if ((first != path.end()) && (*first == "..")) {
        return {};
    }
    return path;
}

auto percent_decode(const std::string& text) -> std::string
{
    std::string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((text[i] == '%') && ((i + 2) < text.size())) {
            const std::string hex{text.substr(i + 1, 2)};
            char* end = nullptr;
            const long value = std::strtol(hex.c_str(), &end, 16);
            if (end == (hex.c_str() + 2)) {
                result.push_back(static_cast<char>(value));
                i += 2;
                continue;
            }
        }
        result.push_back(text[i]);
    }
    return result;
}

// The part of path below prefix, or nothing when path is not under prefix.
auto strip_prefix(const std::filesystem::path& path, const std::filesystem::path& prefix) -> std::optional<std::filesystem::path>
{
    std::filesystem::path::const_iterator path_it = path.begin();
    for (const std::filesystem::path& prefix_part : prefix) {
        if (prefix_part.empty()) {
            continue;
        }
        if ((path_it == path.end()) || (*path_it != prefix_part)) {
            return std::nullopt;
        }
        ++path_it;
    }
    std::filesystem::path result;
    for (; path_it != path.end(); ++path_it) {
        result /= *path_it;
    }
    return result;
}

// Buffer and image URIs relative to the glTF's directory; data URIs skipped.
auto get_gltf_dependencies(const std::string& gltf_text, std::string& error) -> std::vector<std::filesystem::path>
{
    std::vector<std::filesystem::path> result;
    const nlohmann::json json = nlohmann::json::parse(gltf_text, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        error = "glTF JSON does not parse";
        return result;
    }
    for (const char* key : {"buffers", "images"}) {
        const nlohmann::json::const_iterator array = json.find(key);
        if ((array == json.end()) || !array->is_array()) {
            continue;
        }
        for (const nlohmann::json& item : *array) {
            const nlohmann::json::const_iterator uri = item.find("uri");
            if ((uri == item.end()) || !uri->is_string()) {
                continue;
            }
            const std::string uri_text = uri->get<std::string>();
            if (uri_text.starts_with("data:")) {
                continue;
            }
            const std::string decoded = percent_decode(uri_text);
            result.push_back(std::filesystem::path{std::u8string{reinterpret_cast<const char8_t*>(decoded.c_str())}});
        }
    }
    return result;
}

// Percent-encodes each segment of a relative path for a glTF URI.
auto to_uri(const std::filesystem::path& path) -> std::string
{
    std::string result;
    for (const std::filesystem::path& part : path) {
        if (!result.empty()) {
            result.push_back('/');
        }
        const std::u8string u8_part = part.u8string();
        result += percent_encode(std::string{u8_part.begin(), u8_part.end()});
    }
    return result;
}

auto make_host_gltf(const Asset_fetch_source& source) -> std::string
{
    const std::filesystem::path host_directory = source.host_path.parent_path();
    nlohmann::json files            = nlohmann::json::array();
    nlohmann::json external_assets  = nlohmann::json::array();
    nlohmann::json nodes            = nlohmann::json::array();
    nlohmann::json scene_nodes      = nlohmann::json::array();
    for (std::size_t i = 0, end = source.host_instances.size(); i < end; ++i) {
        const Asset_fetch_host_instance& instance = source.host_instances[i];
        files.push_back({{"uri", to_uri(instance.path.lexically_relative(host_directory))}, {"mimeType", "model/gltf+json"}});
        external_assets.push_back({{"file", i}, {"name", instance.name}});
        nodes.push_back({{"name", instance.name}, {"externalAsset", i}});
        scene_nodes.push_back(i);
    }
    const nlohmann::json host = {
        {"asset",          {{"version", "2.1"}, {"minVersion", "2.1"}, {"generator", "erhe asset fetch"}}},
        {"files",          files},
        {"externalAssets", external_assets},
        {"nodes",          nodes},
        {"scenes",         nlohmann::json::array({{{"name", source.name}, {"nodes", scene_nodes}}})},
        {"scene",          0}
    };
    return host.dump(4);
}

class Extract_context
{
public:
    std::ofstream*                         file;
    std::function<bool(std::uint64_t)>*    on_bytes;
};

auto extract_write(void* opaque, mz_uint64 /*file_offset*/, const void* data, std::size_t size) -> std::size_t
{
    Extract_context* context = static_cast<Extract_context*>(opaque);
    context->file->write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!context->file->good()) {
        return 0;
    }
    if (!(*context->on_bytes)(size)) {
        return 0;
    }
    return size;
}

} // anonymous namespace

auto get_bistro_fetch_source() -> Asset_fetch_source
{
    const std::filesystem::path gltf_path = std::filesystem::path{"bistro_lights_compressed"} / "bistro_lights_c.gltf";
    return Asset_fetch_source{
        .name         = "Bistro",
        .archive_root = std::filesystem::path{"res"} / "editor" / "assets",
        .marker_path  = gltf_path,
        .archives     = {
            Asset_fetch_archive{
                .google_drive_file_id = c_bistro_google_drive_file_id,
                .selection            = Archive_selection::all_entries,
                .gltf_path            = gltf_path
            }
        }
    };
}

// Intel Sample Library (CC BY 4.0); both archives share one Y-up space.
auto get_sponza_fetch_source() -> Asset_fetch_source
{
    const std::filesystem::path sponza_directory{"sponza"};
    const std::filesystem::path base_gltf    {c_sponza_base_gltf};
    const std::filesystem::path curtains_gltf{"NewSponza_Curtains_glTF.gltf"};
    return Asset_fetch_source{
        .name         = "Sponza",
        .archive_root = std::filesystem::path{"res"} / "editor" / "assets",
        .marker_path  = sponza_directory / "sponza.gltf",
        .archives     = {
            Asset_fetch_archive{
                .url          = "https://cdrdv2.intel.com/v1/dl/getContent/830833",
                .entry_prefix = c_sponza_base_entry_prefix,
                .selection    = Archive_selection::gltf_dependencies,
                .gltf_path    = base_gltf,
                .extra_paths  = {c_sponza_base_license},
                .destination  = sponza_directory / "base"
            },
            Asset_fetch_archive{
                .url          = "https://cdrdv2.intel.com/v1/dl/getContent/726650",
                .entry_prefix = "pkg_a_curtains",
                .selection    = Archive_selection::gltf_dependencies,
                .gltf_path    = curtains_gltf,
                .extra_paths  = {"credits_license.txt"},
                .destination  = sponza_directory / "curtains"
            }
        },
        .host_path      = sponza_directory / "sponza.gltf",
        .host_instances = {
            Asset_fetch_host_instance{.name = "Sponza",          .path = sponza_directory / "base" / base_gltf},
            Asset_fetch_host_instance{.name = "Sponza Curtains", .path = sponza_directory / "curtains" / curtains_gltf}
        }
    };
}

auto c_str(const Asset_fetch_state state) -> const char*
{
    switch (state) {
        case Asset_fetch_state::idle:            return "idle";
        case Asset_fetch_state::already_present: return "already present";
        case Asset_fetch_state::downloading:     return "downloading";
        case Asset_fetch_state::extracting:      return "extracting";
        case Asset_fetch_state::completed:       return "completed";
        case Asset_fetch_state::failed:          return "failed";
        case Asset_fetch_state::cancelled:       return "cancelled";
        default:                                 return "?";
    }
}

Asset_fetch::Asset_fetch(App_context& context, Asset_browser& asset_browser)
    : m_context      {context}
    , m_asset_browser{asset_browser}
{
}

Asset_fetch::~Asset_fetch() noexcept
{
    request_cancel();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void Asset_fetch::fetch(const Asset_fetch_source& source)
{
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        if (m_running) {
            log_asset->info("Fetch {}: a fetch is already running", source.name);
            return;
        }
    }
    // A worker that has finished may still await its main-thread completion.
    if (m_thread.joinable()) {
        m_thread.join();
    }

    const std::filesystem::path marker = source.archive_root / source.marker_path;
    std::error_code error_code;
    if (std::filesystem::is_regular_file(marker, error_code)) {
        log_asset->info("Fetch {}: {} exists, nothing to fetch", source.name, erhe::file::to_string(marker));
        std::lock_guard<std::mutex> lock{m_mutex};
        m_status = Asset_fetch_status{
            .state       = Asset_fetch_state::already_present,
            .source_name = source.name,
            .message     = fmt::format("{} exists", erhe::file::to_string(marker))
        };
        return;
    }

    {
        std::lock_guard<std::mutex> lock{m_mutex};
        m_running          = true;
        m_cancel_requested = false;
        m_status           = Asset_fetch_status{
            .state         = Asset_fetch_state::downloading,
            .source_name   = source.name,
            .archive_count = source.archives.size()
        };
    }
    log_asset->info("Fetch {}: {} archive(s)", source.name, source.archives.size());
    m_thread = std::thread{[this, source]() { run(source); }};
}

void Asset_fetch::request_cancel()
{
    std::lock_guard<std::mutex> lock{m_mutex};
    m_cancel_requested = true;
}

void Asset_fetch::imgui(const Asset_fetch_source& source, const float button_width)
{
    std::lock_guard<std::mutex> lock{m_mutex};
    ImGui::PushID(source.name.c_str());
    ERHE_DEFER( ImGui::PopID(); );
    const bool this_source = (m_status.source_name == source.name);
    if (m_running && this_source) {
        const float fraction = (m_status.bytes_total > 0)
            ? static_cast<float>(static_cast<double>(m_status.bytes_done) / static_cast<double>(m_status.bytes_total))
            : 0.0f;
        char overlay[64];
        const fmt::format_to_n_result<char*> overlay_end = fmt::format_to_n(
            overlay, sizeof(overlay) - 1, "{} {}/{}", c_str(m_status.state), m_status.archive_index + 1, m_status.archive_count
        );
        *overlay_end.out = '\0';
        ImGui::ProgressBar(fraction, ImVec2{button_width, 0.0f}, overlay);
        if (!m_cancel_requested && ImGui::SmallButton("Cancel Fetch")) {
            m_cancel_requested = true;
        }
        return;
    }
    char label[128];
    const fmt::format_to_n_result<char*> label_end = fmt::format_to_n(label, sizeof(label) - 1, "Fetch {} Scene", source.name);
    *label_end.out = '\0';
    ImGui::BeginDisabled(m_running);
    if (ImGui::Button(label, ImVec2{button_width, 0.0f})) {
        m_context.imgui_windows->queue([this, source]() { fetch(source); }); // out of the ImGui iteration: starts a thread
    }
    ImGui::EndDisabled();
    if ((m_status.state != Asset_fetch_state::idle) && this_source) {
        if (m_status.message.empty()) {
            ImGui::TextUnformatted(c_str(m_status.state));
        } else {
            ImGui::TextWrapped("%s: %s", c_str(m_status.state), m_status.message.c_str());
        }
    }
}

auto Asset_fetch::is_cancel_requested() const -> bool
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return m_cancel_requested;
}

void Asset_fetch::set_state(const Asset_fetch_state state, const std::string& message)
{
    std::lock_guard<std::mutex> lock{m_mutex};
    m_status.state   = state;
    m_status.message = message;
}

void Asset_fetch::set_archive_index(const std::size_t archive_index)
{
    std::lock_guard<std::mutex> lock{m_mutex};
    m_status.archive_index = archive_index;
    m_status.bytes_done    = 0;
    m_status.bytes_total   = 0;
}

void Asset_fetch::set_progress(const std::uint64_t bytes_done, const std::uint64_t bytes_total)
{
    std::lock_guard<std::mutex> lock{m_mutex};
    m_status.bytes_done  = bytes_done;
    m_status.bytes_total = bytes_total;
}

// Worker thread; staging and the final move: doc/editor/asset_fetch.md "Flow".
void Asset_fetch::run(const Asset_fetch_source& source)
{
    const std::filesystem::path staging_directory = source.archive_root / fmt::format(".fetch_{}", source.name);
    const std::filesystem::path archive_path      = staging_directory / "archive.zip";
    const std::filesystem::path extract_directory = staging_directory / "extracted";

    std::error_code error_code;
    std::filesystem::remove_all(staging_directory, error_code);
    std::filesystem::create_directories(extract_directory, error_code);

    std::string error;
    if (error_code) {
        error = fmt::format("cannot create {}: {}", erhe::file::to_string(extract_directory), error_code.message());
    }
    for (std::size_t i = 0, end = source.archives.size(); (i < end) && error.empty(); ++i) {
        const Asset_fetch_archive& archive = source.archives[i];
        set_archive_index(i);
        set_state(Asset_fetch_state::downloading, {});
        error = download_archive(archive, archive_path);
        if (error.empty()) {
            log_asset->info("Fetch {}: archive {} downloaded, {} bytes", source.name, i, std::filesystem::file_size(archive_path, error_code));
            set_state(Asset_fetch_state::extracting, {});
            error = extract_archive(archive_path, archive, extract_directory);
        }
        std::filesystem::remove(archive_path, error_code);
    }
    if (error.empty() && !source.host_path.empty()) {
        if (!erhe::file::write_file(extract_directory / source.host_path, make_host_gltf(source))) {
            error = fmt::format("cannot write {}", erhe::file::to_string(extract_directory / source.host_path));
        }
    }
    if (error.empty()) {
        std::vector<std::filesystem::path> top_level_entries;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{extract_directory, error_code}) {
            top_level_entries.push_back(entry.path());
        }
        for (const std::filesystem::path& entry : top_level_entries) {
            const std::filesystem::path destination = source.archive_root / entry.filename();
            if (std::filesystem::exists(destination, error_code)) {
                error = fmt::format("{} exists without {}; remove it to fetch again", erhe::file::to_string(destination), erhe::file::to_string(source.marker_path));
                break;
            }
        }
        for (const std::filesystem::path& entry : top_level_entries) {
            if (!error.empty()) {
                break;
            }
            const std::filesystem::path destination = source.archive_root / entry.filename();
            std::filesystem::rename(entry, destination, error_code);
            if (error_code) {
                error = fmt::format("cannot move {} to {}: {}", erhe::file::to_string(entry), erhe::file::to_string(destination), error_code.message());
            }
        }
    }

    std::filesystem::remove_all(staging_directory, error_code);

    if (error.empty()) {
        log_asset->info("Fetch {}: completed", source.name);
        set_state(Asset_fetch_state::completed, erhe::file::to_string(source.archive_root / source.marker_path));
    } else if (is_cancel_requested()) {
        log_asset->info("Fetch {}: cancelled", source.name);
        set_state(Asset_fetch_state::cancelled, {});
    } else {
        log_asset->error("Fetch {}: {}", source.name, error);
        set_state(Asset_fetch_state::failed, error);
    }

    m_context.scene_commit_queue->enqueue([this]() { on_worker_done(); });
}

// Main thread, from the scene commit queue flush.
void Asset_fetch::on_worker_done()
{
    if (m_thread.joinable()) {
        m_thread.join();
    }
    bool completed = false;
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        m_running = false;
        completed = (m_status.state == Asset_fetch_state::completed);
    }
    if (completed) {
        m_asset_browser.scan();
    }
}

auto Asset_fetch::download_archive(const Asset_fetch_archive& archive, const std::filesystem::path& target_path) -> std::string
{
    if (!archive.url.empty()) {
        std::string html_page;
        std::string error = download_url(archive.url, target_path, html_page);
        if (error.empty() && !html_page.empty()) {
            error = fmt::format("{} answered with a page instead of the archive: {}", archive.url, html_page.substr(0, 200));
        }
        return error;
    }
    return download_google_drive_file(archive.google_drive_file_id, target_path);
}

// A large file first answers with the virus-scan page; its form is the download.
auto Asset_fetch::download_google_drive_file(const std::string& file_id, const std::filesystem::path& target_path) -> std::string
{
    std::string html_page;
    const std::string url = fmt::format("https://drive.google.com/uc?export=download&id={}", percent_encode(file_id));
    std::string error = download_url(url, target_path, html_page);
    if (!error.empty() || html_page.empty()) {
        return error;
    }

    std::smatch action_match;
    if (!std::regex_search(html_page, action_match, std::regex{"action=\"([^\"]+)\""})) {
        return fmt::format("Google Drive did not offer a download for {}: {}", file_id, html_page.substr(0, 200));
    }
    std::string confirm_url = action_match[1].str();
    char separator = '?';
    bool has_confirm = false;
    const std::regex field_regex{"name=\"([a-z]+)\" value=\"([^\"]*)\""};
    for (std::sregex_iterator i{html_page.begin(), html_page.end(), field_regex}, end; i != end; ++i) {
        const std::string name  = (*i)[1].str();
        const std::string value = (*i)[2].str();
        has_confirm = has_confirm || (name == "confirm");
        confirm_url += fmt::format("{}{}={}", separator, percent_encode(name), percent_encode(value));
        separator = '&';
    }
    if (!has_confirm) {
        return fmt::format("Google Drive did not offer a download for {}: {}", file_id, html_page.substr(0, 200));
    }

    html_page.clear();
    error = download_url(confirm_url, target_path, html_page);
    if (!error.empty()) {
        return error;
    }
    if (!html_page.empty()) {
        return fmt::format("Google Drive answered {} with a page instead of the file: {}", file_id, html_page.substr(0, 200));
    }
    return {};
}

// Streams a 200 response to target_path; HTML or another status goes to html_page.
auto Asset_fetch::download_url(const std::string& url, const std::filesystem::path& target_path, std::string& html_page) -> std::string
{
    const std::size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        return fmt::format("malformed URL {}", url);
    }
    const std::size_t path_begin  = url.find('/', scheme_end + 3);
    const std::string scheme_host = url.substr(0, path_begin);
    const std::string path        = (path_begin == std::string::npos) ? std::string{"/"} : url.substr(path_begin);

    httplib::Client client{scheme_host};
    client.set_follow_location(true);
    client.set_connection_timeout(30, 0);
    client.set_read_timeout(120, 0);
    client.set_payload_max_length((std::numeric_limits<std::size_t>::max)());
#if defined(__ANDROID__)
    client.set_ca_cert_path(std::string{}, get_android_ca_directory());
#endif

    std::ofstream file;
    bool          to_page{false};
    const httplib::Result result = client.Get(
        path,
        httplib::Headers{},
        [&](const httplib::Response& response) -> bool {
            to_page = (response.status != 200) || response.get_header_value("Content-Type").starts_with("text/html");
            if (to_page) {
                return true;
            }
            file.open(target_path, std::ios::binary | std::ios::trunc);
            return file.is_open();
        },
        [&](const char* data, const std::size_t length) -> bool {
            if (is_cancel_requested()) {
                return false;
            }
            if (to_page) {
                if (html_page.size() < c_max_html_page_size) {
                    html_page.append(data, length);
                }
                return true;
            }
            file.write(data, static_cast<std::streamsize>(length));
            return file.good();
        },
        [&](const std::size_t current, const std::size_t total) -> bool {
            if (!to_page) {
                set_progress(current, total);
            }
            return !is_cancel_requested();
        }
    );
    file.close();

    if (!result) {
        if (is_cancel_requested()) {
            return "cancelled";
        }
        return fmt::format(
            "{}{}: {} (TLS error {})",
            scheme_host, path.substr(0, path.find('?')), httplib::to_string(result.error()), result.ssl_backend_error()
        );
    }
    if (result->status != 200) {
        return fmt::format("{}{}: HTTP status {}", scheme_host, path.substr(0, path.find('?')), result->status);
    }
    return {};
}

auto Asset_fetch::extract_archive(
    const std::filesystem::path& archive_path,
    const Asset_fetch_archive&   archive,
    const std::filesystem::path& extract_root
) -> std::string
{
    const std::string archive_name = erhe::file::to_string(archive_path);
    mz_zip_archive zip{};
    mz_zip_zero_struct(&zip);
    if (!mz_zip_reader_init_file(&zip, archive_name.c_str(), 0)) {
        return fmt::format("{}: {}", archive_name, mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    }
    ERHE_DEFER( mz_zip_reader_end(&zip); );

    // Entry paths relative to the entry prefix; empty for entries outside it.
    const mz_uint entry_count = mz_zip_reader_get_num_files(&zip);
    std::vector<std::filesystem::path> entry_paths(entry_count);
    std::optional<mz_uint> gltf_index{};
    const std::filesystem::path gltf_path = archive.gltf_path.lexically_normal();
    for (mz_uint i = 0; i < entry_count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            return fmt::format("{}: entry {}: {}", archive_name, i, mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
        }
        const std::filesystem::path entry_path = to_safe_relative_path(stat.m_filename);
        if (entry_path.empty()) {
            return fmt::format("{}: entry {} is not a relative path inside the archive root", archive_name, stat.m_filename);
        }
        const std::optional<std::filesystem::path> relative = strip_prefix(entry_path, archive.entry_prefix);
        if (relative.has_value()) {
            entry_paths[i] = relative.value();
            if (entry_paths[i] == gltf_path) {
                gltf_index = i;
            }
        }
    }
    if (!gltf_index.has_value()) {
        return fmt::format("{} does not contain {}", archive_name, erhe::file::to_string(archive.entry_prefix / gltf_path));
    }

    std::vector<bool> selected(entry_count, false);
    if (archive.selection == Archive_selection::all_entries) {
        for (mz_uint i = 0; i < entry_count; ++i) {
            selected[i] = !entry_paths[i].empty();
        }
    } else {
        std::size_t gltf_size = 0;
        void* gltf_data = mz_zip_reader_extract_to_heap(&zip, gltf_index.value(), &gltf_size, 0);
        if (gltf_data == nullptr) {
            return fmt::format("{}: {}: {}", archive_name, erhe::file::to_string(gltf_path), mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
        }
        const std::string gltf_text{static_cast<const char*>(gltf_data), gltf_size};
        mz_free(gltf_data);

        std::string parse_error;
        std::set<std::filesystem::path> wanted;
        wanted.insert(gltf_path);
        for (const std::filesystem::path& dependency : get_gltf_dependencies(gltf_text, parse_error)) {
            wanted.insert((gltf_path.parent_path() / dependency).lexically_normal());
        }
        if (!parse_error.empty()) {
            return fmt::format("{}: {}: {}", archive_name, erhe::file::to_string(gltf_path), parse_error);
        }
        for (const std::filesystem::path& extra_path : archive.extra_paths) {
            wanted.insert(extra_path.lexically_normal());
        }
        for (mz_uint i = 0; i < entry_count; ++i) {
            if (!entry_paths[i].empty() && (wanted.erase(entry_paths[i]) > 0)) {
                selected[i] = true;
            }
        }
        if (!wanted.empty()) {
            return fmt::format("{} lacks {} file(s) its glTF needs, e.g. {}", archive_name, wanted.size(), erhe::file::to_string(*wanted.begin()));
        }
    }

    std::uint64_t total_bytes = 0;
    for (mz_uint i = 0; i < entry_count; ++i) {
        if (selected[i]) {
            mz_zip_archive_file_stat stat{};
            static_cast<void>(mz_zip_reader_file_stat(&zip, i, &stat));
            total_bytes += stat.m_uncomp_size;
        }
    }
    const std::filesystem::path target_directory = extract_root / archive.destination;

    std::uint64_t done_bytes = 0;
    set_progress(0, total_bytes);
    std::function<bool(std::uint64_t)> on_bytes = [&](const std::uint64_t byte_count) -> bool {
        done_bytes += byte_count;
        set_progress(done_bytes, total_bytes);
        return !is_cancel_requested();
    };

    std::error_code error_code;
    for (mz_uint i = 0; i < entry_count; ++i) {
        if (is_cancel_requested()) {
            return "cancelled";
        }
        if (!selected[i]) {
            continue;
        }
        const std::filesystem::path output_path = target_directory / entry_paths[i];
        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            std::filesystem::create_directories(output_path, error_code);
            if (error_code) {
                return fmt::format("cannot create {}: {}", erhe::file::to_string(output_path), error_code.message());
            }
            continue;
        }
        std::filesystem::create_directories(output_path.parent_path(), error_code);
        if (error_code) {
            return fmt::format("cannot create {}: {}", erhe::file::to_string(output_path.parent_path()), error_code.message());
        }
        std::ofstream file{output_path, std::ios::binary | std::ios::trunc};
        if (!file.is_open()) {
            return fmt::format("cannot write {}", erhe::file::to_string(output_path));
        }
        Extract_context context{.file = &file, .on_bytes = &on_bytes};
        if (!mz_zip_reader_extract_to_callback(&zip, i, extract_write, &context, 0)) {
            if (is_cancel_requested()) {
                return "cancelled";
            }
            return fmt::format("{}: {}: {}", archive_name, erhe::file::to_string(entry_paths[i]), mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
        }
    }
    return {};
}

}
