//#include "erhe_scene_renderer/format_pools.hpp"
//
//#include "erhe_scene_renderer/generated/mesh_memory_config.hpp"
//#include "erhe_graphics/buffer_transfer_queue.hpp"
//#include "erhe_graphics/device.hpp"
//
//namespace erhe::scene_renderer {
//
//namespace {
//
//constexpr std::size_t kilo = 1024;
//constexpr std::size_t mega = 1024 * kilo;
//
//[[nodiscard]] auto make_vertex_pool_block_info(
//    const Mesh_memory_config& config,
//    const std::string&        debug_label_prefix
//) -> erhe::graphics_buffer_sink::Buffer_pool_block_create_info
//{
//    return erhe::graphics_buffer_sink::Buffer_pool_block_create_info{
//        .usage                              = erhe::graphics::Buffer_usage::vertex,
//        .required_memory_property_bit_mask  = erhe::graphics::Memory_property_flag_bit_mask::device_local,
//        .preferred_memory_property_bit_mask = erhe::graphics::Memory_property_flag_bit_mask::none,
//        .block_size_bytes                   = static_cast<std::size_t>(config.vertex_pool_block_size_mb) * mega,
//        .max_blocks                         = static_cast<std::size_t>(config.max_buffers_per_pool),
//        .debug_label_prefix                 = debug_label_prefix
//    };
//}
//
//[[nodiscard]] auto build_vertex_pools(
//    erhe::graphics::Device&                graphics_device,
//    erhe::graphics::Buffer_transfer_queue& transfer_queue,
//    const Mesh_memory_config&              config,
//    const erhe::dataformat::Vertex_format& vertex_format,
//    const std::string&                     debug_label_prefix
//) -> std::vector<std::unique_ptr<erhe::graphics_buffer_sink::Buffer_pool>>
//{
//    std::vector<std::unique_ptr<erhe::graphics_buffer_sink::Buffer_pool>> result;
//    result.reserve(vertex_format.streams.size());
//    for (std::size_t stream_index = 0; stream_index < vertex_format.streams.size(); ++stream_index) {
//        const std::string label = debug_label_prefix + " stream " + std::to_string(stream_index);
//        result.push_back(std::make_unique<erhe::graphics_buffer_sink::Buffer_pool>(
//            graphics_device,
//            transfer_queue,
//            stream_index,
//            make_vertex_pool_block_info(config, label)
//        ));
//    }
//    return result;
//}
//
//[[nodiscard]] auto extract_pool_pointers(
//    const std::vector<std::unique_ptr<erhe::graphics_buffer_sink::Buffer_pool>>& pools
//) -> std::vector<erhe::graphics_buffer_sink::Buffer_pool*>
//{
//    std::vector<erhe::graphics_buffer_sink::Buffer_pool*> result;
//    result.reserve(pools.size());
//    for (const std::unique_ptr<erhe::graphics_buffer_sink::Buffer_pool>& pool : pools) {
//        result.push_back(pool.get());
//    }
//    return result;
//}
//
//} // anonymous namespace
//
//Format_pools::Format_pools(
//    erhe::graphics::Device&                  graphics_device,
//    erhe::graphics::Buffer_transfer_queue&   transfer_queue,
//    const Mesh_memory_config&                config,
//    erhe::dataformat::Vertex_format          in_vertex_format,
//    erhe::graphics_buffer_sink::Buffer_pool& index_pool,
//    erhe::graphics_buffer_sink::Buffer_pool& edge_line_vertex_pool
//)
//    : vertex_format{std::move(in_vertex_format)}
//    , vertex_pools{build_vertex_pools(graphics_device, transfer_queue, config, vertex_format, "Format_pools")}
//    , graphics_buffer_sink{
//        extract_pool_pointers(vertex_pools),
//        &index_pool,
//        &edge_line_vertex_pool
//    }
//    , buffer_info{
//        .index_type    = erhe::dataformat::Format::format_32_scalar_uint,
//        .vertex_format = vertex_format,
//        .buffer_sink   = graphics_buffer_sink
//    }
//    , vertex_input{
//        graphics_device,
//        erhe::graphics::Vertex_input_state_data::make(vertex_format)
//    }
//{
//}
//
//Format_pools::~Format_pools() = default;
//
//} // namespace erhe::scene_renderer
//