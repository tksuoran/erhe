#pragma once

#include "erhe_utility/debug_label.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace erhe::graphics {

class Device;

enum class Binding_type : unsigned int {
    uniform_buffer,
    storage_buffer,
    combined_image_sampler
};

class Bind_group_layout_binding
{
public:
    uint32_t     binding_point{0};
    Binding_type type         {Binding_type::uniform_buffer};
};

class Bind_group_layout_create_info
{
public:
    std::vector<Bind_group_layout_binding> bindings;
    erhe::utility::Debug_label             debug_label;
};

class Bind_group_layout_impl;

class Bind_group_layout
{
public:
    Bind_group_layout (Device& device, const Bind_group_layout_create_info& create_info);
    ~Bind_group_layout() noexcept;
    Bind_group_layout (const Bind_group_layout&) = delete;
    void operator=    (const Bind_group_layout&) = delete;
    Bind_group_layout (Bind_group_layout&& other) noexcept;
    auto operator=    (Bind_group_layout&& other) noexcept -> Bind_group_layout&;

    [[nodiscard]] auto get_impl      () -> Bind_group_layout_impl&;
    [[nodiscard]] auto get_impl      () const -> const Bind_group_layout_impl&;
    [[nodiscard]] auto get_debug_label() const -> erhe::utility::Debug_label;

private:
    std::unique_ptr<Bind_group_layout_impl> m_impl;
};

} // namespace erhe::graphics
