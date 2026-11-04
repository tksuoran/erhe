#include "erhe_gltf/gltf_export_progress.hpp"

namespace erhe::gltf {

auto c_str(const Gltf_export_stage stage) -> const char*
{
    switch (stage) {
        case Gltf_export_stage::starting: return "starting";
        case Gltf_export_stage::mesh_data: return "reading mesh data";
        case Gltf_export_stage::meshes:   return "preparing meshes";
        case Gltf_export_stage::scene:    return "writing scene";
        case Gltf_export_stage::textures: return "transcoding textures";
        case Gltf_export_stage::encode:   return "encoding file";
        default:                          return "?";
    }
}

void Gltf_export_progress::begin_stage(const Gltf_export_stage stage, const std::size_t total)
{
    std::lock_guard<std::mutex> lock{m_mutex};
    m_state = Gltf_export_progress_state{.stage = stage, .done = 0, .total = total};
}

void Gltf_export_progress::advance()
{
    std::lock_guard<std::mutex> lock{m_mutex};
    ++m_state.done;
}

auto Gltf_export_progress::get() const -> Gltf_export_progress_state
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return m_state;
}

} // namespace erhe::gltf
