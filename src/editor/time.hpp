#pragma once

#include "erhe/components/component.hpp"

namespace editor {

class Application;
class Editor_rendering;
class Window;

class Editor_time
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name{"Editor_time"};
    Editor_time();
    ~Editor_time() override;

    // Implements Component
    void connect              () override;

    void start_time           ();
    void update               ();
    void update_fixed_step    (const erhe::components::Time_context& time_context);
    void update_once_per_frame(const erhe::components::Time_context& time_context);

    std::chrono::steady_clock::time_point m_current_time;
    double                                m_time_accumulator{0.0};
    double                                m_time            {0.0};
    uint64_t                              m_frame_number{0};

    std::shared_ptr<Editor_rendering> m_editor_rendering;
    std::shared_ptr<Application     > m_application;
    std::shared_ptr<Window          > m_window;
};

}
