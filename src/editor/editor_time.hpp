#pragma once

#include "erhe/components/component.hpp"

namespace editor {

class Application;
class Scene_root;

class Editor_time
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

    Editor_time();
    ~Editor_time() override;

    // Implements Component
    [[nodiscard]]
    auto get_type_hash        () const -> uint32_t override { return hash; }
    void connect              () override;

    void start_time           ();
    void update               ();
    void update_fixed_step    (const erhe::components::Time_context& time_context);
    void update_once_per_frame(const erhe::components::Time_context& time_context);

    [[nodiscard]]
    auto time                 () const -> double;

    std::chrono::steady_clock::time_point m_current_time;
    double                                m_time_accumulator{0.0};
    double                                m_time            {0.0};
    uint64_t                              m_frame_number{0};

    // Component dependencies
    std::shared_ptr<Application> m_application;
    std::shared_ptr<Scene_root>  m_scene_root;
};  
   

}
