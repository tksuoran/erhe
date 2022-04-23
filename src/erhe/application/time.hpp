#pragma once

#include "erhe/components/components.hpp"

namespace erhe::application {

class Application;
//class Scene_root;

class Time
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name{"Editor_time"};
    static constexpr uint32_t         hash{
        compiletime_xxhash::xxh32(
            c_name.data(),
            c_name.size(),
            {}
        )
    };

    Time();
    ~Time() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }

    // Public API
    [[nodiscard]] auto time() const -> double;
    void start_time           ();
    void update               ();
    void update_fixed_step    (const erhe::components::Time_context& time_context);
    void update_once_per_frame(const erhe::components::Time_context& time_context);
    auto frame_number         () const -> uint64_t;

private:
    // Component dependencies
    //std::shared_ptr<Scene_root> m_scene_root;

    std::chrono::steady_clock::time_point m_current_time;
    double                                m_time_accumulator{0.0};
    double                                m_time            {0.0};
    uint64_t                              m_frame_number{0};
};

} // namespace erhe::application
