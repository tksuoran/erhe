#pragma once

#include "erhe_dataformat/vertex_format.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace erhe::graphics {

class Device;
class Buffer;

class Vertex_input_attribute
{
public:
    unsigned int             layout_location{0};
    std::size_t              binding;
    std::size_t              stride;
    erhe::dataformat::Format format;
    std::size_t              offset;
    std::string              name;
};

class Vertex_input_binding
{
public:
    std::size_t  binding;
    std::size_t  stride;
    unsigned int divisor;
};

class Vertex_input_state_data
{
public:
    std::vector<Vertex_input_attribute> attributes;
    std::vector<Vertex_input_binding>   bindings;

    static auto make(const erhe::dataformat::Vertex_format& vertex_format) -> Vertex_input_state_data;
};

class Vertex_input_state_impl;
class Vertex_input_state final
{
public:
    Vertex_input_state(Device& device);
    Vertex_input_state(Device& device, Vertex_input_state_data&& create_info);
    ~Vertex_input_state() noexcept;

    void set(const Vertex_input_state_data& data);

    [[nodiscard]] auto get_data() const -> const Vertex_input_state_data&;
    [[nodiscard]] auto get_impl() -> Vertex_input_state_impl&;
    [[nodiscard]] auto get_impl() const -> const Vertex_input_state_impl&;

private:
    std::unique_ptr<Vertex_input_state_impl> m_impl;
};

} // namespace erhe::graphics
