#include "erhe_graphics/shader_monitor.hpp"

#include "erhe_graphics/graphics_log.hpp"
#include "erhe_configuration/configuration.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_file/file.hpp"

namespace erhe::graphics
{

using std::string;

void Shader_monitor::begin()
{
    auto ini = erhe::configuration::get_ini("erhe.ini", "shader_monitor");
    ini->get("enabled", m_run);

    if (!m_run) {
        log_shader_monitor->info("Shader monitor disabled due to erhe.ini setting");
        return;
    }

    m_poll_filesystem_thread = std::thread(&Shader_monitor::poll_thread, this);
}

Shader_monitor::Shader_monitor(igl::IDevice& device)
    : m_device{device}
{
}

Shader_monitor::~Shader_monitor() noexcept
{
    log_shader_monitor->info("Shader_monitor shutting down");
    set_run(false);
    log_shader_monitor->info("Joining shader monitor poll thread");
    if (m_poll_filesystem_thread.joinable()) {
        m_poll_filesystem_thread.join();
    }
    log_shader_monitor->info("Shader_monitor shut down complete");
    m_files.clear();
    m_reload_list.clear();
}

void Shader_monitor::set_enabled(const bool enabled)
{
    set_run(enabled);
}

void Shader_monitor::add(
    erhe::graphics::Shader_stages_create_info     create_info,
    gsl::not_null<erhe::graphics::Shader_stages*> shader_stages
)
{
    for (const auto& shader : create_info.shaders) {
        if (
            shader.source.empty() &&
            erhe::file::check_is_existing_non_empty_regular_file("Shader_monitor::add", shader.path)
        ) {
            add(shader.path, create_info, shader_stages);
        }
    }
}

void Shader_monitor::add(Reloadable_shader_stages& reloadable_shader_stages)
{
    add(reloadable_shader_stages.create_info, &reloadable_shader_stages.shader_stages);
}

void Shader_monitor::add(
    const std::filesystem::path&                     path,
    const erhe::graphics::Shader_stages_create_info& create_info,
    gsl::not_null<erhe::graphics::Shader_stages*>    shader_stages
)
{
    const std::lock_guard<std::mutex> lock{m_mutex};

    auto i = m_files.find(path);
    if (i == m_files.end()) {
        File f;
        m_files[path] = f;
    }

    auto& f = m_files[path];

    f.path = path;

    if (!erhe::file::check_is_existing_non_empty_regular_file("Shader_monitor:add", f.path)) {
        f.path.clear();
    } else {
        f.last_time = std::filesystem::last_write_time(f.path);
        f.reload_entries.emplace(create_info, shader_stages);
    }
}

void Shader_monitor::poll_thread()
{
    while (m_run) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        {
            ERHE_PROFILE_SCOPE("Shader_monitor::poll_thread");

            const std::lock_guard<std::mutex> lock{m_mutex};

            for (auto& i : m_files) {
                auto& f = i.second;

                // Watch out; filesystem can throw exception for some random reason,
                // like file being externally modified at the same time.
                try {
                    const bool ok = erhe::file::check_is_existing_non_empty_regular_file("Shader_monitor::poll_thread", f.path);
                    if (ok) {
                        const auto time = std::filesystem::last_write_time(f.path);
                        if (f.last_time != time) {
                            m_reload_list.emplace_back(&f);
                            continue;
                        }
                    }
                } catch (...) {
                    log_shader_monitor->warn("Failed to poll file {}", f.path.string());
                    // Never mind exceptions.
                }
            }
        }
    }
    log_shader_monitor->info("Exiting shader monitor poll thread");
}

// static constexpr const char* c_shader_monitor_poll = "shader monitor poll";

void Shader_monitor::update_once_per_frame()
{
    ERHE_PROFILE_FUNCTION();

    const std::lock_guard<std::mutex> lock{m_mutex};

    for (auto* f : m_reload_list) {
        for (const auto& entry : f->reload_entries) {
            const auto& create_info = entry.create_info;
            erhe::graphics::Shader_stages_prototype prototype{m_device, create_info};
            if (prototype.is_valid()) {
                entry.shader_stages->reload(std::move(prototype));
                log_shader_monitor->info("Shader reload OK {}", entry.create_info.shaders.front().path.string());
            } else {
                entry.shader_stages->invalidate();
                log_shader_monitor->warn("Shader reload FAIL {}", entry.create_info.shaders.front().path.string());
            }
        }
        std::error_code error_code;
        const auto last_time = std::filesystem::last_write_time(f->path, error_code);
        if (!error_code) {
            f->last_time = last_time;
        }
    }
    m_reload_list.clear();
}

} // namespace erhe::graphics
