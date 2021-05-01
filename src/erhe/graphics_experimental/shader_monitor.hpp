#pragma once

#include "erhe/graphics/shader_stages.hpp"
#include "erhe/components/component.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>

namespace erhe::graphics
{

class Shader_stages;

class Shader_monitor
    : public erhe::components::Component
{
public:
    Shader_monitor();

    virtual ~Shader_monitor();

    void initialize_component() override;

    void add(Shader_stages::Create_info    create_info,
             gsl::not_null<Shader_stages*> program);

    void update_once_per_frame();

private:
    void set_run(bool value)
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_run = value;
    }

    void poll_thread();

    void add(const std::filesystem::path&  path,
             Shader_stages::Create_info    create_info,
             gsl::not_null<Shader_stages*> program);

    struct Reload_entry
    {
        Reload_entry(Shader_stages::Create_info    create_info,
                     gsl::not_null<Shader_stages*> shader_stages)
            : create_info  {std::move(create_info)}
            , shader_stages{shader_stages}
        {
        }

        Shader_stages::Create_info    create_info;
        gsl::not_null<Shader_stages*> shader_stages;
    };

    struct Compare_object
    {
        auto operator()(const Reload_entry& lhs, const Reload_entry& rhs) const
        -> bool
        {
            return lhs.shader_stages < rhs.shader_stages;
        }
    };

    struct File
    {
        std::filesystem::file_time_type        last_time;
        std::filesystem::path                  path;
        std::set<Reload_entry, Compare_object> reload_entries;
    };

    std::map<std::filesystem::path, File> m_files;
    std::mutex                            m_mutex;
    std::thread                           m_poll_filesystem_thread;
    std::vector<File*>                    m_reload_list;
    bool                                  m_run{false};
};

} // namespace erhe::graphics
