#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

namespace editor {

class Time;

class Time_context
{
public:
    double   dt          {0.0};
    double   time        {0.0};
    uint64_t frame_number{0};
};

class Update_time_base
{
public:
    Update_time_base(Time& time);
    virtual ~Update_time_base() noexcept;

protected:
    Time& m_time;
};

class Update_fixed_step : public virtual Update_time_base
{
public:
    Update_fixed_step();
    ~Update_fixed_step() noexcept override;

    virtual void update_fixed_step(const Time_context&) = 0;
};

class Update_once_per_frame : public virtual Update_time_base
{
public:
    Update_once_per_frame();
    ~Update_once_per_frame() noexcept override;

    virtual void update_once_per_frame(const Time_context&) = 0;
};

class Time
{
public:
    [[nodiscard]] auto time() const -> double;
    void start_time           ();
    void update               ();
    void update_fixed_step    (const Time_context& time_context);
    void update_once_per_frame();
    auto frame_number         () const -> uint64_t;

    void register_update_fixed_step      (Update_fixed_step* entry);
    void register_update_once_per_frame  (Update_once_per_frame* entry);
    void unregister_update_fixed_step    (Update_fixed_step* entry);
    void unregister_update_once_per_frame(Update_once_per_frame* entry);

private:
    std::chrono::steady_clock::time_point m_current_time;
    double                                m_time_accumulator{0.0};
    double                                m_time            {0.0};
    uint64_t                              m_frame_number{0};
    Time_context                          m_last_update;

    std::mutex                          m_mutex;
    std::vector<Update_fixed_step*>     m_update_fixed_step;
    std::vector<Update_once_per_frame*> m_update_once_per_frame;
};

} // namespace editor
