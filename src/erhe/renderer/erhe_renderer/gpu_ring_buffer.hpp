//// #pragma once
//// 
//// #include "erhe_renderer/buffer_writer.hpp"
//// #include "erhe_graphics/buffer.hpp"
//// 
//// #include <chrono>
//// #include <deque>
//// #include <limits>
//// #include <optional>
//// 
//// namespace erhe::renderer {
//// 
//// 
//// class GPU_ring_buffer_client
//// {
//// public:
////     GPU_ring_buffer_client(GPU_ring_buffer_server& server);
//// 
////     auto open(gl::Buffer_target buffer_target, Ring_buffer_usage usage, std::size_t byte_count) -> Buffer_range;
//// 
//// private:
////     GPU_ring_buffer_server& m_server;
//// };
//// 
//// } // namespace erhe::renderer
//// 