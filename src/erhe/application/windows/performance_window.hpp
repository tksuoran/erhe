#pragma once

#include "erhe/components/components.hpp"

#include <memory>

namespace erhe::application
{

class Performance_window_impl;

class Performance_window
    : public erhe::components::Component
{
public:
    static constexpr const char* c_title{"Performance_window"};
    static constexpr const char* c_type_name{"Performance_window"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name, compiletime_strlen(c_type_name), {});

    Performance_window ();
    ~Performance_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

private:
    std::unique_ptr<Performance_window_impl> m_impl;
};

extern Performance_window* g_performance_window;

} // namespace editor
