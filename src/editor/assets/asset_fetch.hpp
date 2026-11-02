#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace editor {

class App_context;
class Asset_browser;

enum class Archive_selection : unsigned int {
    all_entries = 0,
    gltf_dependencies // the glTF, the buffers and images it names, and extra_paths
};

// One zip archive of a fetch (doc/editor/asset_fetch.md).
class Asset_fetch_archive
{
public:
    std::string                        url;                  // followed through redirects
    std::string                        google_drive_file_id; // used when url is empty
    std::filesystem::path              entry_prefix;         // archive directory taken as the archive's root
    Archive_selection                  selection{Archive_selection::all_entries};
    std::filesystem::path              gltf_path;            // relative to entry_prefix; required entry
    std::vector<std::filesystem::path> extra_paths;          // relative to entry_prefix
    std::filesystem::path              destination;          // relative to Asset_fetch_source::archive_root
};

// A node of the generated glTF 2.1 host file instancing a fetched glTF.
class Asset_fetch_host_instance
{
public:
    std::string           name;
    std::filesystem::path path; // relative to Asset_fetch_source::archive_root
};

class Asset_fetch_source
{
public:
    std::string                            name;
    std::filesystem::path                  archive_root;
    std::filesystem::path                  marker_path; // relative to archive_root; present == already fetched
    std::vector<Asset_fetch_archive>       archives;
    std::filesystem::path                  host_path;   // relative to archive_root; empty == no host file
    std::vector<Asset_fetch_host_instance> host_instances;
};

[[nodiscard]] auto get_bistro_fetch_source() -> Asset_fetch_source;
[[nodiscard]] auto get_sponza_fetch_source() -> Asset_fetch_source;

enum class Asset_fetch_state : unsigned int {
    idle = 0,
    already_present,
    downloading,
    extracting,
    completed,
    failed,
    cancelled
};

[[nodiscard]] auto c_str(Asset_fetch_state state) -> const char*;

class Asset_fetch_status
{
public:
    Asset_fetch_state state        {Asset_fetch_state::idle};
    std::string       source_name  {};
    std::size_t       archive_index{0};
    std::size_t       archive_count{0};
    std::uint64_t     bytes_done   {0};
    std::uint64_t     bytes_total  {0};
    std::string       message      {};
};

class Asset_fetch
{
public:
    Asset_fetch(App_context& context, Asset_browser& asset_browser);
    ~Asset_fetch() noexcept;

    Asset_fetch(const Asset_fetch&) = delete;
    Asset_fetch& operator=(const Asset_fetch&) = delete;

    // Main thread. Does nothing when the marker file exists or a fetch runs.
    void fetch(const Asset_fetch_source& source);
    void request_cancel();

    // ImGui time: the fetch button for source, or the running fetch's progress.
    void imgui(const Asset_fetch_source& source, float button_width);

private:
    void run(const Asset_fetch_source& source);
    void on_worker_done();
    void set_state(Asset_fetch_state state, const std::string& message);
    void set_archive_index(std::size_t archive_index);
    void set_progress(std::uint64_t bytes_done, std::uint64_t bytes_total);
    [[nodiscard]] auto is_cancel_requested() const -> bool;
    [[nodiscard]] auto download_archive(const Asset_fetch_archive& archive, const std::filesystem::path& target_path) -> std::string;
    [[nodiscard]] auto download_google_drive_file(const std::string& file_id, const std::filesystem::path& target_path) -> std::string;
    [[nodiscard]] auto download_url(const std::string& url, const std::filesystem::path& target_path, std::string& html_page) -> std::string;
    [[nodiscard]] auto extract_archive(const std::filesystem::path& archive_path, const Asset_fetch_archive& archive, const std::filesystem::path& extract_root) -> std::string;

    App_context&       m_context;
    Asset_browser&     m_asset_browser;
    mutable std::mutex m_mutex;
    Asset_fetch_status m_status;
    bool               m_running         {false};
    bool               m_cancel_requested{false};
    std::thread        m_thread;
};

}
