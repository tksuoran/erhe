#pragma once

#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_dataformat/vertex_format.hpp"

#include <mutex>
#include <optional>
#include <thread>

namespace erhe::graphics {

class Buffer;
class Device;

class Vertex_input_state_impl
{
public:
    Vertex_input_state_impl(Device& device);
    Vertex_input_state_impl(Device& device, Vertex_input_state_data&& create_info);
    ~Vertex_input_state_impl() noexcept;

    void set   (const Vertex_input_state_data& data);
    void create();
    void reset ();
    void update();

    [[nodiscard]] auto get_data() const -> const Vertex_input_state_data&;

private:
    Device&                 m_device;
    Vertex_input_state_data m_data;
};

} // namespace erhe::graphics
