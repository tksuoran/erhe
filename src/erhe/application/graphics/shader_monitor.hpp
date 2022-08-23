#pragma once

#include "erhe/components/components.hpp"
#include "erhe/graphics/shader_stages.hpp"

#include <condition_variable>
#include <mutex>
#include <thread>

namespace erhe::graphics {
    class Shader_stages;
}

namespace erhe::application
{

class Shader_monitor
    : public erhe::components::Component
    , public erhe::components::IUpdate_once_per_frame
{
public:
    static constexpr std::string_view c_type_name{"Shader_monitor"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Shader_monitor ();
    ~Shader_monitor() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Implements IUpdate_once_per_frame
    void update_once_per_frame(const erhe::components::Time_context& time_context) override;

    // Public API
    void set_enabled(bool enabled);
    void add(
        erhe::graphics::Shader_stages::Create_info    create_info,
        gsl::not_null<erhe::graphics::Shader_stages*> program
    );

private:
    void set_run(bool value)
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_run = value;
    }

    void poll_thread();

    void add(
        const fs::path&                                   path,
        const erhe::graphics::Shader_stages::Create_info& create_info,
        gsl::not_null<erhe::graphics::Shader_stages*>     program
    );

    class Reload_entry
    {
    public:
        Reload_entry(
            const erhe::graphics::Shader_stages::Create_info& create_info,
            gsl::not_null<erhe::graphics::Shader_stages*>     shader_stages
        )
            : create_info  {create_info}
            , shader_stages{shader_stages}
        {
        }

        erhe::graphics::Shader_stages::Create_info    create_info;
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
        fs::file_time_type                     last_time;
        fs::path                               path;
        std::set<Reload_entry, Compare_object> reload_entries;
    };

    std::map<fs::path, File> m_files;
    std::mutex               m_mutex;
    std::thread              m_poll_filesystem_thread;
    std::vector<File*>       m_reload_list;
    bool                     m_run{false};
};

} // namespace erhe::application
