#include "erhe/graphics_experimental/shader_monitor.hpp"
#include "Tracy.hpp"
#include "erhe/log/log.hpp"
#include "erhe/toolkit/file.hpp"

#include <cassert>

namespace erhe::graphics
{

erhe::log::Log::Category log_shader_monitor(erhe::log::Log::Color::CYAN, erhe::log::Log::Color::GRAY, erhe::log::Log::Level::LEVEL_WARN);

using std::string;

Shader_monitor::Shader_monitor()
    : erhe::components::Component{"Shader_monitor"}
    , m_run{false}
{
}

void Shader_monitor::initialize_component()
{
    ZoneScoped;

    set_run(true);
    m_poll_filesystem_thread = std::thread(&Shader_monitor::poll_thread, this);
}

Shader_monitor::~Shader_monitor()
{
    ZoneScoped;

    set_run(false);
    m_poll_filesystem_thread.join();
}


void Shader_monitor::add(Shader_stages::Create_info    create_info,
                         gsl::not_null<Shader_stages*> shader_stages)
{
    for (const auto& shader : create_info.shaders)
    {
        if (shader.source.empty() && std::filesystem::exists(shader.path))
        {
            add(shader.path, create_info, shader_stages);
        }
    }
}

void Shader_monitor::add(const std::filesystem::path&  path,
                         Shader_stages::Create_info    create_info,
                         gsl::not_null<Shader_stages*> shader_stages)
{
    const std::lock_guard<std::mutex> lock(m_mutex);

    auto i = m_files.find(path);
    if (i == m_files.end())
    {
        File f;
        m_files[path] = f;
    }

    auto& f = m_files[path];

    VERIFY(std::filesystem::exists(path));

    f.path = path;

    if (!std::filesystem::exists(f.path))
    {
        f.path.clear();
    }
    else
    {
        f.last_time = std::filesystem::last_write_time(f.path);
        f.reload_entries.emplace(std::move(create_info), shader_stages);
    }
}

void Shader_monitor::poll_thread()
{
    while (m_run)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        {
            ZoneScoped;
            const std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& i : m_files)
            {
                auto& f = i.second;

                // Watch out; filesystem can throw exception for some random reason,
                // like file being externally modified at the same time.
                try
                {
                    bool ok = std::filesystem::exists(f.path) &&
                              !std::filesystem::is_empty(f.path) &&
                              std::filesystem::is_regular_file(f.path);
                    if (ok)
                    {
                        auto time = std::filesystem::last_write_time(f.path);
                        if (f.last_time != time)
                        {
                            m_reload_list.emplace_back(&f);
                            continue;
                        }
                    }
                }
                catch (...)
                {
                    log_shader_monitor.warn("Failed to poll file {}", f.path.generic_string());
                    // Never mind exceptions.
                }
            }
        }
    }
}

void Shader_monitor::update_once_per_frame()
{
    ZoneScoped;

    const std::lock_guard<std::mutex> lock(m_mutex);
    for (auto* f : m_reload_list)
    {
        for (const auto& entry : f->reload_entries)
        {
            const auto& create_info = entry.create_info;
            auto prototype = Shader_stages::Prototype(create_info);
            if (prototype.is_valid())
            {
                entry.shader_stages->reload(std::move(prototype));
            }
        }
        f->last_time = std::filesystem::last_write_time(f->path);

    }
    m_reload_list.clear();
}

} // namespace erhe::graphics
