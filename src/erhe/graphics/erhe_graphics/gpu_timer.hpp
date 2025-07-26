#pragma once

#include <memory>

namespace erhe::graphics {

class Device;

class Gpu_timer_impl;
class Gpu_timer final
{
public:
    Gpu_timer(Device& device, const char* label);
    ~Gpu_timer() noexcept;

    Gpu_timer     (const Gpu_timer&) = delete;
    auto operator=(const Gpu_timer&) = delete;
    Gpu_timer     (Gpu_timer&&)      = delete;
    auto operator=(Gpu_timer&&)      = delete;

    [[nodiscard]] auto last_result() -> uint64_t;
    [[nodiscard]] auto label      () const -> const char*;
    void begin ();
    void end   ();
    void read  ();

private:
    std::unique_ptr<Gpu_timer_impl> m_impl;
};

class Scoped_gpu_timer
{
public:
    explicit Scoped_gpu_timer(Gpu_timer& timer);
    ~Scoped_gpu_timer() noexcept;

private:
    Gpu_timer& m_timer;
};

} // namespace erhe::graphics
