#include "erhe_rendergraph/texture_rendergraph_node.hpp"

#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_log.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::rendergraph {

Texture_rendergraph_node::Texture_rendergraph_node(const Texture_rendergraph_node_create_info& create_info)
    : Rendergraph_node{create_info.rendergraph, create_info.debug_label}
    , m_render_target {
        Render_target_create_info{
            .graphics_device      = create_info.rendergraph.get_graphics_device(),
            .debug_label          = create_info.debug_label,
            .color_format         = create_info.color_format,
            .depth_stencil_format = create_info.depth_stencil_format,
            .sample_count         = create_info.sample_count,
            .store_depth_stencil  = create_info.store_depth_stencil
        }
    }
    , m_output_key   {create_info.output_key}
    , m_size_requests{create_info.rendergraph.get_graphics_device().get_number_of_frames_in_flight()}
{
    ERHE_VERIFY(!m_size_requests.empty());
    if (create_info.output_key != Rendergraph_node_key::none) {
        register_output(create_info.debug_label, create_info.output_key);
    }
}

Texture_rendergraph_node::~Texture_rendergraph_node() noexcept = default;

auto Texture_rendergraph_node::get_producer_output_texture(const int key, int) const -> std::shared_ptr<erhe::graphics::Texture>
{
    if ((key == m_output_key) || (key == Rendergraph_node_key::wildcard)) {
        return m_render_target.get_color_texture();
    }
    return {};
}

auto Texture_rendergraph_node::get_render_target() -> Render_target&
{
    return m_render_target;
}

auto Texture_rendergraph_node::get_render_target() const -> const Render_target&
{
    return m_render_target;
}

void Texture_rendergraph_node::request_size(const int width, const int height)
{
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);
    const uint64_t frame_index = get_rendergraph().get_graphics_device().get_frame_index();
    Size_request&  request     = m_size_requests[frame_index % m_size_requests.size()];
    request.frame_index = frame_index;
    request.width       = width;
    request.height      = height;
}

void Texture_rendergraph_node::apply_requested_size()
{
    erhe::utility::Thread_roles::assert_render_thread();
    erhe::utility::Thread_roles::assert_frame_phase(erhe::utility::Frame_phase::r_render);
    const uint64_t      frame_index = get_rendergraph().get_graphics_device().get_frame_index();
    const Size_request& request     = m_size_requests[frame_index % m_size_requests.size()];
    if (request.frame_index != frame_index) {
        return;
    }
    m_render_target.update(request.width, request.height, nullptr);
}

} // namespace erhe::rendergraph
