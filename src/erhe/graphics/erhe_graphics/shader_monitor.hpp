#pragma once

#include "erhe_graphics/shader_stages.hpp"

#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>

namespace erhe::graphics {

class Shader_stages;

class Shader_monitor
{
public:
    Shader_monitor(igl::IDevice& device);
    ~Shader_monitor() noexcept;

    void begin();

    void update_once_per_frame();

    // Public API
    void set_enabled(bool enabled);
    void add(
        Shader_stages_create_info     create_info,
        gsl::not_null<Shader_stages*> program
    );
    void add(Reloadable_shader_stages& reloadable_shader_stages);

private:
    void set_run(bool value)
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_run = value;
    }

    void poll_thread();

    void add(
        const std::filesystem::path&                     path,
        const erhe::graphics::Shader_stages_create_info& create_info,
        gsl::not_null<erhe::graphics::Shader_stages*>    program
    );

    class Reload_entry
    {
    public:
        Reload_entry(
            const erhe::graphics::Shader_stages_create_info& create_info,
            gsl::not_null<erhe::graphics::Shader_stages*>    shader_stages
        )
            : create_info  {create_info}
            , shader_stages{shader_stages}
        {
        }

        erhe::graphics::Shader_stages_create_info     create_info;
        gsl::not_null<erhe::graphics::Shader_stages*> shader_stages;
    };

    class Compare_object
    {
    public:
        [[nodiscard]] auto operator()(
            const Reload_entry& lhs,
            const Reload_entry& rhs
        ) const -> bool
        {
            return lhs.shader_stages < rhs.shader_stages;
        }
    };

    class File
    {
    public:
        std::filesystem::file_time_type        last_time;
        std::filesystem::path                  path;
        std::set<Reload_entry, Compare_object> reload_entries;
    };

    igl::IDevice&                         m_device;
    bool                                  m_run{false};
    std::map<std::filesystem::path, File> m_files;
    std::mutex                            m_mutex;
    std::thread                           m_poll_filesystem_thread;
    std::vector<File*>                    m_reload_list;
};

} // namespace erhe::graphics
